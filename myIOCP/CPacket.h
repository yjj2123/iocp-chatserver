#pragma once
#define OPENSSL_SUPPRESS_DEPRECATED
#include <Windows.h>
#include "CLockFreeStack.h"
#include <random>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#define PACKET_BUFFER_SIZE 4096
#define PKT_NONCE_OFFSET 2
#define PKT_NONCE_SIZE 12
#define PKT_MAC_OFFSET 14
#define PKT_MAC_SIZE 16
#define PKT_MAC_FULL_SIZE 32
#define PKT_HEADER_SIZE 30

static const BYTE PKT_AES_KEY[16] = {
    0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static const BYTE PKT_MAC_KEY[32] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
    0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
};

class CPacket
{
private:
    char m_pBuffer[PACKET_BUFFER_SIZE]{};
    int readPos;
    int writePos;
    volatile LONG refCount;
    static CLockFreeStack<CPacket*> pool;

public:
    CPacket();
    static CPacket* Alloc();
    void ReadPacket(char* dest, int len);
    void WritePacket(const char* src, int len);
    void AddRef();
    void Release();
    void Clear();
    int GetDataSize();
    char* GetBufferPtr();
    int GetPacketSize();
    void AttachRaw(const char* rawBuf, int totalLen);
    void SetHeader();
    static int Encode(const BYTE* plain, int plainLen, BYTE* out);
    static int Decode(const BYTE* in, int inLen, BYTE* outPlain);

    template <typename T>
    CPacket& operator<<(T value)
    {
        if (writePos + sizeof(T) > PACKET_BUFFER_SIZE) return *this;
        memcpy(&m_pBuffer[writePos], &value, sizeof(T));
        writePos += sizeof(T);
        return *this;
    }

    template <typename T>
    CPacket& operator>>(T& value)
    {
        if (readPos + sizeof(T) > writePos) return *this;
        memcpy(&value, &m_pBuffer[readPos], sizeof(T));
        readPos += sizeof(T);
        return *this;
    }
};
