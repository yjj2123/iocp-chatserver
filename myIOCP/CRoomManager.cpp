#include "CRoomManager.h"

CRoomManager::CRoomManager()
{
	for (int i = 0; i < MAX_ROOM_COUNT; ++i)
	{
		rooms[i] = new CRoom(i + 1);
	}
}

CRoomManager::~CRoomManager()
{
	for (int i = 0; i < MAX_ROOM_COUNT; ++i)
	{
		delete rooms[i];
	}
}

CRoom* CRoomManager::GetRoom(WORD No)
{
	if (0 < No && No <= MAX_ROOM_COUNT)
	{
		return rooms[No - 1];
	}
	
	return nullptr;
}

int CRoomManager::GetRoomCount()
{
	return MAX_ROOM_COUNT;
}

int CRoomManager::GetActiveRoomcount()
{
	int count = 0;
	for (int i = 0; i < MAX_ROOM_COUNT; ++i)
	{
		if (rooms[i]->GetUserCount() > 0)
			count++;
	}

	return count;
}
