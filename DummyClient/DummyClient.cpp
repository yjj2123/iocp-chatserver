#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Win
ws.h>
#include <process.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cwchar>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "libcrypto.lib")   // vcpkg OpenSSL

// 서버 Protocol.h 와 일치
enum PacketType : WORD
{
    CS_LOGIN_REQ      = 1,
    CS_ROOM_ENTER_REQ = 2,
    CS_ROOM_LEAVE_REQ = 3,
    CS_CHAT_REQ       = 4,
    CS_SIGNUP_REQ     = 5,
    SC_LOGIN_RES      = 101,
    SC_ROOM_ENTER_RES = 102,
    SC_CHAT_RES       = 103,
    SC_USER_JOIN      = 104,
    SC_USER_LEAVE     = 105,
    SC_SIGNUP_RES     = 106,
};

#define MAX_NICKNAME_LEN  32
#define MAX_ACCOUNT_LEN   32
#define MAX_PASSWORD_LEN  64
#define MAX_CHAT_LEN      256
// ===== 패킷 암호화 (서버 myIOCP/CPacket.h 와 100% 동일해야 통신됨) =====
#define PKT_NONCE_OFFSET  2
#define PKT_NONCE_SIZE    12
#define PKT_MAC_OFFSET    14
#define PKT_MAC_SIZE      16
#define PKT_MAC_FULL_SIZE 32
#define PKT_HEADER_SIZE   30   // len2 + nonce12 + mac16

static const BYTE PKT_AES_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};
static const BYTE PKT_MAC_KEY[32] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
};

// ===== Global Stats (auto mode) =====
static volatile LONG g_botRunning = 1;
static volatile LONG g_connectTotal = 0;
static volatile LONG g_sendChatTotal = 0;
static volatile LONG g_recvChatTotal = 0;
static volatile LONG g_recvAnyTotal = 0;
static volatile LONG g_disconnTotal = 0;
static volatile LONG g_aliveBotCount = 0;
static volatile LONG g_loginSuccess = 0;
static volatile LONG g_loginFail = 0;
static volatile LONG g_signupSent = 0;
static volatile LONG g_tamperSent = 0;   // 일부러 보낸 변조 패킷 수 (서버 CksumMismatch 와 대조)

// 분리 카운터 (Err 원인 추적)
static volatile LONG g_socketErr = 0;
static volatile LONG g_connectErr = 0;
static volatile LONG g_cycleErr = 0;

static const char* g_serverIp = "127.0.0.1";
static int g_serverPort = 6000;

// ===== Send Helpers =====

// 서버 CPacket::Encode 와 동일 — 평문 → [len|nonce|mac|cipher]
static int Encode(const BYTE* plain, int plainLen, BYTE* out)
{
    RAND_bytes(out + PKT_NONCE_OFFSET, PKT_NONCE_SIZE);

    BYTE iv[16] = { 0 };
    memcpy(iv, out + PKT_NONCE_OFFSET, PKT_NONCE_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outLen = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, PKT_AES_KEY, iv);
    EVP_EncryptUpdate(ctx, out + PKT_HEADER_SIZE, &outLen, plain, plainLen);
    EVP_CIPHER_CTX_free(ctx);

    *(WORD*)out = (WORD)plainLen;

    BYTE mac[PKT_MAC_FULL_SIZE];
    unsigned int macLen = 0;
    HMAC_CTX* hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, PKT_MAC_KEY, 32, EVP_sha256(), nullptr);
    HMAC_Update(hctx, out, PKT_MAC_OFFSET);
    HMAC_Update(hctx, out + PKT_HEADER_SIZE, plainLen);
    HMAC_Final(hctx, mac, &macLen);
    HMAC_CTX_free(hctx);
    memcpy(out + PKT_MAC_OFFSET, mac, PKT_MAC_SIZE);

    return PKT_HEADER_SIZE + plainLen;
}

