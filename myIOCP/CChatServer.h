#pragma once

#include "CLanServer.h"
#include "CPlayer.h"
#include "CRoomManager.h"
#include "CContentQueue.h"
#include "CDBWorker.h"      

class CChatServer : public CLanServer
{
private:
	CRoomManager* roomManager;
	std::unordered_map<SessionID, CPlayer*> players;
	CContentQueue contentQ;
	CDBWorker     dbWorker;                  
	HANDLE h_updateThread;
	volatile bool isRun;

	LONG loginOkTotal;
	LONG loginFailTotal;
	LONG signupOkTotal;
	LONG signupFailTotal;
	LONG joinCnt; 
	LONG leaveCnt; 
	LONG handleLeaveCnt;
	DWORD lastStatTick;
	void PrintChatStat();
	static unsigned UpdateThread(void* arg);
	void UpdateLoop();
	void Handle_Join(SessionID sid);
	void Handle_Leave(SessionID sid);
	void Handle_Recv(SessionID sid, CPacket* pkt);
	void Handle_Login(SessionID sid, CPacket* pkt);
	void Handle_SignUp(SessionID sid, CPacket* pkt);    
	void Handle_DbResult(const DBResult& r);            
	void HandleRoomEnter(SessionID sid, CPacket* pkt);
	void HandleRoomLeave(SessionID sid, CPacket* pkt);
	void HandleChat(SessionID sid, CPacket* pkt);

protected:
	void OnClientJoin(SessionID sid) override;
	void OnClientLeave(SessionID sid) override;
	void OnRecv(SessionID sid, CPacket* packet) override;
	bool OnConnectionRequest(const char* ip, int port) override;

public:
	CChatServer();
	~CChatServer();
	void StartContent();
	void StopContent();
};
