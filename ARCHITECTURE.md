# IOCP Chat Server — 아키텍처

> IOCP 기반 채팅서버 (컨텐츠 큐 + Update 단일 스레드) + MSSQL 비동기 통합.
> 빌드 = Visual Studio 2022 / MSSQL Express 2019 / ODBC Driver 17.

---

## 1. 한 화면 다이어그램

```
┌────────────┐    TCP    ┌───────────────────────────────────────────┐    ODBC    ┌──────────────┐
│ 클라이언트 │ ────────→ │              IOCP Chat Server              │ ─────────→ │ MSSQL Express│
│ (DummyClient│           │                                            │            │  ChatServer  │
│  + 게임 클라)│ ←──────── │                                            │ ←───────── │   DB         │
└────────────┘           └───────────────────────────────────────────┘            └──────────────┘
                                            │
            ┌───────────────────────────────┼────────────────────────────────┐
            ▼                               ▼                                ▼
   ┌──────────────┐              ┌──────────────────┐              ┌──────────────────┐
   │ Accept Thread│              │ IOCP Worker ×2N  │              │ DB Worker ×1     │
   │   (×1)       │              │ Recv/Send 완료   │              │ sp_Login/SignUp  │
   └──────┬───────┘              └─────────┬────────┘              └─────────▲────────┘
          │ Session 생성                   │ OnRecv → CPacket             │ pushResult
          ▼                                ▼                              │
   ┌──────────────────────────────────────────────────────────┐           │
   │            ContentQueue  ──→  Update Thread (×1)         │           │
   │                                  │                       │           │
   │           ┌──── PopResult ◀──────┴──── PushJob ──────────┘
   │           │
   │           ▼
   │   Handle_Login / Handle_SignUp / Handle_Recv / Handle_DbResult
   │                    │
   │                    ▼
   │   CPlayer  / CRoom × 5 / CRoomManager   (단일 스레드 단독 접근 → 락 X)
   └──────────────────────────────────────────────────────────┘
```

---

## 2. 레이어 분리

| 레이어 | 책임 | 클래스 |
|---|---|---|
| 네트워크 엔진 | IOCP, Accept, Send/Recv 완료, 세션 풀, 패킷 풀, 통계 | `CLanServer`, `CRingBuffer`, `CPacket`, `CLockFreeStack` |
| 프로토콜 | 패킷 타입 / 결과 코드 / 페이로드 형식 / sp ↔ 클라 매핑 | `Protocol.h` |
| 컨텐츠 | 로그인 / 룸 입퇴장 / 채팅 broadcast / DB 결과 처리 | `CChatServer`, `CPlayer`, `CRoom`, `CRoomManager`, `CContentQueue` |
| DB | ODBC 연결 / sp 호출 / 비동기 큐 | `CDBConnector`, `CDBWorker`, `DBJob.h` |

`CChatServer : public CLanServer` — 가상 함수 4개 (`OnRecv`, `OnClientJoin`, `OnClientLeave`, `OnConnectionRequest`) 오버라이드.

---

## 3. 스레드 모델 (컨텐츠 큐 단일화 + DB 비동기)

| 스레드 | 수 | 역할 | 락 |
|---|---|---|---|
| Accept | 1 | 블로킹 accept 루프, 세션 풀에서 alloc | sessionPoolLock |
| IOCP Worker | `2 × CPU` | GQCS 대기, Recv/Send 완료 처리. **컨텐츠 안 만짐** | sendLock (per session) |
| Update | 1 | ContentQ Pop + DBResult Pop → 핸들러 호출. **컨텐츠 단독 소유** | 락 X |
| DB Worker | 1 | DBJob Pop → sp 동기 호출 → DBResult Push | csJobs / csResults |
| Stat | 1 | 1초 주기 네트워크 통계 출력 | (Interlocked) |

**핵심 — 컨텐츠 큐 단일화의 의미**:
- IOCP 워커가 컨텐츠 객체 (CPlayer, CRoom) 를 직접 만지면 룸/플레이어마다 락이 필요 → 락 수·경합 증가
- 큐 1개 + Update 1개로 단일화 → 락 0개로 컨텐츠 보호
- 단점: Update 가 CPU 1 코어만 사용. 채팅 트래픽 (검증 51K cycle, SendTPS 38K) 에는 충분.

**DB 비동기의 의미**:
- sp 호출 = ms ~ 수십 ms 블로킹
- Update 가 직접 호출하면 그 시간 동안 모든 채팅 일시정지
- DBWorker 가 흡수 → Update 는 PushJob 즉시 return → 다음 메시지 처리

---

## 4. 패킷 프로토콜

### 4.1 헤더

```
[WORD Len][WORD Type][Payload...]
```

- `Len` = `Type(2)` + `Payload` 의 합 (네트워크 레이어가 다루는 길이)
- `Type` 은 페이로드 첫 필드 — CLanServer 는 `Len` 만 보고 CChatServer 가 `Type` 분기

### 4.2 페이로드 (DB-5 확장)