// 서버 CPacket::Decode 와 동일 — MAC 검증 후 복호화. 실패 시 -1
static int Decode(const BYTE* in, int inLen, BYTE* outPlain)
{
    WORD len = *(WORD*)in;
    const BYTE* nonce   = in + PKT_NONCE_OFFSET;
    const BYTE* recvMac = in + PKT_MAC_OFFSET;
    const BYTE* cipher  = in + PKT_HEADER_SIZE;

    BYTE calcMac[PKT_MAC_FULL_SIZE];
    unsigned int macLen = 0;
    HMAC_CTX* hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, PKT_MAC_KEY, 32, EVP_sha256(), nullptr);
    HMAC_Update(hctx, in, PKT_MAC_OFFSET);
    HMAC_Update(hctx, in + PKT_HEADER_SIZE, len);
    HMAC_Final(hctx, calcMac, &macLen);
    HMAC_CTX_free(hctx);

    if (memcmp(recvMac, calcMac, PKT_MAC_SIZE) != 0) return -1;

    BYTE iv[16] = { 0 };
    memcpy(iv, nonce, PKT_NONCE_SIZE);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outLen = 0;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, PKT_AES_KEY, iv);
    EVP_DecryptUpdate(ctx, outPlain, &outLen, cipher, len);
    EVP_CIPHER_CTX_free(ctx);

    return len;
}

