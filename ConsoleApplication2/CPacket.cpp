#include "stdafx.h"
#include "CPacket.h"

CLFFreeList<CPacket> CPacket::g_PacketPool;

CPacket::CPacket()
{
    m_iMaxSize = eBUFFER_DEFAULT;
    m_iFront = 0;
    m_iRear = 0;
    m_iUsingSize = 0;
    m_lRefCount = 0;
    m_cpHeadPtr = (char*)malloc(m_iMaxSize);
    ZeroMemory(m_cpHeadPtr, m_iMaxSize);
    m_cpPayloadBuffer = m_cpHeadPtr + eBUFFER_HEADER_SIZE;
    m_bIsEncoded = FALSE;
}

CPacket::~CPacket()
{
    Release();
}

void CPacket::Release()
{
    if (m_cpHeadPtr)
    {
        free(m_cpHeadPtr);
        m_cpHeadPtr = nullptr;
        m_cpPayloadBuffer = nullptr;
    }
}

void CPacket::Clear()
{
    m_iFront = 0;
    m_iRear = 0;
    m_iUsingSize = 0;
    m_bIsEncoded = FALSE;

    if (m_cpHeadPtr)
        ZeroMemory(m_cpHeadPtr, m_iMaxSize);
}

void CPacket::ClearPayload()
{
    if (m_cpPayloadBuffer)
        ZeroMemory(m_cpPayloadBuffer, 0, (size_t)PayloadCapacity());
}

CPacket* CPacket::Alloc()
{
    CPacket* msg = g_PacketPool.Alloc();
    msg->Clear();
    InterlockedExchange(&msg->m_lRefCount, 1);

    return msg;
}

void CPacket::AddRef()
{
    InterlockedIncrement(&m_lRefCount);
}

void CPacket::SubRef()
{
    if (InterlockedDecrement(&m_lRefCount) == 0)
        g_PacketPool.Free(this);
}

int CPacket::MoveWritePos(int size)
{
    if (size <= 0) 
        return 0;

    int canWrite = GetFreeSize();
    int move = (size > canWrite) ? canWrite : size;
    m_iRear += move;
    m_iUsingSize += move;

    return move;
}

int CPacket::MoveReadPos(int size)
{
    if (size <= 0) 
        return 0;

    int canRead = GetDataSize();
    int move = (size > canRead) ? canRead : size;
    m_iFront += move;
    m_iUsingSize -= move;

    if (m_iFront == m_iRear)
    {
        m_iFront = 0;
        m_iRear = 0;
    }

    return move;
}

void CPacket::IncreaseBufferSize(int addSize)
{
    if (addSize <= 0)
        return;

    int payloadcap = PayloadCapacity();
    int size = m_iUsingSize + addSize;

    if (payloadcap >= size)
        return;

    int newSize = payloadcap;
    while (payloadcap < size)
        newSize = (newSize <= 0) ? size : (newSize * 2);

    int newTotal = eBUFFER_HEADER_SIZE + newSize;

    char* newBuffer = (char*)std::malloc((size_t)newTotal);
    if (!newBuffer)
        return;

    std::memset(newBuffer, 0, (size_t)newTotal);
    std::memcpy(newBuffer, m_cpHeadPtr, (size_t)m_iMaxSize);

    std::free(m_cpHeadPtr);
    m_cpHeadPtr = newBuffer;
    m_cpPayloadBuffer = m_cpHeadPtr + eBUFFER_HEADER_SIZE;
    m_iMaxSize = newTotal;
}

void CPacket::PutData(const char* data, int size)
{
    if (GetFreeSize() < size)
        IncreaseBufferSize(size);

    memcpy(m_cpPayloadBuffer + m_iRear, data, size);
    m_iRear += size;
    m_iUsingSize += size;
}

void CPacket::GetData(char* data, int size)
{
    if (GetDataSize() < size)
    {
        //에러처리는 고민좀...
        return;
    }

    memcpy(data, m_cpPayloadBuffer + m_iFront, size);
    m_iFront += size;
    m_iUsingSize -= size;

    if (m_iFront == m_iRear)
    {
        m_iFront = 0;
        m_iRear = 0;
    }
}

void CPacket::SetMessageHeader(const char* header, int len)
{
    memcpy(m_cpHeadPtr, header, len);
}

void CPacket::SetEncodingCode()
{
    if (m_bIsEncoded) 
        return;

    st_PACKET_HEADER header;
    header.code = PACKET_CODE;
    header.len = (WORD)GetDataSize();
    header.randKey = rand() % 256;

    char* pPayload = GetBufferPtr();
    LONG checkSum = 0;
    for (int i = 0; i < header.len; ++i)
        checkSum += pPayload[i];

    header.checkSum = (BYTE)(checkSum % 256);
    SetMessageHeader((char*)&header, sizeof(header));

    BYTE E_n = 0, P_n = 0;
    for (int i = 0; i < header.len; ++i)
    {
        E_n = pPayload[i] ^ (E_n + header.randKey + i + 1);
        pPayload[i] = E_n ^ (P_n + FIX_KEY + i + 1);
        P_n = pPayload[i];
    }

    m_bIsEncoded = TRUE;
}

BOOL CPacket::SetDecodingCode()
{
    st_PACKET_HEADER* packetHeader = (st_PACKET_HEADER*)m_cpHeadPtr;
    if (packetHeader->code != PACKET_CODE)
        return FALSE;

    BYTE randKey = packetHeader->randKey;
    int dataSize = GetDataSize();
    char* pPayload = GetBufferPtr();

    BYTE P_n = 0, P_before = 0, E_n = 0, D_n = 0;

    for (int i = 0; i < dataSize; ++i)
    {
        P_n = pPayload[i] ^ (E_n + FIX_KEY + i + 1);
        E_n = pPayload[i];
        D_n = P_n ^ (P_before + randKey + i + 1);
        P_before = P_n;
        pPayload[i] = D_n;
    }

    DWORD iCheck = 0;
    for (int i = 0; i < dataSize; ++i)
        iCheck += pPayload[i];

    BYTE cCheck = (BYTE)(iCheck % 256);

    return (packetHeader->checkSum == cCheck);
}
