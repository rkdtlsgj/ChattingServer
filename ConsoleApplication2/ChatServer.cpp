#include "ChatServer.h"


ChatServer::ChatServer()
{
	exit = false;

	InitializeSRWLock(&playerLock);
	InitializeSRWLock(&accountLock);

	int idx = 0;
	for (int y = 0; y < dfSECTOR_MAX_Y; ++y)
	{
		for (int x = 0; x < dfSECTOR_MAX_X; ++x)
		{
			sector[y][x].lockIndex = idx++;
			InitializeSRWLock(&sector[y][x].lock);
		}
	}
}

ChatServer::~ChatServer()
{
	exit = TRUE;
	SetEvent(redis_event);

	WaitForSingleObject(redis_thread, INFINITE);
	WaitForSingleObject(heartbeat_thread, INFINITE);
	WaitForSingleObject(monitor_thread, INFINITE);

	CloseHandle(heartbeat_thread);
	CloseHandle(monitor_thread);

	CloseHandle(redis_event);
	CloseHandle(redis_thread);



	AcquireSRWLockExclusive(&playerLock);
	for (auto& kv : players)
	{
		Player* player = kv.second;
		if (player)
			ReleasePlayer(player);
	}
	players.clear();
	ReleaseSRWLockExclusive(&playerLock);

	AcquireSRWLockExclusive(&accountLock);
	accounts.clear();
	ReleaseSRWLockExclusive(&accountLock);
}

void ChatServer::OnClientJoin(SOCKADDR_IN* connectInfo, UINT64 sessionID)
{
	JoinProc(sessionID);
}

void ChatServer::OnClientLeave(UINT64 sessionID)
{
	LeaveProc(sessionID);
}

bool ChatServer::OnConnectionRequest(char* ip, int port)
{
	return true;
}


void ChatServer::OnRecv(UINT64 sessionID, CPacket* packet)
{
	if (!RecvProc(sessionID, packet))
	{
		Disconnect(sessionID);
	}
}

void ChatServer::OnSend(UINT64 sessionID, int sendsize)
{

}

void ChatServer::OnError(int errorcode, WCHAR* buf)
{

}


BOOL ChatServer::Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount)
{
	redis_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (IOCPServer::Start(ip, port, threadCount, nagle, maxUserCount) == false)
	{
		return false;
	}
	
	redis = std::make_unique<cpp_redis::client>();
	redis->connect("127.0.0.1", 6379);

	redis_thread = (HANDLE)_beginthreadex(NULL, 0, RedisThread, (LPVOID)this, NULL, NULL);

	heartbeat_thread = ((HANDLE)_beginthreadex(NULL, 0, HeartbeatThread, (LPVOID)this, NULL, NULL));

	monitor_thread = ((HANDLE)_beginthreadex(NULL, 0, MonitorThread, (LPVOID)this, NULL, NULL));

	return true;
}


unsigned int WINAPI ChatServer::RedisThread(LPVOID arg)
{
	((ChatServer*)arg)->RedisThread();
	return 0;
}

unsigned int WINAPI ChatServer::HeartbeatThread(LPVOID arg)
{
	((ChatServer*)arg)->HeartbeatThread();
	return 0;
}

unsigned int WINAPI ChatServer::MonitorThread(LPVOID arg)
{
	((ChatServer*)arg)->MonitorThread();
	return 0;
}

void ChatServer::RedisThread()
{
	while (!exit)
	{
		WaitForSingleObject(redis_event, INFINITE);

		st_Redis job{};

		if (exit)
			break;

		while (redisQ.pop(job))
		{
			UINT64 sessionID = job.session;
			CPacket* packet = job.packet;

			INT64 account = 0;
			WCHAR id[20]{};
			WCHAR nick[20]{};
			char clientKey[dfSESSIONKEY_LEN]{};
			(*packet) >> account;
			packet->GetData((char*)id, dfID_LEN);
			packet->GetData((char*)nick, dfNiCK_LEN);
			packet->GetData(clientKey, dfSESSIONKEY_LEN);

			char redisKey[32]{};
			_i64toa_s(account, redisKey, 32, 10);

			//테스트용
			redis->set(redisKey, std::string(clientKey, dfSESSIONKEY_LEN));
			redis->sync_commit();


			bool ok = false;
			std::string keyCopy(clientKey, dfSESSIONKEY_LEN);

			redis->get(redisKey, [keyCopy, &ok](cpp_redis::reply& r)
				{
					if (!r.is_string()) 
					{ 
						ok = false; 
						return; 
					}

					const std::string& v = r.as_string();
					ok = (v.size() == keyCopy.size()) && (memcmp(v.data(), keyCopy.data(), keyCopy.size()) == 0);
				});

			redis->sync_commit();
			
			CPacket* result = MakeLoginResult(account, id, nick);
			CompleteLogin(sessionID, result, ok);
			result->SubRef();

			packet->SubRef();
		}
	}
}