static bool SendAll(SOCKET s, const char* buf, int len)
{
    int sent = 0;
    while (sent < len)
    {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static bool SendPacket(SOCKET s, const char* payload, WORD payloadLen)
{
    char buf[1024];
    if (PKT_HEADER_SIZE + payloadLen > sizeof(buf)) return false;
    int total = Encode((const BYTE*)payload, payloadLen, (BYTE*)buf);
    return SendAll(s, buf, total);
}

// DB-5 페이로드: WCHAR account[32] + WCHAR password[64]
static bool SendLogin(SOCKET s, const wchar_t* account, const wchar_t* password)
{
    const WORD payloadSize = 2 + sizeof(wchar_t) * MAX_ACCOUNT_LEN + sizeof(wchar_t) * MAX_PASSWORD_LEN;
    char payload[payloadSize]{};
    WORD type = CS_LOGIN_REQ;
    memcpy(payload, &type, 2);

    // wchar 고정 길이 박스 (null 패딩).
    wchar_t* pAcc = (wchar_t*)(payload + 2);
    wchar_t* pPw  = (wchar_t*)(payload + 2 + sizeof(wchar_t) * MAX_ACCOUNT_LEN);
    wcsncpy_s(pAcc, MAX_ACCOUNT_LEN, account, _TRUNCATE);
    wcsncpy_s(pPw,  MAX_PASSWORD_LEN, password, _TRUNCATE);

    return SendPacket(s, payload, payloadSize);
}

// DB-5 페이로드: WCHAR account[32] + WCHAR password[64] + WCHAR nickname[32]
static bool SendSignUp(SOCKET s, const wchar_t* account, const wchar_t* password, const wchar_t* nickname)
{
    const WORD payloadSize = 2
        + sizeof(wchar_t) * MAX_ACCOUNT_LEN
        + sizeof(wchar_t) * MAX_PASSWORD_LEN
        + sizeof(wchar_t) * MAX_NICKNAME_LEN;
    char payload[payloadSize]{};
    WORD type = CS_SIGNUP_REQ;
    memcpy(payload, &type, 2);

    wchar_t* pAcc  = (wchar_t*)(payload + 2);
    wchar_t* pPw   = (wchar_t*)(payload + 2 + sizeof(wchar_t) * MAX_ACCOUNT_LEN);
    wchar_t* pNick = (wchar_t*)(payload + 2 + sizeof(wchar_t) * MAX_ACCOUNT_LEN
                                            + sizeof(wchar_t) * MAX_PASSWORD_LEN);
    wcsncpy_s(pAcc,  MAX_ACCOUNT_LEN,  account,  _TRUNCATE);
    wcsncpy_s(pPw,   MAX_PASSWORD_LEN, password, _TRUNCATE);
    wcsncpy_s(pNick, MAX_NICKNAME_LEN, nickname, _TRUNCATE);

    return SendPacket(s, payload, payloadSize);
}

static bool SendRoomEnter(SOCKET s, WORD roomNo)
{
    char payload[4]{};
    WORD type = CS_ROOM_ENTER_REQ;
    memcpy(payload, &type, 2);
    memcpy(payload + 2, &roomNo, 2);
    return SendPacket(s, payload, 4);
}

static bool SendRoomLeave(SOCKET s)
{
    char payload[2]{};
    WORD type = CS_ROOM_LEAVE_REQ;
    memcpy(payload, &type, 2);
    return SendPacket(s, payload, 2);
}

static bool SendChat(SOCKET s, const char* chat)
{
    WORD chatLen = (WORD)strlen(chat) + 1;
    if (chatLen > MAX_CHAT_LEN) chatLen = MAX_CHAT_LEN;
    char payload[2 + 2 + MAX_CHAT_LEN]{};
    WORD type = CS_CHAT_REQ;
    memcpy(payload, &type, 2);
    memcpy(payload + 2, &chatLen, 2);
    memcpy(payload + 4, chat, chatLen);
    return SendPacket(s, payload, 2 + 2 + chatLen);
}

// 변조 송신: 정상 Encode 후 cipher 1바이트를 뒤집어 전송 → 서버 MAC 검증 실패 유발
static bool SendChatTampered(SOCKET s, const char* chat)
{
    WORD chatLen = (WORD)strlen(chat) + 1;
    if (chatLen > MAX_CHAT_LEN) chatLen = MAX_CHAT_LEN;
    char payload[2 + 2 + MAX_CHAT_LEN]{};
    WORD type = CS_CHAT_REQ;
    memcpy(payload, &type, 2);
    memcpy(payload + 2, &chatLen, 2);
    memcpy(payload + 4, chat, chatLen);
    WORD payloadLen = 2 + 2 + chatLen;

    char buf[1024];
    int total = Encode((const BYTE*)payload, payloadLen, (BYTE*)buf);
    buf[PKT_HEADER_SIZE] ^= 0x01;   // cipher 첫 바이트 변조 → MAC 불일치
    return SendAll(s, buf, total);
}

// ===== Receive Drain (non-blocking) =====
// 들어온 패킷 다 소비. SC_LOGIN_RES result 파싱 → 카운터 갱신.
// verbose = true 면 SC_CHAT_RES / SC_USER_JOIN / SC_USER_LEAVE 콘솔 출력 (관찰자 봇용).
static bool DrainRecv(SOCKET s, char* recvBuf, int& used, int bufSize, int timeoutMs, bool verbose = false)
{
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s, &rset);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = select(0, &rset, nullptr, nullptr, &tv);
    if (r == 0) return true;
    if (r < 0) return false;

    int n = recv(s, recvBuf + used, bufSize - used, 0);
    if (n <= 0) return false;
    used += n;

    while (1)
    {
        if (used < PKT_HEADER_SIZE) break;
        WORD payloadLen;
        memcpy(&payloadLen, recvBuf, sizeof(WORD));
        if (used < PKT_HEADER_SIZE + payloadLen) break;

        char plain[8192];
        int plainLen = Decode((const BYTE*)recvBuf, PKT_HEADER_SIZE + payloadLen, (BYTE*)plain);
        if (plainLen < 0) return false;   // MAC 검증 실패

        const char* body = plain;

        WORD type;
        memcpy(&type, body, 2);

        if (type == SC_LOGIN_RES)
        {
            BYTE result = (BYTE)body[2];   // body[0..1] = type, body[2] = result
            if (result == 0) InterlockedIncrement(&g_loginSuccess);
            else             InterlockedIncrement(&g_loginFail);
        }
        if (type == SC_CHAT_RES)
        {
            InterlockedIncrement(&g_recvChatTotal);
            if (verbose)
            {
                // SC_CHAT_RES 페이로드: [WORD type][char nick[32]][WORD chatLen][char chat[chatLen]]
                char nick[MAX_NICKNAME_LEN + 1]{};
                memcpy(nick, body + 2, MAX_NICKNAME_LEN);
                WORD chatLen;
                memcpy(&chatLen, body + 2 + MAX_NICKNAME_LEN, 2);
                char chat[MAX_CHAT_LEN + 1]{};
                int copy = chatLen < MAX_CHAT_LEN ? chatLen : MAX_CHAT_LEN;
                memcpy(chat, body + 2 + MAX_NICKNAME_LEN + 2, copy);
                printf("    [CHAT] %s: %s\n", nick, chat);
            }
        }
        if (verbose && type == SC_USER_JOIN)
        {
            char nick[MAX_NICKNAME_LEN + 1]{};
            memcpy(nick, body + 2, MAX_NICKNAME_LEN);
            printf("    [+] %s joined\n", nick);
        }
        if (verbose && type == SC_USER_LEAVE)
        {
            char nick[MAX_NICKNAME_LEN + 1]{};
            memcpy(nick, body + 2, MAX_NICKNAME_LEN);
            printf("    [-] %s left\n", nick);
        }
        InterlockedIncrement(&g_recvAnyTotal);

        int consumed = PKT_HEADER_SIZE + payloadLen;
        memmove(recvBuf, recvBuf + consumed, used - consumed);
        used -= consumed;
    }
    return true;
}

// ===== Auto Bot Thread =====

static unsigned __stdcall BotThread(void* arg)
{
    int idx = (int)(intptr_t)arg;

    wchar_t account[MAX_ACCOUNT_LEN];
    wchar_t password[MAX_PASSWORD_LEN];
    wchar_t nickname[MAX_NICKNAME_LEN];
    char    chatBuf[64];
    swprintf_s(account,  L"Bot%05d", idx);
    swprintf_s(password, L"pw");
    swprintf_s(nickname, L"Bot%05d", idx);

    char recvBuf[8192];
    bool needSignUp = true;     // 첫 cycle 만 SignUp
    bool verbose    = (idx == 0);   // 봇 0 = 관찰자 (채팅 출력)

    InterlockedIncrement(&g_aliveBotCount);

    while (g_botRunning)
    {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
        {
            InterlockedIncrement(&g_socketErr);
            Sleep(1000);
            continue;
        }

        SOCKADDR_IN addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)g_serverPort);
        inet_pton(AF_INET, g_serverIp, &addr.sin_addr);

        if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) != 0)
        {
            InterlockedIncrement(&g_connectErr);
            closesocket(sock);
            Sleep(500);
            continue;
        }
        InterlockedIncrement(&g_connectTotal);

        int used = 0;
        bool sessionOk = true;

        // 1. SignUp (첫 cycle 만 — 중복 응답 받아도 OK)
        if (needSignUp)
        {
            if (!SendSignUp(sock, account, password, nickname)) { sessionOk = false; goto endSession; }
            InterlockedIncrement(&g_signupSent);
            DrainRecv(sock, recvBuf, used, sizeof(recvBuf), 50, verbose);
            needSignUp = false;
        }

        // 2. Login
        if (!SendLogin(sock, account, password)) { sessionOk = false; goto endSession; }
        DrainRecv(sock, recvBuf, used, sizeof(recvBuf), 50, verbose);

        // 3. enter room (관찰자 = 룸 1 고정, 일반 봇 = 랜덤)
        WORD roomNo = verbose ? 1 : (WORD)((rand() % 5) + 1);
        if (!SendRoomEnter(sock, roomNo)) { sessionOk = false; goto endSession; }
        DrainRecv(sock, recvBuf, used, sizeof(recvBuf), 5, verbose);

        // 4. chat loop (관찰자는 채팅 안 보내고 listen 만)
        if (verbose)
        {
            // 관찰자 — 5초 동안 룸 1 청취
            DWORD start = GetTickCount();
            while (GetTickCount() - start < 5000 && g_botRunning && sessionOk)
            {
                if (!DrainRecv(sock, recvBuf, used, sizeof(recvBuf), 100, true))
                {
                    sessionOk = false;
                    break;
                }
            }
        }
        else
        {
            int chatCount = 10 + (rand() % 100);
            for (int k = 0; k < chatCount && g_botRunning && sessionOk; ++k)
            {
                sprintf_s(chatBuf, "Bot%05d msg#%d", idx, k);
                if (rand() % 20 == 0)   // 5% 확률로 변조 패킷 전송
                {
                    SendChatTampered(sock, chatBuf);
                    InterlockedIncrement(&g_tamperSent);
                    // 서버가 MAC 불일치로 이 세션을 끊음 → 아래 DrainRecv 가 끊김 감지 → 재접속 사이클
                }
                else
                {
                    if (!SendChat(sock, chatBuf)) { sessionOk = false; break; }
                    InterlockedIncrement(&g_sendChatTotal);
                }
                if (!DrainRecv(sock, recvBuf, used, sizeof(recvBuf), 2)) { sessionOk = false; break; }
                Sleep(30 + (rand() % 100));
            }
        }

        // 5. leave
        if (sessionOk) SendRoomLeave(sock);

    endSession:
        closesocket(sock);
        InterlockedIncrement(&g_disconnTotal);
        if (!sessionOk) InterlockedIncrement(&g_cycleErr);

        Sleep(200 + (rand() % 500));
    }

    InterlockedDecrement(&g_aliveBotCount);
    return 0;
}

