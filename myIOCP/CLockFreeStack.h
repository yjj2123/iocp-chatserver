#pragma once
#include <Windows.h>

template <typename T>
struct Node
{
    T data;
    Node<T>* next;
};

template <typename T>
class CLockFreeStack
{
    UINT64 m_pTop;

    Node<T>* GetPtr(UINT64 combined)
    {
        return (Node<T>*)(combined & 0x00007FFFFFFFFFFF);
    }
    UINT64 GetStamp(UINT64 combined)
    {
        return combined >> 47;
    }
    UINT64 Combine(UINT64 stamp, Node<T>* ptr)
    {
        return (stamp << 47) | (UINT64)ptr;
    }

public:
    CLockFreeStack() : m_pTop(0)
    {
    }
    void Push(T data)
    {
        Node<T>* newNode = new Node<T>;
        newNode->data = data;
        UINT64 old;
        UINT64 newVal;
        do
        {
            old = m_pTop;
            Node<T>* oldPtr = GetPtr(old);
            newNode->next = oldPtr;
            newVal = Combine(GetStamp(old) + 1, newNode);
        } while (InterlockedCompareExchange64((LONG64*)&m_pTop, newVal, old) != old);
    }
    bool Pop(T* out)
    {
        Node<T>* temp;
        UINT64 old;
        UINT64 newVal;
        do
        {
            old = m_pTop;
            Node<T>* oldPtr = GetPtr(old);
            if (oldPtr == nullptr) return false;
            temp = oldPtr;
            newVal = Combine(GetStamp(old) + 1, oldPtr->next);
        } while (InterlockedCompareExchange64((LONG64*)&m_pTop, newVal, old) != old);
        *out = temp->data;
        delete temp;
        return true;
    }
};