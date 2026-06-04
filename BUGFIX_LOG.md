# IOCP LanServer 디버깅 — 수정 로그

> ⭐ **대표 사례: #19 — PostRecv/FlushSend `CloseSession` 누락 → PlayerMap 누수.**
> 접속 중인 세션 수(`Alive`)와 맵 등록 수(`PlayerMap`)를 비교해 진단 → 즉시 실패 분기에 `CloseSession` 추가로 수정 → **2시간·51만 사이클 연속 부하에서 재발 없음 확인** → [`evidence/LEAK_VERIFICATION.md`](evidence/LEAK_VERIFICATION.md)

## 1. 스택 오버플로우 (해결)
- `Session sessions[MAX_SESSION]`이 클래스 멤버 → EchoServer가 스택에 올라가면서 ~84MB 스택 사용
- `Session* sessions`로 변경, `Start()`에서 `new Session[maxSession]`으로 힙 할당

## 2. 멤버 변수 미초기화 (해결)
- `new Session[maxSession]` 시 MSVC 디버그 모드에서 0xCDCDCDCD 패턴으로 채워짐
- `generation`, `closeFlag` 등이 Start() 초기화 루프에서 빠져있었음
- SID가 `0xFFFFFFFF_CDCDCDCE`로 생성 → `idx = SID >> 48 = 0xFFFF = 65535` → 범위 초과 → SendPacket 전부 실패 → SendTPS=0
- **해결**: Session, CLanServer, CRingBuffer 구조체/클래스 선언부에 기본값 지정

## 3. Start() 초기화 루프 범위 오류 (해결)
- `sessions = new Session[maxSession]`으로 할당하고 루프는 `i < MAX_SESSION`까지 순회
- maxSession < MAX_SESSION이면 할당 범위 초과 접근 (버퍼 오버런)
- **해결**: `for (int i = 0; i < maxSession; ++i)`로 수정

## 4. SendPacket 에러 경로 이중 Release (해결)
- SendPacket 실패 시 내부에서 `packet->Release()` 호출 + 호출자도 `pkt->Release()` 호출 → 이중 해제
- 브로드캐스트 시나리오에서 치명적: 한 패킷을 여러 세션에 보낼 때 실패 경로에서 풀 반환 후 다른 세션의 OnSendComplete에서 크래시
- **해결**: SendPacket 에러 경로의 `packet->Release()` 제거. 소유권은 호출자가 유지.

## 5. SendPacket idx 범위 체크 순서 (해결)
- `&sessions[idx]` 접근 후 범위 체크 → 이미 out-of-bounds 접근 완료
- **해결**: idx 범위 체크 → 세션 접근 → generation 검증 순서로 변경

## 6. ioRefCount 라이프사이클 (해결)
- InitSession에서 `ioRefCount = 0`으로 시작 → 세션 "살아있음" 기본 참조 없음
- OnClientJoin의 WSASend가 PostRecv보다 먼저 완료되면 ioRef=0 → ReleaseSession → 인덱스 풀 반환 → PostRecv가 이미 반환된 세션 사용
- raw 포인터 접근이라 generation 체크 불가
- **해결**: `ioRefCount = 1`로 시작, CloseSession 끝에서 `IORelease`로 기본 참조 회수

## 7. FlushSend 락 누락 (해결)
- FlushSend가 sendLock 없이 sendQ/sqCount 접근
- OnSendComplete → FlushSend 경로에서 다른 스레드의 SendPacket과 동시 접근 → sqCount 꼬임 → null 패킷 읽음 → GetPacketSize() this==nullptr 크래시
- **해결**: FlushSend 내부에서 큐 조작 구간에 EnterCriticalSection/LeaveCriticalSection 추가

## 8. OnClientJoin 에코 오염 (해결)
- OnClientJoin에서 0x7FFF 패킷을 서버가 먼저 전송
- 더미 클라이언트는 자기가 보낸 패킷의 에코를 기대 → RecvEcho Error [32767]
- 에코 에러로 클라이언트가 끊고 재접속 반복 → JOIN/LEAVE 폭주, 이후 TPS=0 프리징
- **해결**: OnClientJoin에서 SendPacket 호출 제거

## 9. scanf_s 인자 누락 (해결)
- `scanf_s("%c", &c)` → `%c`는 버퍼 크기 인자 필수
- **해결**: `scanf_s("%c", &c, 1)`

## 10. CPacket 생성자 중복 정의 (해결)
- 헤더에 선언부 기본값 + cpp에 생성자 본문 → 본문 중복 에러
- **해결**: 선언부 기본값 사용 시 cpp의 생성자 정의 삭제

---

## 2026-05-17 — 채팅서버 컨텐츠 단계 디버깅 + 보강

### 11. Qsize 128 큐 포화 (해결)
- 100 클라 × 200 OverSend 빠른 트래픽에서 SendPacket 의 sqCount >= Qsize 분기 진입 빈번 → false 반환 → 응답 누락 누적 → 클라 Echo Not Recv 가 OverSend × Client = 20000 한도 초과 시 클라 보호 모드 발동 → 송수신 정지 → 서버 STAT 가 TPS=0 진동
- **해결**: Qsize 128 → 512 (4배 여유). 추가로 SendPacket 의 큐 풀 분기에서 명시 `CloseSession(idx)` 호출 (통신 장애 시 명시 정리 + 재접속 유도). SendPacket 진입부에 `closeFlag != 0` 사전 차단 (CloseSession 진행 중 세션에 패킷 race)
- 검증: 50만 패킷/초 안정, 11만 회 cycle 무사고