| 패킷 | 페이로드 |
|---|---|
| `CS_LOGIN_REQ` (1) | `WCHAR account[32] + WCHAR password[64]` |
| `CS_SIGNUP_REQ` (5) | `WCHAR account[32] + WCHAR password[64] + WCHAR nickname[32]` |
| `CS_ROOM_ENTER_REQ` (2) | `WORD roomNo` |
| `CS_ROOM_LEAVE_REQ` (3) | (없음) |
| `CS_CHAT_REQ` (4) | `WORD chatLen + char chat[chatLen]` |
| `SC_LOGIN_RES` (101) | `BYTE result + INT userNo + WCHAR nickname[32]` |
| `SC_SIGNUP_RES` (106) | `BYTE result + INT userNo` |
| `SC_ROOM_ENTER_RES` (102) | `BYTE result + WORD roomNo` |
| `SC_CHAT_RES` (103) | `char nickname[32] + WORD chatLen + char chat[chatLen]` |
| `SC_USER_JOIN` (104) | `char nickname[32]` |
| `SC_USER_LEAVE` (105) | `char nickname[32]` |

채팅·룸 페이로드는 컨텐츠 단계 그대로 `char` 유지 (DB 통합 최소 변경). 로그인·회원가입만 `wchar` (DB 의 NVARCHAR 매칭).

### 4.3 ResultCode 매핑

sp 의 `int` result → 클라 `ResultCode enum` 매핑 헬퍼:

```cpp
MapLoginResult:  0→SUCCESS, 1→INVALID_ACCOUNT, 2→INVALID_PASSWORD
MapSignUpResult: 0→SUCCESS, 1→DUPLICATE_ACCOUNT, 2→DUPLICATE_NICKNAME
```

→ DB sp 코드가 바뀌어도 클라 호환성 깨지지 않게 매핑 레이어 격리.

---

## 5. 세션 / 패킷 풀 핵심

### 5.1 SessionID 16:48 비트 분할

```
[상위 16bit 인덱스 (max 65K)][하위 48bit generation (~280조 회 재사용)]
```

- 인덱스 = 세션 풀 슬롯
- generation = 같은 슬롯 재사용 시 +1 → stale ID 방지 (이전 사용자의 메시지가 새 사용자에게 가지 않음)

### 5.2 ioRefCount 라이프사이클

```
세션 시작            ioRefCount = 1
WSARecv pending      AddRef → ioRefCount = 2
Recv 완료 처리       Release → 1
CloseSession 호출    closeFlag = true (즉시 끊지 않음)
모든 pending I/O 완료 → ioRefCount = 0 → ReleaseSession (풀 반환)
```

→ "처리 중 세션 끊으면 안 됨" 원칙. select 의 `g_DisconnectReserve` 와 같은 의도, 메커니즘만 다름.

### 5.3 CPacket RefCount (브로드캐스트)

```
Alloc           refCount = 1                       (호출자)
for (룸 내 N 명) SendPacket(sid, pkt) → AddRef     refCount = N + 1
호출자 res->Release()                              refCount = N
각 OnSendComplete 마다 Release                     refCount → 0
                                                    → 풀 (CLockFreeStack) 반환
```

→ 한 채팅 패킷이 룸 N 명에게 브로드캐스트되는 동안 풀로 안 돌아감.

### 5.4 CLockFreeStack — ABA 방지

```
Top = [17bit stamp][47bit pointer]   (총 64bit, 8byte CAS)
```

- 47bit pointer = x64 유저모드 가상주소는 하위 47비트로 표현됨(상위 비트는 0) → 포인터 47 + stamp 17 = 64비트
- 17bit stamp = Push/Pop 마다 +1 단조 증가 (약 13만 회 wrap)
- 같은 주소 재사용되어도 stamp 가 달라 CAS 실패 → ABA 차단

---

## 6. DB 통합 흐름 (DB-3 ~ DB-7)

### 6.1 시퀀스 — Login

```
[클라]                  [IOCP Worker]            [Update]             [DBWorker]            [MSSQL]
  │ CS_LOGIN_REQ ──→ OnRecv → CPacket             │                     │                    │
  │                    ContentQueue.Push           │                     │                    │
  │                                                ▼                     │                    │
  │                                          Pop → Handle_Login          │                    │
  │                                          DBJob 만들기 (account/pw)   │                    │
  │                                          dbWorker.PushJob ────────→ │                    │
  │                                          (즉시 return — 비블록)      │                    │
  │                                                                      ▼                    │
  │                                                                    popJob                 │
  │                                                                    db.Login() ─────────→ EXEC sp_Login
  │                                                                                            │
  │                                                                                            ▼
  │                                                                                          (수십 ms)
  │                                                                                            │
  │                                                                    ◀─── SELECT 결과셋 ────┘
  │                                                                    pushResult                
  │                                                                      │                    
  │                                                ◀──── PopResult ─────┘                    
  │                                          Handle_DbResult                                   
  │                                          MapLoginResult                                    
  │                                          player.SetUserNo + SetNickname (성공 시)          
  │                                          SC_LOGIN_RES 패킷 조립                            
  │                                          SendPacket                                        
  │ ←──── SC_LOGIN_RES (result + userNo + nickname) ──────                                     
```

