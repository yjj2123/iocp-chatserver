#pragma once
#include <unordered_map>
#include "CLanServer.h"
#include "CPlayer.h"
#define MAX_LIMIT_PERSON 50

class CRoom
{
private:
	WORD roomNo;
	std::unordered_map<SessionID, CPlayer*> users;
public:
	CRoom();
	CRoom(WORD no);
	bool EnterRoom(SessionID sid, CPlayer* player);
	bool LeaveRoom(SessionID sid);
	const std::unordered_map<SessionID, CPlayer*>& GetUsers ()const;
	int GetUserCount();

};
