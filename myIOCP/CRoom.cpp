#include "CRoom.h"

CRoom::CRoom()
{
	roomNo = INVALID_ROOM_NO;
}

CRoom::CRoom(WORD no)
{
	roomNo = no;
}

bool CRoom::EnterRoom(SessionID sid, CPlayer* player)
{
	
	if (users.size() < MAX_LIMIT_PERSON)
	{
		if (users.find(sid) != users.end())
			return false;
	}
	else
	{
		return false;
	}

	users.insert({ sid, player });
	return true;
}

bool CRoom::LeaveRoom(SessionID sid)
{
	if (!users.erase(sid))
	{
		return false;
	}
	return true;
}

const std::unordered_map<SessionID, CPlayer*>& CRoom::GetUsers() const
{
	return users;
}

int CRoom::GetUserCount()
{
	return users.size();
}
