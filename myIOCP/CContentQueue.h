#pragma once
#include <queue>
#include "CLanServer.h"
#include "CPacket.h"

enum MessageType : BYTE
{
	MSG_INIT = 0,
	MSG_JOIN = 1,
	MSG_LEAVE = 2,
	MSG_RECV = 3
};

struct Message
{
	MessageType type = MSG_INIT;
	SessionID sid = 0;
	CPacket* pkt = nullptr;
};

class CContentQueue
{
private:
	std::queue<Message> msg;
	CRITICAL_SECTION csMsg;
public:
	CContentQueue();
	~CContentQueue();
	void Push(const Message& in);
	bool Pop(Message* out);
	int GetSize();

};