### 12. Stop() 의 MAX_SESSION 사용 (해결)
- `for (int i = 0; i < MAX_SESSION; ++i) CloseSession(i);` — maxSession < MAX_SESSION(10000) 이면 버퍼 오버런. main.cpp 가 MAX_SESSION 그대로 넘겨 우연히 안 터짐
- **해결**: `MAX_SESSION` → `maxSession` (Stop 의 두 루프)

### 13. Accept/Stat 스레드 종료 동기화 누락 (해결)
- Stop() 에서 워커만 WaitForMultipleObjects, Accept/Stat 핸들은 그냥 CloseHandle → 스레드 살아있는 채로 핸들 닫힘 → UB
- **해결**: WaitForSingleObject(acceptHandle, INFINITE), WaitForSingleObject(statHandle, INFINITE) 추가 후 CloseHandle

### 14. CPacket.h 중복 #pragma once + HeaderSize / HEADER_SIZE 두 매크로 혼용 (해결)
- 중복 #pragma once 1줄 제거
- CLanServer.h 의 `#define HeaderSize 2` 제거 → 모두 `HEADER_SIZE` (CPacket.h) 로 통일

### 15. OnRecvComplete 비정상 len / 링버퍼 포화 무방어 (해결)
- 클라가 잘못된 len (0xFFFF 같은) 보내거나 4096 가까이 채운 경우 → 영원히 처리 못 함 → DirectEnqueueSize()=0 → WSARecv(len=0) → 즉시 bytes=0 → CloseSession 자가 절단
- **해결**: OnRecvComplete 에 `if (len > PACKET_BUFFER_SIZE - HEADER_SIZE) CloseSession(idx); return;` 추가. PostRecv 직전 `if (DirectEnqueueSize == 0) CloseSession(idx); return;` 추가

### 16. main.cpp OnClientJoin/Leave printf 가 부하 시 STAT 묻음 (해결)
- 80K 회 JOIN/LEAVE printf 가 콘솔 락 경합 + 콘솔 버퍼 9000줄 한계로 STAT 묻힘
- **해결**: main.cpp 의 EchoServer 의 OnClientJoin / OnClientLeave 본문에서 printf 제거 (부하 테스트 중)

### 17. CLanServer.h `#define PortNumber 9000` 가 SDK rpcdcep.h 와 충돌 (해결)
- 새로 만든 Protocol.h 가 `#include <Windows.h>` 만 사용 (WIN32_LEAN_AND_MEAN 없이) → rpcdcep.h 활성화 → SDK 함수의 매개변수 이름 `PortNumber` 가 사용자 매크로 9000 으로 치환 → 시스템 헤더에서 정체 불명 구문 오류
- **해결**: 안 쓰는 매크로 `PortNumber` 자체를 `Port_Number` 로 변경 (또는 제거). 교훈 — 일반적 단어를 매크로로 쓰면 SDK 식별자와 충돌 위험

### 18. CChatServer.h `<windows.h>` 단독 include → winsock.h vs winsock2.h 충돌 (해결)
- CChatServer.h 가 `#include <windows.h>` 단독 (WIN32_LEAN_AND_MEAN 없이) → winsock.h 활성화 → 그 다음 CLanServer.h 의 WinSock2.h 와 socket 함수 중복 선언
- **해결**: CChatServer.h 의 `#include <windows.h>` 제거 (CLanServer.h 가 WIN32_LEAN_AND_MEAN + Windows.h 이미 포함)

---

## 2026-05-26~27 — PlayerMap 누수 실측 디버깅

### 19. PostRecv / FlushSend 즉시 실패 분기 CloseSession 누락 → PlayerMap 누수 (해결)
- **발견**: DummyClient 다중 봇 부하 검증 중, 서버 `players` 맵(PlayerMap)이 계속 증가 — 세션은 끊겼는데 맵에서 안 빠짐
- **진단**: `Join`·`Leave` 호출 횟수를 각각 세는 카운터(InterlockedIncrement)를 추가하고, 주기마다 한 줄로 `Alive(=Join−Leave)` vs `PlayerMap(=players.size())` 차이를 출력. 두 값의 차이가 계속 쌓이고 증가율이 (+17/초)에서 점점 완만해지는 패턴으로 누수 경로 확정. (함수마다 printf 를 찍으면 노이즈가 심해서, 차이를 한 줄로 모아 출력한 게 핵심)
- **원인**: `CLanServer.cpp` 의 `PostRecv`(~337) / `FlushSend`(~418) 즉시 실패 분기 `SOCKET_ERROR && WSAGetLastError != WSA_IO_PENDING` 에서 `IORelease` 만 호출하고 `CloseSession` 누락.
  - RefCount 짝은 맞아 `ReleaseSession` 정상 → 세션 풀 반환은 OK
  - **그러나 `OnClientLeave` 가 호출 안 됨** → `players` 맵·룸·`Handle_Leave` 정리 누락 → PlayerMap leak
  - = **객체 수명(RefCount) 축**과 **비즈니스 통보(OnClientLeave) 축**이 분리되어 있는데 후자만 누락. 부하 시 즉시 실패 분기를 타는 세션에서 간헐 발생
- **해결**: 두 즉시 실패 분기에 `CloseSession(idx)` 호출 추가 (Worker close 분기 `ok==false || bytes==0` 패턴을 그대로 복사). 순서 = `CloseSession` → `IORelease` (해제 가능 연산을 마지막에)
  - 패턴: `if (success) ...; else IORelease; return;` → `if (success) ...; else { CloseSession; IORelease; return; }`
- **검증**: 봇 가동 중 `Alive ≈ 봇 수` 안정 + 봇 종료 후 `Alive=0` 도달. 1000 동접 30분 안정 (해결).
  - 추가 입증: 2시간 연속 부하 · 51만 번 접속·해제에서 재발 없음 → `evidence/LEAK_VERIFICATION.md`