void ChatServer::HeartbeatThread()
{
	//players의 접근하는 스레드를 줄이기위해 메세지로 updatethread에서 처리할수있게한다.
	HANDLE heartevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	while (!exit)
	{
		DWORD result = WaitForSingleObject(heartevent, 1000);

		if (result == WAIT_TIMEOUT)
		{
			CheckTimeOut();			
		}		
	}

	CloseHandle(heartevent);	
}

void ChatServer::MonitorThread()
{
	
	HANDLE monitorevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	while (!exit)
	{
		DWORD result = WaitForSingleObject(monitorevent, 1000);

		if (result == WAIT_TIMEOUT)
		{
			monitor.Update();

			wprintf(L"Accept Total [%I64d]\n", acceptCount);
			wprintf(L"Recv TPS [%I64d]\n", recvTPS);
			wprintf(L"Send TPS [%I64d]\n", sendTPS);
			wprintf(L"Accept TPS [%I64d]\n", acceptTPS);
			wprintf(L"Connect User [%I64d]\n", sessionCount);
			wprintf(L"player Pool [%I64d] / player use [%I64d]\n", playerPool.GetAllocCount(), playerPool.GetUseCount());			
			//wprintf(L"msg Pool [%I64d] / msg Use [%I64d]\n", msgPool.GetAllocCount(), msgPool.GetUseCount());
			wprintf(L"CPU(SYS): %.1f%%  CPU(PROC): %.1f%%  "
				L"PrivateBytes: %lld  ProcNonPaged: %lld  AvailMem: %lld MB\n\n",
				monitor.ProcessorTotal(),
				monitor.ProcessTotal(),
				monitor.ProcessUserMemory(),
				monitor.ProcessNonPagedMemory(),
				monitor.AvailableMemoryMB());

			InterlockedExchange((LONG*)&recvTPS, 0);
			InterlockedExchange((LONG*)&sendTPS, 0);
			InterlockedExchange((LONG*)&acceptTPS, 0);
		}
	}

	CloseHandle(monitorevent);
}


CPacket* ChatServer::MakeLoginResult(INT64 account, const WCHAR* id, const WCHAR* nick)
{
	CPacket* p = CPacket::Alloc();
	(*p) << account;
	p->PutData((char*)id, dfID_LEN);
	p->PutData((char*)nick, dfNiCK_LEN);
	//p->SetEncodingCode();

	return p;
}

bool ChatServer::IsValidSector(WORD x, WORD y)
{
	return (x < dfSECTOR_MAX_X && y < dfSECTOR_MAX_Y);
}

st_SECTOR ChatServer::MakeAround(WORD x, WORD y)
{
	st_SECTOR s{};
	s.count = 0;
	for (int dy = -1; dy <= 1; ++dy)
	{
		for (int dx = -1; dx <= 1; ++dx)
		{
			if (!IsValidSector(x + dx, y + dy))
				continue;

			s.map[s.count] = { x + dx, y + dy };
			s.count++;
		}
	}


	/*for (int y = 0; y < 25; ++y)
	{
		for (int x = 0; x < 25; ++x)
		{
			s.map[s.count++] = { x, y };
		}
	}*/

	return s;
}

//BOOL ChatServer::CreatePlayer(UINT64 sessionID)
//{
//	Player* player = playerPool.Alloc();
//	if (player == nullptr)
//		return FALSE;
//
//	player->Reset();
//	player->sessionID = sessionID;
//	player->lastRecvTime = GetTickCount64();
//
//	players.emplace(sessionID, player);
//
//	return TRUE;
//}
//
//void ChatServer::DeletePlayer(UINT64 sessionID)
//{
//	Player* player = FindPlayer(sessionID);
//	if (player == nullptr)
//		return;
//
//	if (player->isLogined && player->accountNo != 0)
//		accounts.erase(player->accountNo);
//
//
//	ErasePlayer(sessionID);
//	DeletePlayerSector(player);
//	playerPool.Free(player);
//}
//
//void ChatServer::ErasePlayer(UINT64 sessionID)
//{
//	auto it = players.find(sessionID);
//	if (it == players.end())
//		return;
//
//	players.erase(it);
//}
//
//Player* ChatServer::FindPlayer(UINT64 sessionID)
//{
//	Player* player = nullptr;
//	auto it = players.find(sessionID);
//	if (it == players.end())
//		return nullptr;
//
//	player = it->second;
//
//	return player;
//}