// ===== Stat Thread =====

static unsigned __stdcall StatThread(void*)
{
    LONG lastConn = 0, lastSend = 0, lastRecv = 0;
    LONG lastSockErr = 0, lastConnErr = 0, lastCycleErr = 0;
    LONG lastLoginOk = 0, lastLoginFail = 0;
    int sec = 0;
    while (g_botRunning)
    {
        Sleep(1000);
        sec++;
        LONG conn = g_connectTotal;
        LONG send = g_sendChatTotal;
        LONG recv = g_recvChatTotal;
        LONG disc = g_disconnTotal;
        LONG alive = g_aliveBotCount;
        LONG sockErr = g_socketErr;
        LONG connErr = g_connectErr;
        LONG cycleErr = g_cycleErr;
        LONG loginOk = g_loginSuccess;
        LONG loginFail = g_loginFail;
        LONG signups = g_signupSent;
        LONG tamper = g_tamperSent;

        LONG cTps = conn - lastConn;
        LONG sTps = send - lastSend;
        LONG rTps = recv - lastRecv;
        LONG sockTps = sockErr - lastSockErr;
        LONG connTps = connErr - lastConnErr;
        LONG cycleTps = cycleErr - lastCycleErr;
        LONG loginOkTps = loginOk - lastLoginOk;
        LONG loginFailTps = loginFail - lastLoginFail;
        lastConn = conn; lastSend = send; lastRecv = recv;
        lastSockErr = sockErr; lastConnErr = connErr; lastCycleErr = cycleErr;
        lastLoginOk = loginOk; lastLoginFail = loginFail;

        printf("[%4ds] Bots=%ld | Conn=%ld(+%ld) Disc=%ld | LoginOK=%ld(+%ld) LoginFail=%ld(+%ld) SignUps=%ld Tamper=%ld | Send=%ld(+%ld) Recv=%ld(+%ld) | Err sock=%ld(+%ld) conn=%ld(+%ld) cycle=%ld(+%ld)\n",
               sec, alive,
               conn, cTps, disc,
               loginOk, loginOkTps, loginFail, loginFailTps, signups, tamper,
               send, sTps, recv, rTps,
               sockErr, sockTps, connErr, connTps, cycleErr, cycleTps);
    }
    return 0;
}

