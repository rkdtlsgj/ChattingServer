#pragma once
#include "stdafx.h"
#include <boost/lockfree/queue.hpp>
#include "CPacket.h"
#include "CRingBuffer.h"
#include <list>
#include <stack>
#include <vector>
#include <mstcpip.h>
#include <Windows.h>

#define COMBINE_ID_INDEX(ID,INDEX) ((ID << 48) | INDEX)
#define GET_SESSION_ID(ID) (ID >> 48)
#define GET_SESSION_INDEX(ID)  (ID & 0xffff)

struct Session
{
	SOCKET sock;
	UINT64 sessionID;
	INT64 index;

	SOCKADDR_IN clienr_addr;

	LONG isSendFlag;
	LONG sendCount;
	LONG isReleased;
	LONG ioCount;


	boost::lockfree::queue<CPacket*, boost::lockfree::capacity<4096>> sendBuf;
	boost::lockfree::queue<CPacket*, boost::lockfree::capacity<4096>> completeSendBuf;
	CRingBuffer recvBuf;
	std::list<CPacket*> recvPacketList;


	OVERLAPPED sendOverlap;
	OVERLAPPED recvOverlap;

	UINT64 lastRecvTime;


	Session()
	{
		Init();
	}

	void Init()
	{
		sock = INVALID_SOCKET;
		sessionID = -1;
		index = 0;

		ZeroMemory(&clienr_addr, sizeof(clienr_addr));
		ZeroMemory(&sendOverlap, sizeof(sendOverlap));
		ZeroMemory(&recvOverlap, sizeof(recvOverlap));

		isSendFlag = false;
		sendCount = 0;

		lastRecvTime = 0;

		recvPacketList.clear();

		CPacket* packet = nullptr;
		while (sendBuf.pop(packet))
		{
			packet->SubRef();
		}

		while (completeSendBuf.pop(packet))
		{
			packet->SubRef();
		}

		isReleased = FALSE;
		ioCount = 0;
	}
};

class IOCPServer
{
private:
	SOCKET listenSocket;
	BOOL nagle;
	HANDLE hcp;

	int maxClientCount;

	HANDLE acceptThread;
	HANDLE monitorThread;

	int allThreadCount;
	std::vector<HANDLE> threadVector;

	SOCKADDR_IN serrver_addr;
	Session* sessionArray;

	std::stack<int> indexStack;
	std::mutex indexMutex;
	UINT64 sessionID;

	LONG64 sessionCount;
	LONG64 sendTPS;
	LONG64 recvTPS;
	LONG64 acceptTPS;
	LONG64 acceptCount;

	BOOL running;

private:
	Session* CreateSession(SOCKET socket);
	void ReleaseSession(Session* sessionInfo);

	bool PostSend(Session* session);
	bool PostRecv(Session* session);

	void RecvComplete(Session* session, DWORD transferred);
	void SendComplete(Session* session, DWORD transferred);

	static unsigned int WINAPI WorkerThread(LPVOID arg);
	static unsigned int WINAPI AcceptThread(LPVOID arg);
	static unsigned int WINAPI MonitorThread(LPVOID arg);

	void PushIndex(INT64 index);
	UINT64 PopIndex();

	

	Session* SessionLock(UINT64 sessionID);
	void SessionUnLock(Session* session);	

protected:
	void Stop();

	virtual void OnClientJoin(SOCKADDR_IN* connectInfo, UINT64 sessionID) = 0;
	virtual void OnClientLeave(UINT64 sessionID) = 0;
	virtual bool OnConnectionRequest(char* ip, int port) = 0;

	virtual void OnRecv(UINT64 sessionID, CPacket* packet) = 0;
	virtual void OnSend(UINT64 sessionID, int sendsize) = 0;

	//virtual void OnWorkerThreadBegin() = 0;
	//virtual void OnWorkerThreadEnd() = 0;

	virtual void OnError(int errorcode, WCHAR* buf) = 0;

	void SendPacket(UINT64 id, CPacket* packet);

	void DisConnect(Session* session);
	void Disconnect(UINT64 sessionID);
public:
	BOOL Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount);
};