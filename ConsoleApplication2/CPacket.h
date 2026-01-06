#pragma once
#include "stdafx.h"
#include "CLFFreeList.h"

#define FIX_KEY           0x32
#define PACKET_CODE       0x77
#define MESSAGE_HEADER_LEN 5

#pragma pack(push, 1)
struct st_PACKET_HEADER
{
    BYTE code;
    WORD len;
    BYTE randKey;
    BYTE checkSum;
};
#pragma pack(pop)

class CPacket
{
public:
    enum en_PACKET
    {
        eBUFFER_DEFAULT = 512,
        eBUFFER_HEADER_SIZE = MESSAGE_HEADER_LEN
    };

private:
    int m_iMaxSize;
    int m_iFront;
    int m_iRear;
    int m_iUsingSize;
    LONG m_lRefCount;

    char* m_cpHeadPtr;
    char* m_cpPayloadBuffer;

    BOOL m_bIsEncoded;

public:
    static CLFFreeList<CPacket> g_PacketPool;

private:
    template<typename T> void WriteValue(const T& val);
    template<typename T> void ReadValue(T& val);

    int  PayloadCapacity() const { return m_iMaxSize - eBUFFER_HEADER_SIZE; }
    void IncreaseBufferSize(int addSize);

public:
    CPacket();
    virtual ~CPacket();

    void AddRef();
    void SubRef();
    static CPacket* Alloc();

    void Release();
    void Clear();
    void ClearPayload();

    int  GetBufferSize() const { return m_iMaxSize; }
    int  GetDataSize() const { return m_iUsingSize; }
    int  GetFreeSize() const { return m_iMaxSize - m_iUsingSize; }

    char* GetBufferPtr() { return m_cpPayloadBuffer; }    
    char* GetHeaderPtr() { return m_cpHeadPtr; }

    int  GetFront() const { return m_iFront; }
    int  GetRear() const { return m_iRear; }

    int  MoveWritePos(int size);
    int  MoveReadPos(int size);    

    void PutData(const char* data, int size);
    void GetData(char* data, int size);

    void SetMessageHeader(const char* header, int len);

    void SetEncodingCode();
    BOOL SetDecodingCode();

    CPacket& operator<<(BYTE v) { WriteValue(v); return *this; }
    CPacket& operator<<(char v) { WriteValue(v); return *this; }
    CPacket& operator<<(WCHAR v) { WriteValue(v); return *this; }
    CPacket& operator<<(short v) { WriteValue(v); return *this; }
    CPacket& operator<<(WORD v) { WriteValue(v); return *this; }
    CPacket& operator<<(int v) { WriteValue(v); return *this; }
    CPacket& operator<<(DWORD v) { WriteValue(v); return *this; }
    CPacket& operator<<(float v) { WriteValue(v); return *this; }
    CPacket& operator<<(long v) { WriteValue(v); return *this; }
    CPacket& operator<<(__int64 v) { WriteValue(v); return *this; }
    CPacket& operator<<(double v) { WriteValue(v); return *this; }

    CPacket& operator>>(BYTE& v) { ReadValue(v); return *this; }
    CPacket& operator>>(char& v) { ReadValue(v); return *this; }
    CPacket& operator>>(WCHAR& v) { ReadValue(v); return *this; }
    CPacket& operator>>(short& v) { ReadValue(v); return *this; }
    CPacket& operator>>(WORD& v) { ReadValue(v); return *this; }
    CPacket& operator>>(int& v) { ReadValue(v); return *this; }
    CPacket& operator>>(DWORD& v) { ReadValue(v); return *this; }
    CPacket& operator>>(float& v) { ReadValue(v); return *this; }
    CPacket& operator>>(long& v) { ReadValue(v); return *this; }
    CPacket& operator>>(__int64& v) { ReadValue(v); return *this; }
    CPacket& operator>>(double& v) { ReadValue(v); return *this; }    

};

template<typename T>
void CPacket::WriteValue(const T& val)
{
    int need = (int)sizeof(T);
    if (GetFreeSize() < need)
        IncreaseBufferSize(need * 2);

    memcpy(m_cpPayloadBuffer + m_iRear, &val, need);
    m_iRear += need;
    m_iUsingSize += need;
}

template<typename T>
void CPacket::ReadValue(T& val)
{
    int need = (int)sizeof(T);
    if (GetDataSize() < need)
        return;

    memcpy(&val, m_cpPayloadBuffer + m_iFront, need);
    m_iFront += need;
    m_iUsingSize -= need;

    if (m_iFront == m_iRear)
    {
        m_iFront = 0;
        m_iRear = 0;
    }
}