// ===== Interactive Mode (기존) =====

static SOCKET g_intSock = INVALID_SOCKET;
static volatile LONG g_intRunning = 1;

static unsigned __stdcall InteractiveRecvThread(void*)
{
    char buf[8192];
    int used = 0;
    while (g_intRunning)
    {
        int n = recv(g_intSock, buf + used, sizeof(buf) - used, 0);
        if (n <= 0)
        {
            printf("\n[!] Disconnected from server\n");
            InterlockedExchange(&g_intRunning, 0);
            break;
        }
        used += n;
        while (1)
        {
            if (used < PKT_HEADER_SIZE) break;
            WORD payloadLen;
            memcpy(&payloadLen, buf, sizeof(WORD));
            if (used < PKT_HEADER_SIZE + payloadLen) break;
            char plain[8192];
            int plainLen = Decode((const BYTE*)buf, PKT_HEADER_SIZE + payloadLen, (BYTE*)plain);
            if (plainLen < 0)
            {
                printf("\n[!] MAC mismatch -> disconnect\n");
                InterlockedExchange(&g_intRunning, 0);
                return 0;
            }
            const char* payload = plain;
            WORD type;
            memcpy(&type, payload, 2);
            const char* body = payload + 2;
            switch (type)
            {
            case SC_LOGIN_RES:
            {
                BYTE result = (BYTE)body[0];
                int  userNo;
                memcpy(&userNo, body + 1, 4);
                wchar_t nick[MAX_NICKNAME_LEN + 1] = { 0 };
                memcpy(nick, body + 5, sizeof(wchar_t) * MAX_NICKNAME_LEN);
                wprintf(L"\n[<-] LOGIN_RES result=%u userNo=%d nick=%ls\n",
                        result, userNo, nick);
                break;
            }
            case SC_SIGNUP_RES:
            {
                BYTE result = (BYTE)body[0];
                int  userNo;
                memcpy(&userNo, body + 1, 4);
                printf("\n[<-] SIGNUP_RES result=%u userNo=%d\n", result, userNo);
                break;
            }
            case SC_ROOM_ENTER_RES:
            {
                WORD roomNo;
                memcpy(&roomNo, body + 1, 2);
                printf("\n[<-] ROOM_ENTER_RES result=%u room=%u\n", (BYTE)body[0], roomNo);
                break;
            }
            case SC_CHAT_RES:
            {
                char nickname[MAX_NICKNAME_LEN + 1]{};
                memcpy(nickname, body, MAX_NICKNAME_LEN);
                WORD chatLen;
                memcpy(&chatLen, body + MAX_NICKNAME_LEN, 2);
                char chat[MAX_CHAT_LEN + 1]{};
                int copyLen = chatLen < MAX_CHAT_LEN ? chatLen : MAX_CHAT_LEN;
                memcpy(chat, body + MAX_NICKNAME_LEN + 2, copyLen);
                printf("\n[CHAT] %s: %s\n", nickname, chat);
                break;
            }
            case SC_USER_JOIN:
            {
                char nickname[MAX_NICKNAME_LEN + 1]{};
                memcpy(nickname, body, MAX_NICKNAME_LEN);
                printf("\n[+] %s joined\n", nickname);
                break;
            }
            case SC_USER_LEAVE:
            {
                char nickname[MAX_NICKNAME_LEN + 1]{};
                memcpy(nickname, body, MAX_NICKNAME_LEN);
                printf("\n[-] %s left\n", nickname);
                break;
            }
            default:
                printf("\n[<-] Unknown type=%u\n", type);
                break;
            }
            int consumed = PKT_HEADER_SIZE + payloadLen;
            memmove(buf, buf + consumed, used - consumed);
            used -= consumed;
        }
    }
    return 0;
}

