#pragma once
#include <cstring>

class CRingBuffer
{
public:
	CRingBuffer();
	CRingBuffer(int iBuffSize);
	~CRingBuffer();

	void ReSize(int size);
	int GetBufferSize();

	int GetUseSize();
	int GetFreeSize();

	int DirectEnqueueSize();
	int DirectDequeueSize();

	int Enqueue(const char* chpData, int iSize);
	int Dequeue(char* chpDest, int iSize);

	int Peek(char* chpDest, int iSize);

	int MoveRear(int iSize);
	int MoveFront(int iSize);

	void ClearBuffer();

	char* GetBufferPtr();
	char* GetFrontBufferPtr();
	char* GetRearBufferPtr();

private:
	int iFrontPos;
	int iRearPos;
	int iBufSize;
	char* data;
};
