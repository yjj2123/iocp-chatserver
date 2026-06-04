#include "CContentQueue.h"

CContentQueue::CContentQueue()
{
	InitializeCriticalSection(&csMsg);
}

CContentQueue::~CContentQueue()
{
	DeleteCriticalSection(&csMsg);
}

void CContentQueue::Push(const Message& in)
{
	EnterCriticalSection(&csMsg);
	msg.push(in);
	LeaveCriticalSection(&csMsg);
}

bool CContentQueue::Pop(Message* out)
{
	EnterCriticalSection(&csMsg);
	if (!msg.empty())
	{
		*out = msg.front();
		msg.pop();
		LeaveCriticalSection(&csMsg);
		return true;
	}
	else
	{
		LeaveCriticalSection(&csMsg);
		return false;
	}
}

int CContentQueue::GetSize()
{
	return msg.size();
}