static void PrintHelp()
{
    printf("\n========== Commands ==========\n");
    printf("  /signup <account> <pw> <nick>  Sign up\n");
    printf("  /login  <account> <pw>         Login\n");
    printf("  /enter  <roomNo 1-5>           Enter room\n");
    printf("  /leave                         Leave room\n");
    printf("  /help                          Show this\n");
    printf("  /quit                          Exit\n");
    printf("  <text>                         Send chat\n");
    printf("==============================\n");
}

static void ToWide(const char* in, wchar_t* out, int outCount)
{
    MultiByteToWideChar(CP_ACP, 0, in, -1, out, outCount);
}

static int RunInteractive()
{
    g_intSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_serverPort);
    inet_pton(AF_INET, g_serverIp, &addr.sin_addr);
    printf("Connecting to %s:%d ...\n", g_serverIp, g_serverPort);
    if (connect(g_intSock, (SOCKADDR*)&addr, sizeof(addr)) != 0)
    {
        printf("connect failed: %d\n", WSAGetLastError());
        closesocket(g_intSock);
        return 1;
    }
    printf("Connected!\n");
    PrintHelp();

    HANDLE hRecv = (HANDLE)_beginthreadex(nullptr, 0, InteractiveRecvThread, nullptr, 0, nullptr);

    char line[1024];
    while (g_intRunning)
    {
        if (!fgets(line, sizeof(line), stdin)) break;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        if (strncmp(line, "/signup ", 8) == 0)
        {
            // /signup account pw nick
            char acc[64], pw[64], nk[64];
            if (sscanf_s(line + 8, "%63s %63s %63s", acc, (unsigned)_countof(acc), pw, (unsigned)_countof(pw), nk, (unsigned)_countof(nk)) == 3)
            {
                wchar_t wAcc[MAX_ACCOUNT_LEN], wPw[MAX_PASSWORD_LEN], wNk[MAX_NICKNAME_LEN];
                ToWide(acc, wAcc, MAX_ACCOUNT_LEN);
                ToWide(pw,  wPw,  MAX_PASSWORD_LEN);
                ToWide(nk,  wNk,  MAX_NICKNAME_LEN);
                SendSignUp(g_intSock, wAcc, wPw, wNk);
            }
            else printf("Usage: /signup <account> <pw> <nick>\n");
        }
        else if (strncmp(line, "/login ", 7) == 0)
        {
            char acc[64], pw[64];
            if (sscanf_s(line + 7, "%63s %63s", acc, (unsigned)_countof(acc), pw, (unsigned)_countof(pw)) == 2)
            {
                wchar_t wAcc[MAX_ACCOUNT_LEN], wPw[MAX_PASSWORD_LEN];
                ToWide(acc, wAcc, MAX_ACCOUNT_LEN);
                ToWide(pw,  wPw,  MAX_PASSWORD_LEN);
                SendLogin(g_intSock, wAcc, wPw);
            }
            else printf("Usage: /login <account> <pw>\n");
        }
        else if (strncmp(line, "/enter ", 7) == 0)
            SendRoomEnter(g_intSock, (WORD)atoi(line + 7));
        else if (strcmp(line, "/leave") == 0)
            SendRoomLeave(g_intSock);
        else if (strcmp(line, "/help") == 0)
            PrintHelp();
        else if (strcmp(line, "/quit") == 0)
            break;
        else if (line[0] == '/')
            printf("Unknown command. Type /help\n");
        else
            SendChat(g_intSock, line);
    }

    InterlockedExchange(&g_intRunning, 0);
    closesocket(g_intSock);
    WaitForSingleObject(hRecv, 2000);
    CloseHandle(hRecv);
    return 0;
}