### 6.2 CDBConnector — 1 connector = 1 connection

```
생성자          멤버만 초기화 (실제 연결 X)
Connect()       SQLAllocHandle(ENV) → SetEnvAttr(ODBC3) → AllocHandle(DBC) → DriverConnect
                "Driver={ODBC Driver 17 for SQL Server};Server=localhost\SQLEXPRESS;...;Trusted_Connection=yes"
Login()         AllocHandle(STMT) → SQLBindParameter ×2 → ExecDirect("EXEC sp_Login ?, ?")
                → SQLBindCol ×3 (Result, UserNo, Nickname) → SQLFetch → FreeHandle(STMT)
SignUp()        AllocHandle(STMT) → SQLBindParameter ×3 → ExecDirect("EXEC sp_SignUp ?, ?, ?")
                → SQLBindCol ×2 → SQLFetch → FreeHandle(STMT)
소멸자          Disconnect + FreeHandle (DBC → ENV 역순)
```

**스레드 안전 X** — 1 connector = 1 워커 스레드 정책 (STMT 상태 공유 동기화 회피).

### 6.3 CDBWorker — 큐 두 개 (입력/출력)

```
[외부]  PushJob (lock)  ──→  jobs 큐  ──→  popJob (lock)
                                                  │
                                              [WorkerLoop 스레드]
                                              CDBConnector::Login / SignUp
                                                  │
[외부]  PopResult (lock) ←─── results 큐 ←──── pushResult (lock)
```

- 외부 (Update) 와 워커 스레드가 **다른 락 두 개** 잡음 → 경합 최소
- 큐 1개 (in/out 섞이면) 동기화 폭증
- graceful 종료: `isRun = false` → 남은 잡 다 처리 후 스레드 종료

---

## 7. MSSQL 스키마 (`myIOCP/db/01-schema.sql`)

```sql
CREATE TABLE Users (
    UserNo   INT NOT NULL PRIMARY KEY IDENTITY(1, 1),
    Account  NVARCHAR(32) NOT NULL UNIQUE,
    Password NVARCHAR(64) NOT NULL,
    Nickname NVARCHAR(32) NOT NULL UNIQUE,
    RegDate  DATETIME2 NOT NULL DEFAULT SYSUTCDATETIME()
);

CREATE PROCEDURE sp_Login @Account NVARCHAR(32), @PassWord NVARCHAR(64)
AS BEGIN
    SET NOCOUNT ON;           -- INSERT/UPDATE 의 rowcount 메시지 끔 (ODBC 표준 패턴)
    DECLARE @UserNo INT = 0;
    DECLARE @StoredPassword NVARCHAR(64) = N'';
    DECLARE @Nickname NVARCHAR(32) = N'';

    SELECT @UserNo = UserNo, @StoredPassword = Password, @Nickname = Nickname
    FROM Users WHERE Account = @Account;

    IF @UserNo = 0
    BEGIN SELECT 1 AS Result, 0 AS UserNo, N'' AS Nickname; RETURN; END
    IF @StoredPassword <> @PassWord
    BEGIN SELECT 2 AS Result, 0 AS UserNo, N'' AS Nickname; RETURN; END

    SELECT 0 AS Result, @UserNo AS UserNo, @Nickname AS Nickname;
END

CREATE PROCEDURE sp_SignUp @Account NVARCHAR(32), @Password NVARCHAR(64), @Nickname NVARCHAR(32)
AS BEGIN
    SET NOCOUNT ON;
    IF EXISTS (SELECT 1 FROM Users WHERE Account = @Account)
    BEGIN SELECT 1 AS Result, 0 AS UserNo; RETURN; END
    IF EXISTS (SELECT 1 FROM Users WHERE Nickname = @Nickname)
    BEGIN SELECT 2 AS Result, 0 AS UserNo; RETURN; END

    INSERT INTO Users (Account, Password, Nickname) VALUES (@Account, @Password, @Nickname);
    SELECT 0 AS Result, SCOPE_IDENTITY() AS UserNo;
END
```

설계 결정:
- 비밀번호 평문 — 학습 단계. 운영은 bcrypt / argon2
- `SCOPE_IDENTITY()` — 트리거 영향 안 받음 (`@@IDENTITY` 대비 안전)
- `SET NOCOUNT ON` — INSERT 결과의 rowcount 메시지가 ODBC `SQLFetch` 에서 "Invalid cursor state (24000)" 유발

---

## 8. 검증 결과 요약

| 단계 | 검증 | 결과 |
|---|---|---|
| 컨텐츠 (이전) | DummyClient `--auto 500` × 5분 | 51,927 cycle / SendTPS 38K / Err 0 |
| DB-3 | CDBConnector 단독 6 케이스 | 모든 분기 (성공/실패/중복) 정상 |
| DB-4 | CDBWorker 비동기 3 잡 | PushJob 즉시 return + 결과 큐 수신 |
| DB-7 (통합) | DummyClient `--auto 30` × 4분 | 7050+ Login OK / Fail 0 / Err 0 / Queue 0 |
