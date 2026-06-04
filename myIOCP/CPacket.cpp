#include "CPacket.h"

CLockFreeStack<CPacket*> CPacket::pool;

CPacket::CPacket() : readPos(PKT_HEADER_SIZE), writePos(PKT_HEADER_SIZE), refCount(0)
{

}

CPacket* CPacket::Alloc()
{
    CPacket* pkt;
    if (pool.Pop(&pkt))
    {
        pkt->Clear();
        pkt->refCount = 1;
        return pkt;
    }
    else
    {
        pkt = new CPacket;
        pkt->refCount = 1;
        return pkt;
    }
}

void CPacket::ReadPacket(char* dest, int len)
{
    if (readPos + len <= writePos)
    {
        memcpy(dest, &m_pBuffer[readPos], len);
        readPos += len;
    }
}

void CPacket::WritePacket(const char* src, int len)
{
    if (writePos + len <= PACKET_BUFFER_SIZE)
    {
        memcpy(&m_pBuffer[writePos], src, len);
        writePos += len;
   }
}

void CPacket::AddRef()
{
    InterlockedIncrement(&refCount);
}

void CPacket::Release()
{
    if (InterlockedDecrement(&refCount) == 0)
    {
        pool.Push(this);
    }
}

void CPacket::Clear()
{
    readPos = PKT_HEADER_SIZE;
    writePos = PKT_HEADER_SIZE;
}

int CPacket::GetDataSize()
{
    return writePos - readPos;
}

char* CPacket::GetBufferPtr()
{
    return m_pBuffer;
}

int CPacket::GetPacketSize()
{
    return writePos;
}

void CPacket::AttachRaw(const char* plainBuf, int plainLen)
{
    if (plainLen > PACKET_BUFFER_SIZE - PKT_HEADER_SIZE) return;
    memcpy(m_pBuffer + PKT_HEADER_SIZE, plainBuf, plainLen);
    readPos = PKT_HEADER_SIZE;
    writePos = PKT_HEADER_SIZE + plainLen;
}

void CPacket::SetHeader()
{
    int plainLen = writePos - PKT_HEADER_SIZE;
    Encode((const BYTE*)(m_pBuffer + PKT_HEADER_SIZE), plainLen, (BYTE*)m_pBuffer);
}

int CPacket::Encode(const BYTE* plain, int plainLen, BYTE* out)
{
    RAND_bytes(out + PKT_NONCE_OFFSET, PKT_NONCE_SIZE);

    BYTE iv[16] = { 0 };
    memcpy(iv, out + PKT_NONCE_OFFSET, PKT_NONCE_SIZE);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outLen = 0;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, PKT_AES_KEY, iv);
    EVP_EncryptUpdate(ctx, out + PKT_HEADER_SIZE, &outLen, plain, plainLen);
    EVP_CIPHER_CTX_free(ctx);

    *(WORD*)out = (WORD)plainLen;

    BYTE mac[PKT_MAC_FULL_SIZE];
    unsigned int macLen = 0;

    HMAC_CTX* hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, PKT_MAC_KEY, 32, EVP_sha256(), nullptr);
    HMAC_Update(hctx, out, PKT_MAC_OFFSET);
    HMAC_Update(hctx, out + PKT_HEADER_SIZE, plainLen);
    HMAC_Final(hctx, mac, &macLen);
    HMAC_CTX_free(hctx);
    memcpy(out + PKT_MAC_OFFSET, mac, PKT_MAC_SIZE);

    return PKT_HEADER_SIZE + plainLen;
}

int CPacket::Decode(const BYTE* in, int inLen, BYTE* outPlain)
{
    WORD len = *(WORD*)in;
    const BYTE* nonce = in + PKT_NONCE_OFFSET;
    const BYTE* recvMac = in + PKT_MAC_OFFSET;
    const BYTE* cipher = in + PKT_HEADER_SIZE;

    BYTE calcMac[PKT_MAC_FULL_SIZE];
    unsigned int macLen = 0;

    HMAC_CTX* hctx = HMAC_CTX_new();
    HMAC_Init_ex(hctx, PKT_MAC_KEY, 32, EVP_sha256(), nullptr);
    HMAC_Update(hctx, in, PKT_MAC_OFFSET);
    HMAC_Update(hctx, in + PKT_HEADER_SIZE, len);
    HMAC_Final(hctx, calcMac, &macLen);
    HMAC_CTX_free(hctx);

    if (memcmp(recvMac, calcMac, PKT_MAC_SIZE) != 0) return -1;

    BYTE iv[16] = { 0 };
    memcpy(iv, nonce, PKT_NONCE_SIZE);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int outLen = 0;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, PKT_AES_KEY, iv);
    EVP_DecryptUpdate(ctx, outPlain, &outLen, cipher, len);
    EVP_CIPHER_CTX_free(ctx);

    return len;
}