// ===== Auto Mode =====

static int RunAuto(int n)
{
    printf("[AUTO] Spawning %d bots → %s:%d\n", n, g_serverIp, g_serverPort);
    printf("[AUTO] Press 'q' Enter to stop.\n\n");

    srand((unsigned)time(nullptr));
    HANDLE hStat = (HANDLE)_beginthreadex(nullptr, 0, StatThread, nullptr, 0, nullptr);

    HANDLE* hBots = new HANDLE[n];
    for (int i = 0; i < n; ++i)
        hBots[i] = (HANDLE)_beginthreadex(nullptr, 0, BotThread, (void*)(intptr_t)i, 0, nullptr);

    {
        char c;
        while (scanf_s("%c", &c, 1) != EOF)
            if (c == 'q' || c == 'Q') break;
    }

    printf("\n[AUTO] Stopping ...\n");
    InterlockedExchange(&g_botRunning, 0);

    int batchSize = 64;
    for (int off = 0; off < n; off += batchSize)
    {
        int cnt = (n - off < batchSize) ? (n - off) : batchSize;
        WaitForMultipleObjects(cnt, hBots + off, TRUE, 5000);
    }
    for (int i = 0; i < n; ++i) CloseHandle(hBots[i]);
    delete[] hBots;

    WaitForSingleObject(hStat, 2000);
    CloseHandle(hStat);

    printf("\n[AUTO] Final: ConnTotal=%ld Disc=%ld LoginOK=%ld LoginFail=%ld SignUps=%ld Tamper=%ld SendChat=%ld RecvChat=%ld RecvAny=%ld | Err sock=%ld conn=%ld cycle=%ld\n",
           g_connectTotal, g_disconnTotal,
           g_loginSuccess, g_loginFail, g_signupSent, g_tamperSent,
           g_sendChatTotal, g_recvChatTotal, g_recvAnyTotal,
           g_socketErr, g_connectErr, g_cycleErr);
    return 0;
}

