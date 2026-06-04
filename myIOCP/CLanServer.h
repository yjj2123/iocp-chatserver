#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <process.h>
#include <stdio.h>
#include "CPacket.h"
#include "CRingBuffer.h"

#pragma comment (lib, "ws2_32.lib")
#define Port_Number 9000
#define MAX_SESSION 10000
#define Qsize 512
#define Bit_Masking 0x0000FFFFFFFFFFFF
using SessionID = unsigned long long;
enum ioType { RECV, SEND };

struct IOContext
{
	OVERLAPPED ov;
	ioType type;
};

struct Session
{
	SOCKET sock = INVALID_SOCKET;
	SessionID sID = 0;
	volatile LONG generation = 0;
	volatile LONG closeFlag = 0;

	CRingBuffer recvBuffer;
	IOContext recvCtx{};

	CRITICAL_SECTION sendLock{};
	volatile LONG sendPending = 0;
	CPacket* sendQ[Qsize]{};
	int sqHead = 0;
	int sqTail = 0;
	int sqCount = 0;
	WSABUF sendWsaBuf[Qsize]{};
	CPacket* sendPkts[Qsize]{};
	int sentCount = 0;
	IOContext sendCtx{};

	volatile LONG ioRefCount = 0;
};
class CLanServer
{
private:
	Session* sessions = nullptr;
	CLockFreeStack<int> indexPool;
	SOCKET listenSock = INVALID_SOCKET;
	HANDLE hIOCP = nullptr;

	HANDLE* workerHandles = nullptr;
	int workerCount = 0;
	HANDLE acceptHandle = nullptr;
	HANDLE statHandle = nullptr;

	bool nagle = false;
	int maxSession = 0;
	bool isrun = false;

	volatile LONG acceptCount = 0;
	volatile LONG recvCount = 0;
	volatile LONG sendCount = 0;
	volatile LONG totalAccept = 0;
	volatile LONG totalDisconn = 0;
	volatile LONG checksumMismatchCount = 0;

	static unsigned AcceptThread(void* arg);
	static unsigned WorkerThread(void* arg);
	static unsigned StatThread(void* arg);
					    
	Session* AllocSession(SOCKET sock);
	void InitSession(Session* s, SOCKET sock);
	void CloseSession(int idx);
	void ReleaseSession(int idx);

	void PostRecv(Session* s);
	void OnRecvComplete(Session* s, int idx, DWORD bytes);
	void FlushSend(Session* s);
	void OnSendComplete(Session* s);

	void IOAddRef(Session* s);
	void IORelease(Session* s, int idx);

public:
	bool Start(const char* IP,int portNum, DWORD workNum, bool nagle, int maxSession );
	void Stop();
	bool SendPacket(SessionID sid, CPacket* packet);
	bool Disconnect(SessionID sid);
	int GetSessionCount();
protected:
	virtual void OnClientJoin(SessionID sid) =0;
	virtual void OnClientLeave(SessionID sid) = 0;
	virtual void OnRecv(SessionID sid, CPacket *packet) = 0;
	virtual bool OnConnectionRequest(const char* ip, int port) = 0;
};
