#pragma once
#include <cstringt.h>
#include "CLanServer.h"
#include "Protocol.h"

enum PlayerState : BYTE
{
	PS_NOT_LOGIN = 1,
	PS_LOGGED_IN = 2,
	PS_IN_ROOM = 3
};

class CPlayer
{
private:
	SessionID sid;
	int userNo;                          
	char nickname[MAX_NICKNAME_LEN];
	WORD roomNo;
	PlayerState state;
public:
	CPlayer();
	CPlayer(SessionID s);
	SessionID GetSid();
	int GetUserNo();
	const char* GetNickname();
	WORD GetRoomNo();
	PlayerState GetState();
	void SetNickname(const char* name);
	void SetUserNo(int no);
	void SetLoggedIn();                  
	bool EnterRoom(WORD no);
	bool LeaveRoom();
};