// ===== Tamper Test (변조 검출 검증) =====

static int RunTamper()
{
    printf("[TAMPER] 변조 검출 테스트 -> %s:%d\n", g_serverIp, g_serverPort);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKADDR_IN addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)g_serverPort);
    inet_pton(AF_INET, g_serverIp, &addr.sin_addr);
    if (connect(s, (SOCKADDR*)&addr, sizeof(addr)) != 0)
    {
        printf("connect 실패: %d\n", WSAGetLastError());
        closesocket(s);
        return 1;
    }

    wchar_t acc[] = L"Tamper01", pw[] = L"pw", nick[] = L"Tamper01";
    char recvBuf[8192];
    int used = 0;

    SendSignUp(s, acc, pw, nick);  DrainRecv(s, recvBuf, used, sizeof(recvBuf), 150);
    SendLogin(s, acc, pw);         DrainRecv(s, recvBuf, used, sizeof(recvBuf), 150);
    SendRoomEnter(s, 1);           DrainRecv(s, recvBuf, used, sizeof(recvBuf), 150);

    // [1] 정상 패킷 — 서버가 정상 처리해야 함
    printf("[1] 정상 패킷 전송...\n");
    SendChat(s, "hello normal");
    DrainRecv(s, recvBuf, used, sizeof(recvBuf), 300, true);
    printf("    -> 위에 채팅/응답 보이면 정상 통신 OK\n");

    // [2] 변조 패킷 — 서버가 MAC 불일치로 끊어야 함
    printf("[2] 변조 패킷 전송 (cipher 1바이트 뒤집음)...\n");
    SendChatTampered(s, "hello tampered");

    // 서버가 CloseSession 하면 recv 가 0/에러. 3초 안에 반응 없으면 검출 실패로 간주.
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s, &rset);
    timeval tv{ 3, 0 };
    int r = select(0, &rset, nullptr, nullptr, &tv);
    if (r == 0)
    {
        printf("[FAIL] 3초간 서버 무반응 — 변조를 안 끊고 무시? (MAC 검출 의심)\n");
    }
    else
    {
        char rb[1024];
        int n = recv(s, rb, sizeof(rb), 0);
        if (n <= 0)
            printf("[OK] 서버가 연결을 끊음 (recv=%d) -> 변조 검출 성공. 서버 콘솔 CksumMismatch +1 확인\n", n);
        else
            printf("[FAIL] 서버가 안 끊고 %d바이트 응답 -> 변조가 안 잡힘\n", n);
    }

    closesocket(s);
    return 0;
}

// ===== main =====

int main(int argc, char* argv[])
{
    bool autoMode = false;
    bool tamperMode = false;
    int botCount = 0;

    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "--auto") == 0)
    {
        if (argi + 1 >= argc) { printf("Usage: --auto N [IP PORT]\n"); return 1; }
        autoMode = true;
        botCount = atoi(argv[argi + 1]);
        argi += 2;
    }
    else if (argi < argc && strcmp(argv[argi], "--tamper") == 0)
    {
        tamperMode = true;
        argi += 1;
    }
    if (argi < argc) g_serverIp = argv[argi++];
    if (argi < argc) g_serverPort = atoi(argv[argi++]);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 1; }

    int ret = tamperMode ? RunTamper() : (autoMode ? RunAuto(botCount) : RunInteractive());

    WSACleanup();
    return ret;
}
