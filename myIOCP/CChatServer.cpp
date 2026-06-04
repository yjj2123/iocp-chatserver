#include "CChatServer.h"

unsigned CChatServer::UpdateThread(void* arg)
{
	CChatServer* server = (CChatServer*)arg;
	server->UpdateLoop();
	return 0;
}

CChatServer::CChatServer()
{
	roomManager = new CRoomManager;
	h_updateThread = nullptr;
	isRun = false;

	loginOkTotal    = 0;
	loginFailTotal  = 0;
	signupOkTotal   = 0;
	signupFailTotal = 0;
	lastStatTick    = 0;
	joinCnt = 0;
	leaveCnt = 0;
	handleLeaveCnt = 0;
}

void CChatServer::UpdateLoop()
{
	Message msg;
	DBResult dbr;
	lastStatTick = GetTickCount();
	const DWORD STAT_INTERVAL_MS = 5000;

	while (isRun)
	{
		bool didWork = false;

		if (contentQ.Pop(&msg))
		{
			didWork = true;
			switch (msg.type)
			{
			case MSG_JOIN:  Handle_Join(msg.sid);  break;
			case MSG_LEAVE: Handle_Leave(msg.sid); break;
			case MSG_RECV:
				Handle_Recv(msg.sid, msg.pkt);
				msg.pkt->Release();
				break;
			}
		}

		while (dbWorker.PopResult(&dbr))
		{
			didWork = true;
			Handle_DbResult(dbr);
		}

		DWORD now = GetTickCount();
		if (now - lastStatTick >= STAT_INTERVAL_MS)
		{
			PrintChatStat();
			lastStatTick = now;
		}

		if (!didWork)
			Sleep(1);
	}
}

void CChatServer::PrintChatStat()
{
	static LONG lastJoin = 0, lastLeave = 0, lastHL = 0;
	LONG j = joinCnt, l = leaveCnt, h = handleLeaveCnt;
	LONG dj = j - lastJoin, dl = l - lastLeave, dh = h - lastHL;
	lastJoin = j; lastLeave = l; lastHL = h;

	int roomCounts[MAX_ROOM_COUNT + 1] = { 0 };   
	int activeRooms = 0;
	int totalInRoom = 0;
	for (WORD r = 1; r <= MAX_ROOM_COUNT; ++r)
	{
		CRoom* room = roomManager->GetRoom(r);
		if (room)
		{
			int c = room->GetUserCount();
			roomCounts[r] = c;
			totalInRoom += c;
			if (c > 0) ++activeRooms;
		}
	}

	int playerCount = (int)players.size();
	int dbJobSize   = dbWorker.GetJobSize();
	int dbResSize   = dbWorker.GetResultSize();
	int contentQSize= contentQ.GetSize();

	printf("[DBG] Join=%ld(+%ld) Leave=%ld(+%ld) HL=%ld(+%ld) Alive=%ld\n",
		j, dj, l, dl, h, dh, j - l);
	printf("... Alive=%ld PlayerMap=%zu\n", j - l, players.size());
}

void CChatServer::OnClientJoin(SessionID sid)
{
	InterlockedIncrement(&joinCnt);

	Message msg;
	msg.sid = sid;
	msg.pkt = nullptr;
	msg.type = MSG_JOIN;
	contentQ.Push(msg);
}
void CChatServer::OnClientLeave(SessionID sid)
{
	InterlockedIncrement(&leaveCnt);

	Message msg;
	msg.sid = sid;
	msg.pkt = nullptr;
	msg.type = MSG_LEAVE;
	contentQ.Push(msg);
}
void CChatServer::OnRecv(SessionID sid, CPacket* packet)
{
	packet->AddRef();
	Message msg;
	msg.sid = sid;
	msg.pkt = packet;
	msg.type = MSG_RECV;
	contentQ.Push(msg);
}
bool CChatServer::OnConnectionRequest(const char* ip, int port)
{
	return true;
}

CChatServer::~CChatServer()
{
	delete roomManager;
}

