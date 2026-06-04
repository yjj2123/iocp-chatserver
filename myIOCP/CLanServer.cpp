#include "CLanServer.h"

bool CLanServer::Start(const char* IP, int portNum, DWORD workNum, bool nagle, int maxSession)
{
	this->nagle = nagle;
	this->maxSession = maxSession;
	isrun = true;
	sessions = new Session[maxSession];
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

	listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	SOCKADDR_IN sAddr{};
	sAddr.sin_family = AF_INET;
	sAddr.sin_port = htons((u_short)portNum);
	
	if ((IP == nullptr) || (IP[0] == '\0'))
		sAddr.sin_addr.s_addr = INADDR_ANY;
	else
		inet_pton(AF_INET, IP, &sAddr.sin_addr);

	if (bind(listenSock, (SOCKADDR*)&sAddr, sizeof(sAddr)) != 0)
		return false;
	if (listen(listenSock, SOMAXCONN_HINT(5000)) != 0)
		return false;

	hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

	for (int i = 0; i < maxSession; ++i)
	{
		sessions[i].sock = INVALID_SOCKET;
		sessions[i].ioRefCount = 0;
		sessions[i].sendPending = 0;
		sessions[i].sqTail = 0;
		sessions[i].sqHead = 0;
		sessions[i].sqCount = 0;
		sessions[i].sentCount = 0;
		sessions[i].generation = 0;
		sessions[i].closeFlag = 0;
		InitializeCriticalSection(&sessions[i].sendLock);
	}

	for (int i = 0; i < maxSession; i++)
		indexPool.Push(i);

	workerCount = workNum;
	workerHandles = new HANDLE[workerCount];

	for (int i = 0; i < workerCount; i++)
	{
		workerHandles[i] = (HANDLE)_beginthreadex(nullptr, 0, WorkerThread, this, 0, nullptr);
	}

	acceptHandle = (HANDLE)_beginthreadex(nullptr, 0, AcceptThread, this, 0, nullptr);
	statHandle = (HANDLE)_beginthreadex(nullptr, 0, StatThread, this, 0, nullptr);

	return true;
}

void CLanServer::Stop()
{
	isrun = false;
	closesocket(listenSock);
	listenSock = INVALID_SOCKET;

	WaitForSingleObject(acceptHandle, INFINITE);
	WaitForSingleObject(statHandle, INFINITE);
	CloseHandle(acceptHandle);
	CloseHandle(statHandle);

	for (int i = 0; i < maxSession; ++i)
	{
		CloseSession(i);
	}
	for (int i = 0; i < workerCount; i++)
	{
		PostQueuedCompletionStatus(hIOCP, 0, 0, nullptr);
	}
	WaitForMultipleObjects(workerCount, workerHandles, TRUE, INFINITE);
	for (int i = 0; i < workerCount; i++)
		CloseHandle(workerHandles[i]);
	delete[] workerHandles;
	workerHandles = nullptr;

	CloseHandle(hIOCP);
	hIOCP = nullptr;

	for (int i = 0; i < maxSession; i++)
		DeleteCriticalSection(&sessions[i].sendLock);

	delete[] sessions;
	sessions = nullptr;
	WSACleanup();

}

bool CLanServer::SendPacket(SessionID sid, CPacket* packet)
{
	int idx = (int)(sid >> 48);
	if (idx < 0 || idx >= maxSession)
		return false;

	LONG gen = (LONG)(sid & Bit_Masking);
	Session* s = &sessions[idx];

	if (s->generation != gen)
		return false;

	if (s->closeFlag != 0)
		return false;

	EnterCriticalSection(&s->sendLock);
	if (s->sqCount >= Qsize)
	{
		LeaveCriticalSection(&s->sendLock);
		CloseSession(idx);
		return false;
	}
	packet->AddRef();
	s->sendQ[s->sqTail] = packet;
	s->sqTail = (s->sqTail + 1) % Qsize;
	s->sqCount++;
	InterlockedIncrement(&sendCount);
	FlushSend(s);
	LeaveCriticalSection(&s->sendLock);
	return true;
}

bool CLanServer::Disconnect(SessionID sid)
{
	int idx = (int)(sid >> 48);
	LONG gen = (LONG)(sid & Bit_Masking);
	if (idx < 0 || idx >= maxSession)
	{
		return false;
	}
	Session* s = &sessions[idx];

	if (s->generation != gen)
	{
		return false;
	}
	else
	{
		CloseSession(idx);
	}

	return true;
}

int CLanServer::GetSessionCount()
{
	return totalAccept - totalDisconn;
}

