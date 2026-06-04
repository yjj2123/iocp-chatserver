#pragma once
#include "CRoom.h"

class CRoomManager
{
private:
	CRoom* rooms[MAX_ROOM_COUNT];
public:
	CRoomManager();
	~CRoomManager();
	CRoom* GetRoom(WORD No);
	int GetRoomCount();
	int GetActiveRoomcount();
};