void CChatServer::StartContent()
{
	
	if (!dbWorker.Start(L"Driver={ODBC Driver 17 for SQL Server};"
	                    L"Server=localhost\\SQLEXPRESS;"
	                    L"Database=ChatServer;"
	                    L"Trusted_Connection=yes;"))
	{
		printf("[ChatServer] dbWorker 시작 실패\n");
		
	}

	isRun = true;
	h_updateThread = (HANDLE)_beginthreadex(nullptr, 0, UpdateThread, this, 0, nullptr);
}

void CChatServer::StopContent()
{
	isRun = false;
	WaitForSingleObject(h_updateThread, INFINITE);
	CloseHandle(h_updateThread);
	h_updateThread = nullptr;

	dbWorker.Stop();
}

void CChatServer::Handle_Join(SessionID sid)
{
	if (players.find(sid) == players.end())
	{
		CPlayer* player = new CPlayer(sid);
		players[sid] = player;
	}
}

void CChatServer::Handle_Leave(SessionID sid)
{
	InterlockedIncrement(&handleLeaveCnt);

	auto it = players.find(sid);
	if (it == players.end()) return;

	if (it->second->GetState() == PS_IN_ROOM)
	{
		CRoom* room = roomManager->GetRoom(it->second->GetRoomNo());
		room->LeaveRoom(sid);
	}

	CPlayer* temp = it->second;
	players.erase(sid);
	delete temp;

}

void CChatServer::Handle_Recv(SessionID sid, CPacket* pkt)
{
	WORD type;
	*pkt >> type;
	switch (type)
	{
	case CS_LOGIN_REQ:      Handle_Login(sid, pkt); break;
	case CS_SIGNUP_REQ:     Handle_SignUp(sid, pkt); break;   
	case CS_ROOM_ENTER_REQ: HandleRoomEnter(sid, pkt); break;
	case CS_ROOM_LEAVE_REQ: HandleRoomLeave(sid, pkt); break;
	case CS_CHAT_REQ:       HandleChat(sid, pkt); break;
	}
}

void CChatServer::Handle_Login(SessionID sid, CPacket* pkt)
{
	
	if (players.find(sid) == players.end()) return;

	DBJob job;
	job.type = DB_JOB_LOGIN;
	job.sid  = sid;

	pkt->ReadPacket((char*)job.account,  sizeof(wchar_t) * MAX_ACCOUNT_LEN);
	pkt->ReadPacket((char*)job.password, sizeof(wchar_t) * MAX_PASSWORD_LEN);

	job.account[MAX_ACCOUNT_LEN - 1]   = L'\0';
	job.password[MAX_PASSWORD_LEN - 1] = L'\0';

	dbWorker.PushJob(job);
}

void CChatServer::Handle_SignUp(SessionID sid, CPacket* pkt)
{
	if (players.find(sid) == players.end()) return;

	DBJob job;
	job.type = DB_JOB_SIGNUP;
	job.sid  = sid;

	pkt->ReadPacket((char*)job.account,  sizeof(wchar_t) * MAX_ACCOUNT_LEN);
	pkt->ReadPacket((char*)job.password, sizeof(wchar_t) * MAX_PASSWORD_LEN);
	pkt->ReadPacket((char*)job.nickname, sizeof(wchar_t) * MAX_NICKNAME_LEN);

	job.account[MAX_ACCOUNT_LEN - 1]   = L'\0';
	job.password[MAX_PASSWORD_LEN - 1] = L'\0';
	job.nickname[MAX_NICKNAME_LEN - 1] = L'\0';

	dbWorker.PushJob(job);
}

