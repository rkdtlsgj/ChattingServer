#pragma once
#define WIN32_LEAN_AND_MEAN
#include "IOCPServer.h"
#include "Protocol.h"
#include <unordered_map>
#include <unordered_set>
#include <cpp_redis/cpp_redis>


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
	UINT64 sessionID;
	INT64  accountNo;

	WCHAR id[20];
	WCHAR nick[20];
	char  sessionKey[dfSESSIONKEY_LEN];

	WORD sectorX;
	WORD sectorY;

	LONG isLogined;

	INT64 lastRecvTime;

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

class ChatServer : public IOCPServer 
{
public:
	BOOL Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount);
	ChatServer();
	~ChatServer();

	BOOL CreatePlayer(UINT64 sessionID);
	void DeletePlayer(UINT64 sessionID);
	void ErasePlayer(UINT64 sessionID);
	Player* FindPlayer(UINT64 sessionID);

	void DeletePlayerSector(Player* session);

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

	BOOL Req_Login(UINT64 sessionID, CPacket* packet);
	BOOL Req_SectorMove(UINT64 sessionID, CPacket* packet);
	BOOL Req_Chat(UINT64 sessionID, CPacket* packet);
	BOOL Heartbeat(UINT64 sessionID);


	CPacket* Res_Login(INT64 account, BYTE status);
	CPacket* Res_SectorMove(INT64 account, WORD x, WORD y);
	CPacket* Res_Chat(Player* player, WCHAR* msg, WORD len);

	void SendUnicast(UINT64 sessionID, CPacket* packet);
	void SendPacket_SectorOne(WORD x, WORD y, CPacket* packet, Player* player);
	void SendPacket_Around(Player* player, CPacket* packet, bool bSendMe = false);	

	BOOL PacketProcess(UINT64 sessionID, CPacket* packet);

	static unsigned int WINAPI UpdateThread(LPVOID arg);
	static unsigned int WINAPI RedisThread(LPVOID arg);
	static unsigned int WINAPI HeartbeatThread(LPVOID arg);

	void UpdateThread();
	void RedisThread();
	void HeartbeatThread();

	void EnqueueRedis(UINT64 sessionID, CPacket* packet);

	BOOL Req_Login_Redis(UINT64 sessionID, CPacket* packet);
	BOOL CompleteLogin(UINT64 sessionID, CPacket* resultPacket, bool success);
	
	BOOL CheckTimeOut();

	CPacket* MakeLoginResult(INT64 account, const WCHAR* id, const WCHAR* nick);

private:
	BOOL exit;

	CLFFreeList<Player> playerPool;
	CLFFreeList< st_MSG> msgPool;

	std::unordered_map<UINT64, Player*> players;
	std::unordered_map<INT64, UINT64> accounts;


	std::unordered_set<Player*> sector[dfSECTOR_MAX_Y][dfSECTOR_MAX_X];

	HANDLE main_event;
	HANDLE update_thread;

	HANDLE heartbeat_thread;

	HANDLE redis_event;
	HANDLE redis_thread;
	
	boost::lockfree::queue<st_MSG*, boost::lockfree::capacity<4096>> msgQ;
	boost::lockfree::queue<st_Redis, boost::lockfree::capacity<4096>> redisQ;
	std::unique_ptr<cpp_redis::client> redis;



};