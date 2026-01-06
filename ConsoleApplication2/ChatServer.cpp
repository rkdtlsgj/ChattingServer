#include "ChatServer.h"


ChatServer::ChatServer()
{
	exit = false;
}

ChatServer::~ChatServer()
{
	exit = TRUE;
	SetEvent(main_event);
	WaitForSingleObject(update_thread, INFINITE);

	CloseHandle(main_event);
	CloseHandle(update_thread);
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

	if (IOCPServer::Start(ip, port, threadCount, nagle, maxUserCount) == false)
	{
		return false;
	}

	
	update_thread = ((HANDLE)_beginthreadex(NULL, 0, UpdateThread, (LPVOID)this, NULL, NULL));

	return true;
}

unsigned int WINAPI ChatServer::UpdateThread(LPVOID arg)
{
	((ChatServer*)arg)->UpdateThread();
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
					PacketProcess(msg->session, msg->packet);
					break;

				case en_MSG_JOIN:
					CreatePlayer(msg->session);
					break;

				case en_MSG_LEAVE:
					DeletePlayer(msg->session);
					break;

				default:
					break;
			}

			msgPool.Free(msg);
		}
	}
}

BOOL ChatServer::PacketProcess(UINT64 sessionID, CPacket* packet)
{
	BOOL result = false;
	WORD type;
	(*packet) >> type;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		result = Req_Login(sessionID, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		result = Req_SectorMove(sessionID, packet);
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		result = Req_Chat(sessionID, packet);
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