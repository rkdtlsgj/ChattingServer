#include "ChatServer.h"


ChatServer::ChatServer()
{
	exit = false;
}

ChatServer::~ChatServer()
{
	exit = TRUE;
	SetEvent(main_event);
	SetEvent(redis_event);

	WaitForSingleObject(update_thread, INFINITE);
	WaitForSingleObject(redis_thread, INFINITE);
	WaitForSingleObject(heartbeat_thread, INFINITE);
	WaitForSingleObject(monitor_thread, INFINITE);

	CloseHandle(main_event);
	CloseHandle(update_thread);
	CloseHandle(heartbeat_thread);
	CloseHandle(monitor_thread);

	CloseHandle(redis_event);
	CloseHandle(redis_thread);
}

void ChatServer::OnClientJoin(SOCKADDR_IN* connectInfo, UINT64 sessionID)
{
	st_MSG* msg = msgPool.Alloc();
	msg->type = en_MSG_JOIN;
	msg->session = sessionID;
	msg->packet = nullptr;

	msgQ.push(msg);
	SetEvent(main_event);

	//CreatePlayer(sessionID);
}

void ChatServer::OnClientLeave(UINT64 sessionID)
{
	st_MSG* msg = msgPool.Alloc();
	msg->type = en_MSG_LEAVE;
	msg->session = sessionID;
	msg->packet = nullptr;

	msgQ.push(msg);
	SetEvent(main_event);

	//DeletePlayer(sessionID);
}

bool ChatServer::OnConnectionRequest(char* ip, int port)
{
	return true;
}


void ChatServer::OnRecv(UINT64 sessionID, CPacket* packet)
{
	st_MSG* msg = msgPool.Alloc();
	packet->AddRef();

	msg->type = en_MSG_RECV;
	msg->session = sessionID;
	msg->packet = packet;;

	msgQ.push(msg);
	SetEvent(main_event);
}

void ChatServer::OnSend(UINT64 sessionID, int sendsize)
{

}

void ChatServer::OnError(int errorcode, WCHAR* buf)
{

}


BOOL ChatServer::Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount)
{
	main_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	redis_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	if (IOCPServer::Start(ip, port, threadCount, nagle, maxUserCount) == false)
	{
		return false;
	}
	
	redis = std::make_unique<cpp_redis::client>();
	redis->connect("127.0.0.1", 6379);

	redis_thread = (HANDLE)_beginthreadex(NULL, 0, RedisThread, (LPVOID)this, NULL, NULL);

	update_thread = ((HANDLE)_beginthreadex(NULL, 0, UpdateThread, (LPVOID)this, NULL, NULL));

	heartbeat_thread = ((HANDLE)_beginthreadex(NULL, 0, HeartbeatThread, (LPVOID)this, NULL, NULL));

	monitor_thread = ((HANDLE)_beginthreadex(NULL, 0, MonitorThread, (LPVOID)this, NULL, NULL));

	return true;
}