unsigned CLanServer::AcceptThread(void* arg)
{
	CLanServer* srv = (CLanServer*)arg;

	while (srv->isrun)
	{
		SOCKADDR_IN cAddr{};
		int cAddrSize = sizeof(cAddr);
		SOCKET clientSock = accept(srv->listenSock, (SOCKADDR*)&cAddr, &cAddrSize);

		if (clientSock == INVALID_SOCKET) break;

		char ip[INET_ADDRSTRLEN]{};
		inet_ntop(AF_INET, &cAddr.sin_addr, ip, sizeof(ip));
		int port = ntohs(cAddr.sin_port);

		if (!srv->OnConnectionRequest(ip, port))
		{
			closesocket(clientSock);
			continue;
		}

		if (!srv->nagle)
		{
			BOOL noDelay = TRUE;
			setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay));
		}

		Session* sess = srv->AllocSession(clientSock);
		if (sess == nullptr)
		{
			closesocket(clientSock);
			continue;
		}
		int idx = (int)(sess - srv->sessions);
		HANDLE h = CreateIoCompletionPort((HANDLE)clientSock, srv->hIOCP, (ULONG_PTR)(idx + 1), 0);
		if (h != srv->hIOCP)
		{
			srv->CloseSession(idx);
			continue;
		}
		InterlockedIncrement(&srv->totalAccept);
		InterlockedIncrement(&srv->acceptCount);
		srv->OnClientJoin(sess->sID);
		srv->PostRecv(sess);
	}	
	return 0;
}

unsigned CLanServer::WorkerThread(void* arg)
{
	CLanServer* srv = (CLanServer*)arg;
	while (1)
	{
		DWORD bytes = 0;
		ULONG_PTR key = 0;
		OVERLAPPED* pOv = nullptr;
		BOOL ok = GetQueuedCompletionStatus(srv->hIOCP,&bytes ,&key,&pOv, INFINITE );

		if (pOv == nullptr) break;
		int idx = (int)(key - 1);
		Session* sess = &srv->sessions[idx];
		if (ok == false || bytes == 0)
		{
			srv->CloseSession(idx);
			srv->IORelease(sess, idx);
		}
		else
		{
			IOContext* ctx = CONTAINING_RECORD(pOv,IOContext, ov);
			if (ctx->type == RECV)
				srv->OnRecvComplete(sess,idx,bytes);
			else if (ctx->type == SEND)
				srv->OnSendComplete(sess);
			srv->IORelease(sess, idx);
		}

	}
	return 0;
}

unsigned CLanServer::StatThread(void* arg)
{
	CLanServer* srv = (CLanServer*)arg;

	while (srv->isrun)
	{
		Sleep(1000);    

		int aTPS = (int)InterlockedExchange(&srv->acceptCount, 0);
		int rTPS = (int)InterlockedExchange(&srv->recvCount, 0);
		int sTPS = (int)InterlockedExchange(&srv->sendCount, 0);

		printf("[STAT] Sessions=%d  AcceptTotal=%ld  DisconnTotal=%ld  "
			"AcceptTPS=%d  RecvTPS=%d  SendTPS=%d  CksumMismatch=%ld\n",
			srv->GetSessionCount(),
			srv->totalAccept,
			srv->totalDisconn,
			aTPS, rTPS, sTPS,
			srv->checksumMismatchCount);
	}
	return 0;
}
Session* CLanServer::AllocSession(SOCKET sock)
{
	int idx;
	if (!indexPool.Pop(&idx))
		return nullptr;

	Session* s = &sessions[idx];
	InitSession(s, sock);
	return s;

}

void CLanServer::InitSession(Session* s, SOCKET sock)
{
	s->sock = sock;

	int idx = (int)(s - sessions);
	InterlockedIncrement(&s->generation);
	s->sID = ((SessionID)idx << 48) | (SessionID)s->generation;
	s->recvBuffer.Clear();
	memset(&s->recvCtx, 0, sizeof(IOContext));
	s->recvCtx.type = RECV;
	memset(&s->sendCtx, 0, sizeof(IOContext));
	s->sendCtx.type = SEND;

	s->sqHead = 0;
	s->sqTail = 0;
	s->sqCount = 0;
	s->sentCount = 0;
	s->sendPending = 0;

	s->ioRefCount = 1;
	s->closeFlag = 0;
}

