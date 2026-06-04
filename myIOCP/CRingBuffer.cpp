#include "CRingBuffer.h"

CRingBuffer::CRingBuffer() : maxSize(RINGBUFFER_SIZE), rear(0), front(0)
{
}

int CRingBuffer::UseSize()
{
    return front <= rear ? rear - front : maxSize - front + rear;
}

int CRingBuffer::FreeSize()
{
    return maxSize - 1 - UseSize();
}

char* CRingBuffer::GetWritePtr()
{
    return &buffer[rear];
}

int CRingBuffer::DirectEnqueueSize()
{
    if (front <= rear)
    {
        return front == 0 ? maxSize - 1 - rear : maxSize - rear;
    }
    else
    {
        return front - rear - 1;
    }
}

void CRingBuffer::MoveWritePos(int size)
{
    rear = (rear + size) % maxSize;
}

char* CRingBuffer::GetReadPtr()
{
    return &buffer[front];
}

int CRingBuffer::DirectDequeueSize()
{
    return front <= rear ? rear - front : maxSize - front;
}

void CRingBuffer::MoveReadPos(int size)
{
    front = (front + size) % maxSize;
}

void CRingBuffer::Clear()
{
    rear = 0;
    front = 0;
}

int CRingBuffer::Peek(char* dest, int size)
{
    if (UseSize() < size) return 0;

    int direct = DirectDequeueSize();

    if (direct >= size)
    {
        memcpy(dest, &buffer[front], size);
    }
    else
    {
        memcpy(dest, &buffer[front], direct);
        memcpy(dest + direct, &buffer[0], size - direct);
    }

    return size;
}

int CRingBuffer::Read(char* dest, int size)
{
    int result = Peek(dest, size);
    if (result > 0)
        MoveReadPos(size);
    
    return result;
}
