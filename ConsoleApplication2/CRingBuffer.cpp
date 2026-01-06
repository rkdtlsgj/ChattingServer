#pragma once
#include "CRingBuffer.h"
#include "CPacket.h"
#include <algorithm>
#define BUF_SIZE 65536

CRingBuffer::CRingBuffer()
{
	iFrontPos = 0;
	iRearPos = 0;
	iBufSize = BUF_SIZE;
	data = new char[BUF_SIZE];

	ZeroMemory(data, iBufSize);
}

CRingBuffer::CRingBuffer(int iBuffSize)
{
	iFrontPos = 0;
	iRearPos = 0;
	iBufSize = iBuffSize;
	data = new char[iBuffSize];

	ZeroMemory(data, iBufSize);
}

CRingBuffer::~CRingBuffer()
{
	delete[] data;
}

void CRingBuffer::ReSize(int size)
{
	if (size <= 0) return;
	char* nd = new char[size];
	int use = GetUseSize();
	
	if (iFrontPos <= iRearPos) 
	{
		memcpy(nd, data + iFrontPos, use);
	}
	else 
	{
		int first = iBufSize - iFrontPos;
		memcpy(nd, data + iFrontPos, first);
		memcpy(nd + first, data, iRearPos);
	}

	delete[] data;
	data = nd;
	iBufSize = size;
	iFrontPos = 0;
	iRearPos = use % iBufSize;
}

int CRingBuffer::GetBufferSize()
{
	return iBufSize;
}

int CRingBuffer::GetUseSize()
{
	if (iRearPos >= iFrontPos)
	{
		return iRearPos - iFrontPos;
	}
	else
	{
		return iBufSize - iFrontPos + iRearPos;
	}
}

int CRingBuffer::GetFreeSize()
{
	return iBufSize - 1 - GetUseSize();
}

int CRingBuffer::DirectEnqueueSize()
{
	if (iFrontPos > iRearPos)
	{
		return iFrontPos - iRearPos - 1;
	}
	else
	{
		return iBufSize - iRearPos - (iFrontPos == 0 ? 1 : 0);
	}
}

int CRingBuffer::DirectDequeueSize()
{
	if (iFrontPos <= iRearPos)
	{
		return iRearPos - iFrontPos;
	}
	else
	{
		return iBufSize - iFrontPos;
	}
}

int CRingBuffer::Enqueue(const char* chpData, int iSize)
{
	int writeSize;

	//남아있는 용량이 들어오는 용량보다 적다면
	if (GetFreeSize() < iSize)
	{
		writeSize = GetFreeSize();
	}
	else
	{
		writeSize = iSize;
	}

	if (iRearPos + writeSize > iBufSize) 
	{
		int first = iBufSize - iRearPos;         // 뒤쪽 조각
		// destsz는 실제 남은 공간 크기를 넣는다
		memcpy_s(&data[iRearPos], iBufSize - iRearPos, chpData, first);
		memcpy_s(&data[0], iBufSize, chpData + first, writeSize - first);
	}
	else 
	{
		memcpy_s(&data[iRearPos], iBufSize - iRearPos, chpData, writeSize);
	}

	iRearPos = (iRearPos + writeSize) % iBufSize;

	return writeSize;

}

int CRingBuffer::Dequeue(char* chpDest, int iSize)
{
	int readSize;


	if (GetUseSize() < iSize)
	{
		readSize = GetUseSize();
	}
	else
	{
		readSize = iSize;
	}

	if (iFrontPos + readSize > iBufSize)
	{
		int size = iBufSize - iFrontPos;
		memcpy_s(chpDest, iSize, &data[iFrontPos], size);
		memcpy_s(chpDest + size, iSize - size, &data[0], readSize - size);
	}
	else
	{
		memcpy_s(chpDest, iSize, &data[iFrontPos], readSize);
	}

	iFrontPos = (iFrontPos + readSize) % iBufSize;

	return readSize;
}

int CRingBuffer::Peek(char* chpDest, int iSize)
{
	int readSize;


	if (GetUseSize() < iSize)
	{
		readSize = GetUseSize();
	}
	else
	{
		readSize = iSize;
	}

	if (iFrontPos + readSize > iBufSize)
	{
		int size = iBufSize - iFrontPos;
		memcpy_s(chpDest, iSize, &data[iFrontPos], size);
		memcpy_s(chpDest + size, iSize - size, &data[0], readSize - size);
	}
	else
	{
		memcpy_s(chpDest, iSize, &data[iFrontPos], readSize);
	}

	return readSize;
}

int CRingBuffer::MoveFront(int iSize)
{
	int can = GetUseSize();
	if (iSize > can) iSize = can;
	iFrontPos = (iFrontPos + iSize) % iBufSize;
	return iFrontPos;
}


int CRingBuffer::MoveRear(int iSize)
{
	int can = GetFreeSize();
	if (iSize > can) iSize = can;
	iRearPos = (iRearPos + iSize) % iBufSize;
	return iRearPos;
}

void CRingBuffer::ClearBuffer()
{
	iFrontPos = 0;
	iRearPos = 0;
}

char* CRingBuffer::GetFrontBufferPtr()
{
	return data + iFrontPos;
}

char* CRingBuffer::GetRearBufferPtr()
{
	return &data[iRearPos];
}

char* CRingBuffer::GetBufferPtr()
{
	return data;
}