void CLanServer::CloseSession(int idx)
{
	if (InterlockedExchange(&sessions[idx].closeFlag, 1) != 0) return;

	CancelIoEx((HANDLE)sessions[idx].sock, nullptr);
	closesocket(sessions[idx].sock);
	sessions[idx].sock = INVALID_SOCKET;

	EnterCriticalSection(&sessions[idx].sendLock);
	while (sessions[idx].sqCount > 0)
	{
		CPacket* pkt = sessions[idx].sendQ[sessions[idx].sqHead];
		sessions[idx].sqHead = (sessions[idx].sqHead + 1) % Qsize;
		sessions[idx].sqCount--;
		pkt->Release();
	}
	LeaveCriticalSection(&sessions[idx].sendLock);

	OnClientLeave(sessions[idx].sID);
	InterlockedIncrement(&totalDisconn);

	IORelease(&sessions[idx], idx); 
}

void CLanServer::ReleaseSession(int idx)
{
	indexPool.Push(idx);
}

void CLanServer::PostRecv(Session* s)
{
	WSABUF wBuf;
	wBuf.buf = s->recvBuffer.GetWritePtr();
	wBuf.len = s->recvBuffer.DirectEnqueueSize();
	DWORD flags = 0;
	IOAddRef(s);
	int ret = WSARecv(s->sock, &wBuf, 1, nullptr, &flags, &s->recvCtx.ov, nullptr);

	if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		int idx = (int)(s - sessions);
		CloseSession(idx);
		IORelease(s, idx);
	}
}

void CLanServer::OnRecvComplete(Session* s, int idx, DWORD bytes)
{
	s->recvBuffer.MoveWritePos(bytes);

	while (1)
	{
		if (s->recvBuffer.UseSize() < PKT_HEADER_SIZE) break;
		char header[PKT_HEADER_SIZE];
		s->recvBuffer.Peek(header, PKT_HEADER_SIZE);
		WORD len = *(WORD*)header;

		if (len > PACKET_BUFFER_SIZE - PKT_HEADER_SIZE)
		{
			CloseSession(idx);
			return;
		}

		if (s->recvBuffer.UseSize() < PKT_HEADER_SIZE + len) break;

		char buf[PACKET_BUFFER_SIZE];
		s->recvBuffer.Read(buf, PKT_HEADER_SIZE + len);
		CPacket* pkt = CPacket::Alloc();
		char plain[PACKET_BUFFER_SIZE];
		int plainLen = CPacket::Decode((const BYTE*)buf, PKT_HEADER_SIZE + len, (BYTE*)plain);
		if (plainLen < 0){
			InterlockedIncrement(&checksumMismatchCount);
			pkt->Release();
			CloseSession(idx);
			return;
		}
		pkt->AttachRaw(plain, plainLen);
		OnRecv(s->sID, pkt);
		InterlockedIncrement(&recvCount);
		pkt->Release();
	}

	if (s->recvBuffer.DirectEnqueueSize() == 0)
	{
		CloseSession(idx);
		return;
	}
	PostRecv(s);
}

void CLanServer::FlushSend(Session* s)
{
	if (InterlockedExchange(&s->sendPending, 1) != 0) return;

	EnterCriticalSection(&s->sendLock);
	int count = 0;
	while (s->sqCount > 0 && count < Qsize)
	{
		CPacket* pkt = s->sendQ[s->sqHead];
		s->sqHead = (s->sqHead + 1) % Qsize;
		s->sqCount--;
		s->sendPkts[count] = pkt;
		s->sendWsaBuf[count].buf = pkt->GetBufferPtr();
		s->sendWsaBuf[count].len = pkt->GetPacketSize();
		count++;
	}
	s->sentCount = count;
	LeaveCriticalSection(&s->sendLock);

	IOAddRef(s);
	int ret = WSASend(s->sock, s->sendWsaBuf, s->sentCount,
		nullptr, 0, &s->sendCtx.ov, nullptr);
	if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
	{
		for (int i = 0; i < s->sentCount; ++i)
		{
			s->sendPkts[i]->Release();
			s->sendPkts[i] = nullptr;
		}
		s->sentCount = 0;
		InterlockedExchange(&s->sendPending, 0);
		int idx = (int)(s - sessions);
		CloseSession(idx);
		IORelease(s, idx);
	}
}

void CLanServer::OnSendComplete(Session* s)
{
	for (int i = 0; i < s->sentCount; ++i)
	{
		s->sendPkts[i]->Release();
		s->sendPkts[i] = nullptr;
	}
	s->sentCount = 0;
	InterlockedExchange(&s->sendPending, 0);
	if (s->sqCount > 0) FlushSend(s);
}

void CLanServer::IOAddRef(Session* s)
{
	InterlockedIncrement(&s->ioRefCount);
}

void CLanServer::IORelease(Session* s, int idx)
{
	LONG count = InterlockedDecrement(&s->ioRefCount);
	if (count == 0)
		ReleaseSession(idx);
}