unsigned int WINAPI ChatServer::UpdateThread(LPVOID arg)
{
	((ChatServer*)arg)->UpdateThread();
	return 0;
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


void ChatServer::UpdateThread()
{
	while (exit == false)
	{
		DWORD result = WaitForSingleObject(main_event, INFINITE);
		st_MSG* msg = nullptr;

		while (msgQ.pop(msg))
		{
			switch (msg->type)
			{
			case en_MSG_RECV:
				Heartbeat(msg->session);
				PacketProcess(msg->session, msg->packet);
				break;

			case en_MSG_JOIN:
				CreatePlayer(msg->session);
				break;

			case en_MSG_LEAVE:
				DeletePlayer(msg->session);
				break;

			case en_MSG_HEART:
				CheckTimeOut();
				break;

			case en_MSG_LOGIN_OK:
				CompleteLogin(msg->session, msg->packet, true);
				break;

			case en_MSG_LOGIN_FAIL:
				CompleteLogin(msg->session, msg->packet, false);
				//오류
				break;

			default:
				break;
			}

			msgPool.Free(msg);
		}
	}
}

void ChatServer::RedisThread()
{
	while (!exit)
	{
		WaitForSingleObject(redis_event, INFINITE);

		st_Redis job{};

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

			bool ok = false;

			redis->get(redisKey, [clientKey, &ok](cpp_redis::reply& r) 
				{
					if (!r.is_string()) 
					{ 
						ok = false; 
						return; 
					}

					ok = (strcmp(r.as_string().c_str(), clientKey) == 0);
				});

			redis->sync_commit();
			
			CPacket* result = MakeLoginResult(account, id, nick);

			st_MSG* msg = msgPool.Alloc();
			msg->session = sessionID;
			msg->packet = result;
			msg->type = ok ? en_MSG_LOGIN_OK : en_MSG_LOGIN_FAIL;

			msgQ.push(msg);
			SetEvent(main_event);

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
			st_MSG* msg = msgPool.Alloc();
			msg->type = en_MSG_HEART;
			msg->session = 0;
			msg->packet = nullptr;

			msgQ.push(msg);	
			//느슨한 처리를 위해 이벤트는 꺠우지않는다.
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
			wprintf(L"Accept Total [%I64d]\n", acceptCount);
			wprintf(L"Recv TPS [%I64d]\n", recvTPS);
			wprintf(L"Send TPS [%I64d]\n", sendTPS);
			wprintf(L"Accept TPS [%I64d]\n", acceptTPS);
			wprintf(L"Connect User [%I64d]\n", sessionCount);
			wprintf(L"player Pool [%I64d]\n", playerPool.GetAllocCount());
			wprintf(L"player use [%I64d]\n", playerPool.GetUseCount());
			wprintf(L"msg Pool [%I64d]\n", msgPool.GetAllocCount());
			wprintf(L"msg Use [%I64d]\n\n", msgPool.GetUseCount());




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
	p->SetEncodingCode();

	return p;
}

BOOL ChatServer::PacketProcess(UINT64 sessionID, CPacket* packet)
{
	BOOL result = false;
	WORD type;
	(*packet) >> type;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		result = Req_Login_Redis(sessionID, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		result = Req_SectorMove(sessionID, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		result = Req_Chat(sessionID, packet);
		break;

	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		result = true;
		break;

	default:
		break;
	}

	packet->SubRef();

	return result;
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

	return s;
}

BOOL ChatServer::CreatePlayer(UINT64 sessionID)
{
	Player* player = playerPool.Alloc();
	if (player == nullptr)
		return FALSE;

	player->Reset();
	player->sessionID = sessionID;

	players.emplace(sessionID, player);

	return TRUE;
}

void ChatServer::DeletePlayer(UINT64 sessionID)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return;

	if (player->isLogined && player->accountNo != 0)
		accounts.erase(player->accountNo);


	ErasePlayer(sessionID);
	DeletePlayerSector(player);
	playerPool.Free(player);
}

void ChatServer::ErasePlayer(UINT64 sessionID)
{
	auto it = players.find(sessionID);
	if (it == players.end())
		return;

	players.erase(it);
}

Player* ChatServer::FindPlayer(UINT64 sessionID)
{
	Player* player = nullptr;
	auto it = players.find(sessionID);
	if (it == players.end())
		return nullptr;

	player = it->second;

	return player;
}

void ChatServer::DeletePlayerSector(Player* player)
{
	if (player != nullptr)
	{
		WORD y = player->sectorY;
		WORD x = player->sectorX;
		if (!IsValidSector(x, y))
			return;

		sector[y][x].erase(player);

		player->sectorX = -1;
		player->sectorY = -1;
	}
}

BOOL ChatServer::CompleteLogin(UINT64 sessionID, CPacket* packet, bool success)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
	{
		packet->SubRef();
		return true;
	}

	if (player->sessionID != sessionID)
	{
		packet->SubRef();
		return false;
	}

	INT64 account = 0;
	WCHAR id[20];
	WCHAR nick[20];

	(*packet) >> account;
	packet->GetData((char*)id, dfID_LEN);
	packet->GetData((char*)nick, dfNiCK_LEN);

	if (success)
	{
		accounts[account] = sessionID;
		player->isLogined = true;

		//player->SetLogin(sessionID, account, id, nick, key);

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

	packet->SubRef();
	return TRUE;
}


BOOL ChatServer::Req_Login(UINT64 sessionID, CPacket* packet)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return false;

	if (player->sessionID == sessionID)
		return false;

	if (player->sessionID != 0 || player->isLogined)
		return false;

	BYTE status = 1;
	INT64 account = 0;
	WCHAR id[20];
	WCHAR nick[20];
	char key[dfSESSIONKEY_LEN];

	(*packet) >> account;

	packet->GetData((char*)id, dfID_LEN);
	packet->GetData((char*)nick, dfNiCK_LEN);
	packet->GetData(key, dfSESSIONKEY_LEN);

	accounts[account] = sessionID;

	player->isLogined = true;
	player->SetLogin(sessionID, account, id, nick, key);

	CPacket* res = Res_Login(account, status);

	if (res)
	{
		SendUnicast(sessionID, res);
		res->SubRef();
	}


	return true;
}

BOOL ChatServer::Req_Login_Redis(UINT64 sessionID, CPacket* packet)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
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



BOOL ChatServer::Req_SectorMove(UINT64 sessionID, CPacket* packet)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return false;


	INT64 account = 0;
	WORD x;
	WORD y;

	(*packet) >> account;
	(*packet) >> x;
	(*packet) >> y;

	if (!IsValidSector(x, y))
		return false;

	DeletePlayerSector(player);
	player->SetSector(x, y);
	sector[y][x].insert(player);


	CPacket* res = Res_SectorMove(account, x, y);
	if (res)
	{
		SendUnicast(sessionID, res);
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

BOOL ChatServer::Req_Chat(UINT64 sessionID, CPacket* packet)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return false;

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

	SendPacket_Around(player, res, true);
	res->SubRef();

	return true;
}

BOOL ChatServer::Heartbeat(UINT64 sessionID)
{
	Player* player = FindPlayer(sessionID);
	if (player == nullptr)
		return false;
	if (player->sessionID != sessionID)
		return false;

	player->lastRecvTime = GetTickCount64();

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

void ChatServer::SendPacket_SectorOne(WORD x, WORD y, CPacket* packet, Player* player)
{
	for (Player* target : sector[y][x])
	{
		if (!target)
			continue;

		if (!target->isLogined)
			continue;

		if (!player && !target)
			continue;

		if (player && target == player)
			continue;

		SendUnicast(target->sessionID, packet);
	}
}

void ChatServer::SendPacket_Around(Player* player, CPacket* packet, bool bSendMe)
{
	st_SECTOR around = MakeAround(player->sectorX, player->sectorY);

	for (int i = 0; i < around.count; ++i)
	{
		WORD x = around.map[i].x;
		WORD y = around.map[i].y;

		for (Player* target : sector[y][x])
		{
			if (!target)
				continue;

			if (!target->isLogined)
				continue;

			if (!bSendMe && target == player)
				continue;

			SendUnicast(target->sessionID, packet);
		}
	}
}

BOOL ChatServer::CheckTimeOut()
{
	INT64 curTime = GetTickCount64();
	std::vector<UINT64> kickList;


	for (auto& iter : players)
	{
		INT64 lastTime = iter.second->lastRecvTime;
		if (curTime > lastTime && (curTime - lastTime) > 40000)
			kickList.push_back(iter.first);

	}

	for (UINT64 sessionID : kickList)
		Disconnect(sessionID);

	return true;
}