//void ChatServer::DeletePlayerSector(Player* player)
//{
//	if (player != nullptr)
//	{
//		WORD y = player->sectorY;
//		WORD x = player->sectorX;
//		if (!IsValidSector(x, y))
//			return;
//
//		sector[y][x].erase(player);
//
//		player->sectorX = -1;
//		player->sectorY = -1;
//	}
//}

BOOL ChatServer::CompleteLogin(UINT64 sessionID, CPacket* packet, bool success)
{
	Player* player = AcquirePlayer(sessionID);
	if (player == nullptr)
	{		
		return true;
	}

	if (player->sessionID != sessionID)
	{
		return false;
	}

	if (player->closing.load(std::memory_order_acquire))
	{
		ReleasePlayer(player);
		return true;
	}

	INT64 account = 0;
	WCHAR id[20];
	WCHAR nick[20];

	(*packet) >> account;
	packet->GetData((char*)id, dfID_LEN);
	packet->GetData((char*)nick, dfNiCK_LEN);

	if (success)
	{
		AcquireSRWLockExclusive(&accountLock);
		accounts[account] = sessionID;
		ReleaseSRWLockExclusive(&accountLock);

		player->isLogined = true;
		
		player->accountNo = account;
		wcscpy_s(player->id, dfID_LEN / 2, id);
		wcscpy_s(player->nick, dfNiCK_LEN / 2, nick);

		CPacket* res = Res_Login(account, 1);
		if (res) 
		{ 
			SendUnicast(sessionID, res); 
			res->SubRef(); 
		}
	}
	else
	{
		CPacket* res = Res_Login(account, 0);
		if (res) 
		{ 
			SendUnicast(sessionID, res); res->SubRef(); 
		}

	}

	ReleasePlayer(player);

	return TRUE;
}

BOOL ChatServer::Req_Login_Redis(UINT64 sessionID, Player* player, CPacket* packet)
{	
	if (player == nullptr)
		return false;

	if (player->closing.load(std::memory_order_acquire)) 
		return false;

	if (player->sessionID != sessionID) 
		return false;

	if (player->accountNo != 0 || player->isLogined) 
		return false;
	
	packet->AddRef();
	EnqueueRedis(sessionID, packet);

	
	return TRUE;
}

void ChatServer::EnqueueRedis(UINT64 sessionID, CPacket* packet)
{
	st_Redis job{ sessionID, packet };
	redisQ.push(job);
	SetEvent(redis_event);
}

CPacket* ChatServer::Res_Login(INT64 account, BYTE status)
{
	CPacket* packet = CPacket::Alloc();
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_LOGIN;
	(*packet) << status;
	(*packet) << account;

	packet->SetEncodingCode();
	return packet;
}



BOOL ChatServer::Req_SectorMove(UINT64 sessionID, Player* player, CPacket* packet)
{
	INT64 account = 0;
	WORD x;
	WORD y;

	(*packet) >> account;
	(*packet) >> x;
	(*packet) >> y;

	if (!IsValidSector(x, y))
		return false;

	WORD ox = (WORD)player->sectorX;
	WORD oy = (WORD)player->sectorY;
	bool hadOld = IsValidSector(ox, oy);

	if (hadOld)
	{
		LockTwoSectorsExclusive(ox, oy, x, y);
		sector[oy][ox].set.erase(player);
		sector[y][x].set.insert(player);
		UnlockTwoSectorsExclusive(ox, oy, x, y);
	}
	else
	{
		LockSectorExclusive(x, y);
		sector[y][x].set.insert(player);
		UnlockSectorExclusive(x, y);
	}

	player->sectorX = x;
	player->sectorY = y;
	player->lastRecvTime = GetTickCount64();

	CPacket* res = Res_SectorMove(account, x, y);
	if (res)
	{
		SendPacket(sessionID, res);
		res->SubRef();
	}
	return true;
}

