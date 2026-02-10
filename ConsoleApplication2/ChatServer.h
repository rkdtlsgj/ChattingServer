#pragma once
#define WIN32_LEAN_AND_MEAN
#include "IOCPServer.h"
#include "Protocol.h"
#include "CpuMonitor.h"
#include <unordered_map>
#include <unordered_set>

#include <cpp_redis/cpp_redis>
#include <tacopie/tacopie>




struct st_POSITION
{
	int x;
	int y;
};

struct st_SECTOR
{
	int count;
	st_POSITION map[9];
};

struct st_MSG
{
	BYTE type;
	UINT64 session;
	CPacket* packet;
};

struct st_Redis
{
	UINT64 session;
	CPacket* packet;
};



enum en_MSG_TYPE
{
	en_MSG_RECV,
	en_MSG_JOIN,
	en_MSG_LEAVE,
	en_MSG_HEART,
	en_MSG_LOGIN_OK,
	en_MSG_LOGIN_FAIL
};


struct Player
{
	std::atomic<long> ref{ 1 };
	std::atomic<bool> closing{ false };

	UINT64 sessionID;
	INT64  accountNo;

	WCHAR id[20];
	WCHAR nick[20];
	char  sessionKey[dfSESSIONKEY_LEN];

	WORD sectorX;
	WORD sectorY;

	LONG isLogined;

	std::atomic<UINT64> lastRecvTime{ 0 };

	void Reset()
	{
		sessionID = 0;
		accountNo = 0;
		lastRecvTime = 0;


		sectorX = -1;
		sectorY = -1;
		isLogined = FALSE;

		ZeroMemory(id, sizeof(id));
		ZeroMemory(nick, sizeof(nick));
		ZeroMemory(sessionKey, sizeof(sessionKey));

		lastRecvTime.store(0, std::memory_order_relaxed);
		ref.store(1, std::memory_order_relaxed);
		closing.store(false, std::memory_order_relaxed);
	}



	void SetLogin(UINT64 sid, INT64 account, const WCHAR* wid, const WCHAR* wNick, const char* key)
	{
		sessionID = sid;
		accountNo = account;

		memcpy(id, wid, dfID_LEN);
		memcpy(nick, wNick, dfNiCK_LEN);
		memcpy(sessionKey, key, dfSESSIONKEY_LEN);

		isLogined = TRUE;
	}

	void SetSector(WORD x, WORD y)
	{
		sectorX = x;
		sectorY = y;
	}
};

struct st_SECTOR_NODE
{
	int lockIndex = 0;
	SRWLOCK lock{};
	std::unordered_set<Player*> set;
};

class ChatServer : public IOCPServer 
{
public:
	BOOL Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount);
	ChatServer();
	~ChatServer();

	//BOOL CreatePlayer(UINT64 sessionID);
	//void DeletePlayer(UINT64 sessionID);
	//void ErasePlayer(UINT64 sessionID);
	//Player* FindPlayer(UINT64 sessionID);
	//void DeletePlayerSector(Player* session);

	Player* AcquirePlayer(UINT64 sessionID);
	void    AddRefPlayer(Player* player);
	void    ReleasePlayer(Player* player);



protected:
	virtual void OnClientJoin(SOCKADDR_IN* connectInfo, UINT64 sessionID);
	virtual void OnClientLeave(UINT64 sessionID);
	virtual bool OnConnectionRequest(char* ip, int port);
	virtual void OnRecv(UINT64 sessionID, CPacket* packet);
	virtual void OnSend(UINT64 sessionID, int sendsize);
	virtual void OnError(int errorcode, WCHAR* buf);

private:
	bool IsValidSector(WORD x, WORD y);
	st_SECTOR MakeAround(WORD x, WORD y);

	void LockSectorExclusive(WORD x, WORD y);
	void UnlockSectorExclusive(WORD x, WORD y);
	void LockTwoSectorsExclusive(WORD x1, WORD y1, WORD x2, WORD y2);
	void UnlockTwoSectorsExclusive(WORD x1, WORD y1, WORD x2, WORD y2);
	
	BOOL Req_SectorMove(UINT64 sessionID, Player* player, CPacket* packet);
	BOOL Req_Chat(UINT64 sessionID, Player* player, CPacket* packet);	


	CPacket* Res_Login(INT64 account, BYTE status);
	CPacket* Res_SectorMove(INT64 account, WORD x, WORD y);
	CPacket* Res_Chat(Player* player, WCHAR* msg, WORD len);

	void SendUnicast(UINT64 sessionID, CPacket* packet);

	static unsigned int WINAPI RedisThread(LPVOID arg);
	static unsigned int WINAPI HeartbeatThread(LPVOID arg);
	static unsigned int WINAPI MonitorThread(LPVOID arg);

	void RedisThread();
	void HeartbeatThread();
	void MonitorThread();

	void EnqueueRedis(UINT64 sessionID, CPacket* packet);

	BOOL Req_Login_Redis(UINT64 sessionID, Player* player, CPacket* packet);
	BOOL CompleteLogin(UINT64 sessionID, CPacket* resultPacket, bool success);
	
	BOOL CheckTimeOut();

	CPacket* MakeLoginResult(INT64 account, const WCHAR* id, const WCHAR* nick);


	bool JoinProc(UINT64 sessionID);
	bool LeaveProc(UINT64 sessionID);
	bool RecvProc(UINT64 sessionID, CPacket* packet);

private:
	BOOL exit;

	CLFFreeList<Player> playerPool;
	//CLFFreeList<st_MSG> msgPool;

	std::unordered_map<UINT64, Player*> players;
	std::unordered_map<INT64, UINT64> accounts;


	st_SECTOR_NODE sector[dfSECTOR_MAX_Y][dfSECTOR_MAX_X];


	HANDLE heartbeat_thread;

	HANDLE monitor_thread;

	HANDLE redis_event;
	HANDLE redis_thread;
	
	//boost::lockfree::queue<st_MSG*> msgQ{ 200000 };
	boost::lockfree::queue<st_Redis, boost::lockfree::capacity<65534>> redisQ;
	std::unique_ptr<cpp_redis::client> redis;	

	CpuMonitor monitor;

	SRWLOCK playerLock{};
	SRWLOCK accountLock{};


};