void CChatServer::Handle_DbResult(const DBResult& r)
{
	auto it = players.find(r.sid);
	if (it == players.end())
	{
		
		return;
	}

	if (r.type == DB_JOB_LOGIN)
	{
		ResultCode rc = MapLoginResult(r.result);

		if (rc == RESULT_SUCCESS) ++loginOkTotal;
		else                       ++loginFailTotal;

		if (rc == RESULT_SUCCESS)
		{
			
			char nick[MAX_NICKNAME_LEN] = { 0 };
			WideCharToMultiByte(CP_ACP, 0, r.nickname, -1,
			                    nick, MAX_NICKNAME_LEN, NULL, NULL);
			nick[MAX_NICKNAME_LEN - 1] = '\0';

			it->second->SetUserNo(r.userNo);
			it->second->SetNickname(nick);   
		}

		CPacket* res = CPacket::Alloc();
		*res << (WORD)SC_LOGIN_RES;
		*res << (BYTE)rc;
		*res << (int)r.userNo;
		res->WritePacket((char*)r.nickname, sizeof(wchar_t) * MAX_NICKNAME_LEN);
		res->SetHeader();
		SendPacket(r.sid, res);
		res->Release();
	}
	else if (r.type == DB_JOB_SIGNUP)
	{
		ResultCode rc = MapSignUpResult(r.result);

		if (rc == RESULT_SUCCESS) ++signupOkTotal;
		else                       ++signupFailTotal;

		CPacket* res = CPacket::Alloc();
		*res << (WORD)SC_SIGNUP_RES;
		*res << (BYTE)rc;
		*res << (int)r.userNo;
		res->SetHeader();
		SendPacket(r.sid, res);
		res->Release();
	}
}

void CChatServer::HandleRoomEnter(SessionID sid, CPacket* pkt)
{
	WORD no;
	*pkt >> no;
	auto it = players.find(sid);
	if (it == players.end()) return;
	CRoom* room = roomManager->GetRoom(no);
	if (room == nullptr) return;
	if(!(room->EnterRoom(sid, it->second))) return;
	if (!(it->second->EnterRoom(no))) return;
	CPacket* res = CPacket::Alloc();
	*res << (WORD)SC_ROOM_ENTER_RES;
	*res << (BYTE)RESULT_SUCCESS;
	*res << (WORD)no;
	res->SetHeader();
	SendPacket(sid, res);
	res->Release();
	CPacket* notice = CPacket::Alloc();
	*notice << (WORD)SC_USER_JOIN;
	notice->WritePacket(it->second->GetNickname(), MAX_NICKNAME_LEN);
	notice->SetHeader();
	for (const auto& pair : room->GetUsers())
	{
		if (pair.first == sid) continue;
		else
		{
			SendPacket(pair.first, notice);
		}
	}

	notice->Release();
}

void CChatServer::HandleRoomLeave(SessionID sid, CPacket* pkt)
{
	auto it = players.find(sid);
	if (it == players.end()) return;
	if (it->second->GetState() != PS_IN_ROOM) return;
	CRoom* room = roomManager->GetRoom(it->second->GetRoomNo());
	room->LeaveRoom(sid);
	it->second->LeaveRoom();

	CPacket* res = CPacket::Alloc();
	*res << (WORD)SC_USER_LEAVE;
	res->WritePacket(it->second->GetNickname(), MAX_NICKNAME_LEN);
	res->SetHeader();
	for (const auto& pair : room->GetUsers())
		SendPacket(pair.first, res);
	res->Release();
}

void CChatServer::HandleChat(SessionID sid, CPacket* pkt)
{
	WORD chatLen;
	*pkt >> chatLen;
	if (chatLen > MAX_CHAT_LEN) return;
	char chat[MAX_CHAT_LEN];
	pkt->ReadPacket(chat, chatLen);
	auto it = players.find(sid);
	if (it == players.end()) return;
	if (it->second->GetState() != PS_IN_ROOM) return;
	CRoom* room = roomManager->GetRoom(it->second->GetRoomNo());
	CPacket* res = CPacket::Alloc();
	*res << (WORD)SC_CHAT_RES;
	res->WritePacket(it->second->GetNickname(), MAX_NICKNAME_LEN);
	*res << chatLen;
	res->WritePacket(chat, chatLen);
	res->SetHeader();
	for (const auto& pair : room->GetUsers())
		SendPacket(pair.first, res);
	res->Release();
}