CPacket* ChatServer::Res_SectorMove(INT64 account, WORD x, WORD y)
{
	CPacket* packet = CPacket::Alloc();
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
	(*packet) << account;
	(*packet) << x;
	(*packet) << y;

	packet->SetEncodingCode();
	return packet;
}

BOOL ChatServer::Req_Chat(UINT64 sessionID, Player* player, CPacket* packet)
{
	if (player->isLogined == false)
		return false;

	INT64 account = 0;
	WORD msgLenBytes = 0;

	(*packet) >> account;
	(*packet) >> msgLenBytes;

	if (msgLenBytes == 0)
		return false;


	WCHAR msg[1024];
	packet->GetData((char*)msg, msgLenBytes);

	CPacket* res = Res_Chat(player, msg, msgLenBytes);
	if (!res)
		return false;

	WORD cx = (WORD)player->sectorX;
	WORD cy = (WORD)player->sectorY;
	if (!IsValidSector(cx, cy))
	{
		res->SubRef();
		return false;
	}


	st_SECTOR around = MakeAround(cx, cy);

	std::vector<UINT64> targets;
	targets.reserve(256);

	for (int i = 0; i < around.count; ++i)
	{
		WORD x = around.map[i].x;
		WORD y = around.map[i].y;

		AcquireSRWLockShared(&sector[y][x].lock);
		for (Player* t : sector[y][x].set)
		{
			if (!t) continue;
			if (!t->isLogined) continue;
			targets.push_back(t->sessionID);
		}
		ReleaseSRWLockShared(&sector[y][x].lock);
	}

	// 락 없이 전송
	for (UINT64 sid : targets)
		SendPacket(sid, res);

	res->SubRef();
	return true;
}


CPacket* ChatServer::Res_Chat(Player* player, WCHAR* msg, WORD len)
{
	CPacket* packet = CPacket::Alloc();
	(*packet) << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE;
	(*packet) << player->accountNo;
	packet->PutData((char*)player->id, dfID_LEN);
	packet->PutData((char*)player->nick, dfNiCK_LEN);
	(*packet) << len;
	packet->PutData((char*)msg, len);
	int wlen = len / 2;
	if (wlen >= 1024) wlen = 1023;
	msg[wlen] = 0;
	packet->SetEncodingCode();

	return packet;
}


void ChatServer::SendUnicast(UINT64 sessionID, CPacket* packet)
{
	SendPacket(sessionID, packet);
}

//void ChatServer::SendPacket_SectorOne(WORD x, WORD y, CPacket* packet, Player* player)
//{
//	for (Player* target : sector[y][x])
//	{
//		if (!target)
//			continue;
//
//		if (!target->isLogined)
//			continue;
//
//		if (!player && !target)
//			continue;
//
//		if (player && target == player)
//			continue;
//
//		SendUnicast(target->sessionID, packet);
//	}
//}
//
//void ChatServer::SendPacket_Around(Player* player, CPacket* packet, bool bSendMe)
//{
//	st_SECTOR around = MakeAround(player->sectorX, player->sectorY);
//
//	for (int i = 0; i < around.count; ++i)
//	{
//		WORD x = around.map[i].x;
//		WORD y = around.map[i].y;
//
//		for (Player* target : sector[y][x])
//		{
//			if (!target)
//				continue;
//
//			if (!target->isLogined)
//				continue;
//
//			if (!bSendMe && target == player)
//				continue;
//
//			SendUnicast(target->sessionID, packet);
//		}
//	}
//}

BOOL ChatServer::CheckTimeOut()
{
	INT64 curTime = GetTickCount64();
	std::vector<UINT64> kickList;
	kickList.reserve(1024);

	AcquireSRWLockShared(&playerLock);
	for (auto& kv : players)
	{
		Player* player = kv.second;
		if (!player) 
			continue;

		UINT64 sid = kv.first;
		
		if (curTime > player->lastRecvTime && (curTime - player->lastRecvTime) > 40000)
			kickList.push_back(sid);
	}
	ReleaseSRWLockShared(&playerLock);
	
	for (UINT64 sid : kickList)
		Disconnect(sid);

	return TRUE;
}

void ChatServer::AddRefPlayer(Player* player)
{
	player->ref.fetch_add(1, std::memory_order_relaxed);
}

