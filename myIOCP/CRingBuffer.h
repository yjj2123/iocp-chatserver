#pragma once
#include <Windows.h>

#define RINGBUFFER_SIZE 4096

class CRingBuffer
{
private:
    int maxSize;
    int rear;
    int front;
    char buffer[RINGBUFFER_SIZE]{};

public:
    CRingBuffer();
    int UseSize();
    int FreeSize();
    char* GetWritePtr();
    int DirectEnqueueSize();
    void MoveWritePos(int size);
    char* GetReadPtr();
    int DirectDequeueSize();
    void MoveReadPos(int size);
    void Clear();
    int Peek(char* dest, int size);
    int Read(char* dest, int size);
};