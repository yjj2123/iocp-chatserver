#include "CPlayer.h"

CPlayer::CPlayer()
{
	sid = 0;
	userNo = 0;
	nickname[0] = '\0';
	roomNo = INVALID_ROOM_NO;
	state = PS_NOT_LOGIN;
}

CPlayer::CPlayer(SessionID s)
{
	sid = s;
	userNo = 0;
	nickname[0] = '\0';
	roomNo = INVALID_ROOM_NO;
	state = PS_NOT_LOGIN;
}

SessionID CPlayer::GetSid()
{
	return sid;
}

int CPlayer::GetUserNo()
{
	return userNo;
}

const char* CPlayer::GetNickname()
{
	return nickname;
}

WORD CPlayer::GetRoomNo()
{
	return roomNo;
}

PlayerState CPlayer::GetState()
{
	return state;
}

void CPlayer::SetNickname(const char* name)
{
	strcpy_s(nickname, MAX_NICKNAME_LEN, name);
	
	state = PS_LOGGED_IN;
}

void CPlayer::SetUserNo(int no)
{
	userNo = no;
}

void CPlayer::SetLoggedIn()
{
	state = PS_LOGGED_IN;
}

bool CPlayer::EnterRoom(WORD no)
{
	if (state == PS_IN_ROOM) return false;
	else
	{
		roomNo = no;
		state = PS_IN_ROOM;
		return true;
	}
}

bool CPlayer::LeaveRoom()
{
	if (state != PS_IN_ROOM) return false;
	else
	{
		roomNo = INVALID_ROOM_NO;
		state = PS_LOGGED_IN;
		return true;
	}

}