void ChatServer::ReleasePlayer(Player* player)
{
	long r = player->ref.fetch_sub(1, std::memory_order_acq_rel) - 1;
	if (r == 0)
	{
		playerPool.Free(player);
	}
}

Player* ChatServer::AcquirePlayer(UINT64 sessionID)
{
	Player* player = nullptr;
	AcquireSRWLockShared(&playerLock);
	auto it = players.find(sessionID);
	if (it != players.end())
	{
		player = it->second;
		if (player)
			AddRefPlayer(player);
	}
	ReleaseSRWLockShared(&playerLock);

	if (player && player->closing.load(std::memory_order_acquire))
	{
		ReleasePlayer(player);
		return nullptr;
	}
	return player;
}

bool ChatServer::JoinProc(UINT64 sessionID)
{
	Player* player = playerPool.Alloc();
	if (!player)
		return false;

	player->Reset();
	player->sessionID = sessionID;
	player->lastRecvTime = GetTickCount64();
	player->ref.store(1);
	player->closing.store(false);

	AcquireSRWLockExclusive(&playerLock);
	if (players.find(sessionID) != players.end())
	{
		ReleaseSRWLockExclusive(&playerLock);
		playerPool.Free(player);
		return false;
	}
	players.emplace(sessionID, player);
	ReleaseSRWLockExclusive(&playerLock);

	return true;
}


bool ChatServer::LeaveProc(UINT64 sessionID)
{
	Player* player = nullptr;

	AcquireSRWLockExclusive(&playerLock);
	auto it = players.find(sessionID);
	if (it == players.end())
	{
		ReleaseSRWLockExclusive(&playerLock);
		return true;
	}
	player = it->second;
	players.erase(it);

	if (player)
	{
		AddRefPlayer(player);            
		player->closing.store(true);    
	}
	ReleaseSRWLockExclusive(&playerLock);

	if (!player) \
		return true;


	if (player->isLogined && player->accountNo != 0)
	{
		AcquireSRWLockExclusive(&accountLock);
		auto it2 = accounts.find(player->accountNo);
		if (it2 != accounts.end() && it2->second == sessionID)
			accounts.erase(it2);
		ReleaseSRWLockExclusive(&accountLock);
	}

	if (IsValidSector((WORD)player->sectorX, (WORD)player->sectorY))
	{
		LockSectorExclusive((WORD)player->sectorX, (WORD)player->sectorY);
		sector[player->sectorY][player->sectorX].set.erase(player);
		UnlockSectorExclusive((WORD)player->sectorX, (WORD)player->sectorY);
	}
	
	player->closing.store(true);
	ReleasePlayer(player);

	return true;
}

bool ChatServer::RecvProc(UINT64 sessionID, CPacket* packet)
{
	Player* player = AcquirePlayer(sessionID);
	if (!player) 
		return false;

	
	player->lastRecvTime = GetTickCount64();

	WORD type = 0;
	(*packet) >> type;

	bool ok = false;
	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		ok = Req_Login_Redis(sessionID, player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		ok = Req_SectorMove(sessionID, player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		ok = Req_Chat(sessionID, player, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		ok = true;
		break;
	default:
		ok = false;
		break;
	}

	ReleasePlayer(player);
	return ok;
}

void ChatServer::LockSectorExclusive(WORD x, WORD y)
{
	AcquireSRWLockExclusive(&sector[y][x].lock);
}
void ChatServer::UnlockSectorExclusive(WORD x, WORD y)
{
	ReleaseSRWLockExclusive(&sector[y][x].lock);
}

void ChatServer::LockTwoSectorsExclusive(WORD x1, WORD y1, WORD x2, WORD y2)
{
	int i1 = sector[y1][x1].lockIndex;
	int i2 = sector[y2][x2].lockIndex;

	if (i1 == i2)
	{
		LockSectorExclusive(x1, y1);
		return;
	}
	if (i1 < i2)
	{
		LockSectorExclusive(x1, y1);
		LockSectorExclusive(x2, y2);
	}
	else
	{
		LockSectorExclusive(x2, y2);
		LockSectorExclusive(x1, y1);
	}
}

void ChatServer::UnlockTwoSectorsExclusive(WORD x1, WORD y1, WORD x2, WORD y2)
{
	if (sector[y1][x1].lockIndex == sector[y2][x2].lockIndex)
	{
		UnlockSectorExclusive(x1, y1);
		return;
	}
	UnlockSectorExclusive(x1, y1);
	UnlockSectorExclusive(x2, y2);
}