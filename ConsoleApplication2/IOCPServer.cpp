#include "IOCPServer.h"


BOOL IOCPServer::Start(const WCHAR* ip, int port, short threadCount, bool nagle, int maxUserCount)
{
	int retval;


	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	hcp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (hcp == NULL)
		return false;

	listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (listenSocket == INVALID_SOCKET)
		return false;

	ZeroMemory(&serrver_addr, sizeof(serrver_addr));

	serrver_addr.sin_family = AF_INET;
	serrver_addr.sin_port = htons(port);

	if (!ip)
		serrver_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		InetPton(AF_INET, ip, &serrver_addr.sin_addr);

	InetPton(AF_INET, ip, &serrver_addr.sin_addr);
	serrver_addr.sin_port = htons(port);

	if (nagle == true)
	{
		int tcpNoDelay = TRUE;
		setsockopt(listenSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&tcpNoDelay, sizeof(tcpNoDelay));
	}

	retval = bind(listenSocket, (SOCKADDR*)&serrver_addr, sizeof(serrver_addr));
	if (retval == SOCKET_ERROR)
		return false;


	retval = listen(listenSocket, SOMAXCONN);
	if (retval == SOCKET_ERROR)
		return false;


	maxClientCount = maxUserCount;
	sessionArray = new Session[maxUserCount];
	for (int i = maxUserCount - 1; i >= 0; i--)
	{
		indexStack.push(i);
	}


	sessionCount = 0;
	sendTPS = 0;
	recvTPS = 0;
	acceptTPS = 0;
	acceptCount = 0;


	allThreadCount = threadCount + 1;
	threadCount = threadCount;

	acceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, (LPVOID)this, 0, 0);

	monitorThread = (HANDLE)_beginthreadex(NULL, 0, MonitorThread, (LPVOID)this, 0, 0);

	for (int i = 1; i < allThreadCount; i++)
		threadVector.push_back((HANDLE)_beginthreadex(NULL, 0, WorkerThread, (LPVOID)this, 0, 0));

	return true;
}

void IOCPServer::Stop()
{
	closesocket(listenSocket);
	CloseHandle(acceptThread);
	CloseHandle(monitorThread);

	if (hcp != NULL)
	{
		const int workerCount = (int)threadVector.size();
		for (int i = 0; i < workerCount; ++i)
		{
			PostQueuedCompletionStatus(hcp, 0, 0, NULL);
		}
	}

	if (acceptThread)
	{
		WaitForSingleObject(acceptThread, INFINITE);
		CloseHandle(acceptThread);
	}

	if (monitorThread)
	{
		WaitForSingleObject(monitorThread, INFINITE);
		CloseHandle(monitorThread);
	}

	for (HANDLE h : threadVector)
	{
		if (h != NULL)
		{
			WaitForSingleObject(h, INFINITE);
			CloseHandle(h);
		}
	}
	threadVector.clear();

	if (sessionArray != nullptr)
	{
		for (int i = 0; i < maxClientCount; ++i)
		{
			Session* s = &sessionArray[i];

			if (s->sock != INVALID_SOCKET)
			{
				DisConnect(s);
			}

			CPacket* p = nullptr;
			while (s->sendBuf.pop(p))
			{
				if (p) p->SubRef();
			}
			while (s->completeSendBuf.pop(p))
			{
				if (p) p->SubRef();
			}

			s->recvBuf.ClearBuffer();
		}

		delete[] sessionArray;
		sessionArray = nullptr;
	}

	if (hcp != NULL)
	{
		CloseHandle(hcp);
		hcp = NULL;
	}

	WSACleanup();

	sessionCount = 0;
	sendTPS = 0;
	recvTPS = 0;
	acceptTPS = 0;
	acceptCount = 0;
}

unsigned int WINAPI IOCPServer::WorkerThread(LPVOID arg)
{
	IOCPServer* server = (IOCPServer*)arg;

	DWORD transferred = 0;
	OVERLAPPED* overlapped = NULL;
	Session* session = NULL;

	int retval;
	while (true)
	{
		transferred = 0;
		overlapped = NULL;
		session = NULL;

		retval = GetQueuedCompletionStatus(server->hcp, &transferred, (PULONG_PTR)&session, (LPOVERLAPPED*)&overlapped, INFINITE);

		if (overlapped == NULL)
		{
			if (retval == 0)
			{
				int errorCode = WSAGetLastError();
				wprintf(L"IOCP ErrorCode : [%d] \n", errorCode);
				break;
			}
			else if (transferred == 0 && session == NULL)
			{
				//종료!
				wprintf(L"IOCP 종료 \n");
				//server->DisConnect(session);
				break;
			}
		}
		else
		{
			if (overlapped == &session->recvOverlap)
			{
				server->RecvComplete(session, transferred);
			}

			if (overlapped == &session->sendOverlap)
			{
				server->SendComplete(session, transferred);
			}


			if (InterlockedDecrement((LONG*)&session->ioCount) == 0)
			{
				if (session->isReleased)
					server->ReleaseSession(session);

			}

		}
	}

	return 0;
}

unsigned int WINAPI IOCPServer::AcceptThread(LPVOID arg)
{
	IOCPServer* server = (IOCPServer*)arg;
	SOCKET clinetSocket;
	SOCKADDR_IN clientAddr;
	int addrlen = sizeof(SOCKADDR_IN);

	while (true)
	{
		clinetSocket = accept(server->listenSocket, (SOCKADDR*)&clientAddr, &addrlen);
		if (clinetSocket == INVALID_SOCKET)
			break;


		if (server->sessionCount >= server->maxClientCount)
		{
			//연결끊기
			closesocket(clinetSocket);
			continue;
		}

		char ip[16];
		inet_ntop(AF_INET, &clientAddr.sin_addr, ip, 16);

		if (server->OnConnectionRequest(ip, clientAddr.sin_port) == true)
		{
			Session* session = server->CreateSession(clinetSocket);

			CreateIoCompletionPort((HANDLE)clinetSocket, server->hcp, (ULONG_PTR)session, 0);

			server->OnClientJoin(&session->clienr_addr, session->sessionID);
			server->PostRecv(session);

			InterlockedIncrement((LONG*)&server->acceptTPS);
			//InterlockedIncrement((LONG*)&server->acceptCount);

			int index = GET_SESSION_INDEX(session->sessionID);
			int id = GET_SESSION_ID(session->sessionID);

			//wprintf(L"Accept SeesionID : [%d][%d][%d]\n", session->sock, index, id);

		}
		else
		{
			closesocket(clinetSocket);
			continue;
		}
	}

	return 0;
}


unsigned WINAPI IOCPServer::MonitorThread(LPVOID arg)
{
	IOCPServer* server = (IOCPServer*)arg;

	while (1)
	{
		wprintf(L"Accept Total [%I64d]\n", server->acceptCount);
		wprintf(L"Recv TPS [%I64d]\n", server->recvTPS);
		wprintf(L"Send TPS [%I64d]\n", server->sendTPS);
		wprintf(L"Accept TPS [%I64d]\n", server->acceptTPS);
		wprintf(L"Connect User [%I64d]\n\n", server->sessionCount);

		InterlockedExchange((LONG*)&server->recvTPS, 0);
		InterlockedExchange((LONG*)&server->sendTPS, 0);
		InterlockedExchange((LONG*)&server->acceptTPS, 0);

		Sleep(999);
	}
}

Session* IOCPServer::CreateSession(SOCKET socket)
{
	UINT64 index = PopIndex();

	//1차 heartbit
	tcp_keepalive tcpAlive;
	tcpAlive.onoff = 1;
	tcpAlive.keepalivetime = 3000;
	tcpAlive.keepaliveinterval = 2000;
	WSAIoctl(socket, SIO_KEEPALIVE_VALS, &tcpAlive, sizeof(tcp_keepalive), 0, 0, NULL, NULL, NULL);



	Session* newSession = &sessionArray[index];
	newSession->sessionID = COMBINE_ID_INDEX(++sessionID, index);
	newSession->index = index;
	newSession->sock = socket;
	newSession->recvBuf.ClearBuffer();
	newSession->lastRecvTime = GetTickCount64(); //2차 heartbit	


	InterlockedIncrement64(&sessionCount);
	InterlockedIncrement64(&acceptTPS);
	InterlockedIncrement64(&acceptCount);

	return newSession;


}

void IOCPServer::ReleaseSession(Session* session)
{
	//wprintf(L"[ReleaseSession] sid=%I64d idx=%I64d io=%ld\n", session->sessionID, session->index, session->ioCount);

	OnClientLeave(session->sessionID);

	CPacket* p = nullptr;
	while (session->sendBuf.pop(p))
	{
		if (p)
			p->SubRef();
	}
	while (session->completeSendBuf.pop(p))
	{
		if (p)
			p->SubRef();
	}

	INT64 idx = session->index;
	session->recvBuf.ClearBuffer();
	session->Init();

	PushIndex(idx);
	InterlockedDecrement64(&sessionCount);

	return;
}

void IOCPServer::DisConnect(Session* session)
{
	if (session == NULL)
		return;

	if (InterlockedCompareExchange(&session->isReleased, TRUE, FALSE) == TRUE)
		return;

	shutdown(session->sock, SD_BOTH);
	CancelIoEx((HANDLE)session->sock, NULL);

	LINGER opt{ 1, 0 };

	setsockopt(session->sock, SOL_SOCKET, SO_LINGER, (char*)&opt, sizeof(opt));
	closesocket(session->sock);
	session->sock = INVALID_SOCKET;

	if (InterlockedCompareExchange(&session->ioCount, 0, 0) == 0)
	{
		ReleaseSession(session);
	}
}

void IOCPServer::Disconnect(UINT64 sessionID)
{
	Session* session = SessionLock(sessionID);
	if (session == NULL) 
		return;

	DisConnect(session);
	SessionUnLock(session);
}

bool IOCPServer::PostSend(Session* session)
{
	if (InterlockedCompareExchange((LONG*)&session->isSendFlag, 1, 0) != 0)
		return false;

	WSABUF wBuf[200];
	int bufCount = 0;
	DWORD recvVal = 0;
	CPacket* packet = NULL;
	int pktCount = 0;

	ZeroMemory(&session->sendOverlap, sizeof(OVERLAPPED));


	while (pktCount < 100 && (bufCount + 2) <= 200 && session->sendBuf.pop(packet))
	{
		if (!packet)
			continue;

		wBuf[bufCount].buf = (char*)packet->GetHeaderPtr();
		wBuf[bufCount].len = MESSAGE_HEADER_LEN;
		++bufCount;


		wBuf[bufCount].buf = packet->GetBufferPtr();
		wBuf[bufCount].len = (ULONG)packet->GetDataSize();
		++bufCount;

		session->completeSendBuf.push(packet);
		++pktCount;

	}

	if (pktCount == 0)
	{
		InterlockedExchange(&session->isSendFlag, 0);
		return false;
	}

	session->sendCount = pktCount;
	InterlockedIncrement(&session->ioCount);

	if (WSASend(session->sock, wBuf, bufCount, &recvVal, 0, &session->sendOverlap, NULL) == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			wprintf(L"[SendPost][%d]\n", error);
			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				ReleaseSession(session);
				//연결끊기
				return false;
			}
		}
	}

	return true;

}

bool IOCPServer::PostRecv(Session* session)
{
	if (session->isReleased == TRUE)
		return false;

	WSABUF wbuf[2];
	int bufCount = 1;
	DWORD recvVal = 0;
	DWORD flag = 0;

	ZeroMemory(&wbuf, sizeof(WSABUF) * 2);

	wbuf[0].buf = session->recvBuf.GetRearBufferPtr();
	wbuf[0].len = session->recvBuf.DirectEnqueueSize();

	if (session->recvBuf.DirectEnqueueSize() < session->recvBuf.GetFreeSize())
	{
		wbuf[1].buf = session->recvBuf.GetBufferPtr();
		wbuf[1].len = session->recvBuf.GetFreeSize() - wbuf[0].len;
		++bufCount;
	}


	ZeroMemory(&session->recvOverlap, sizeof(OVERLAPPED));
	InterlockedIncrement(&session->ioCount);

	int retval = WSARecv(session->sock, wbuf, bufCount, &recvVal, &flag, &session->recvOverlap, nullptr);
	if (retval == SOCKET_ERROR)
	{
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING)
		{
			if (InterlockedDecrement(&session->ioCount) == 0)
			{
				ReleaseSession(session);
			}


			DisConnect(session);
			return false;
		}
	}

	return true;

}

void IOCPServer::SendComplete(Session* session, DWORD transferred)
{
	if (transferred == 0)
	{
		DisConnect(session);
	}

	int cnt = session->sendCount;
	session->sendCount = 0;

	for (int i = 0; i < cnt; ++i)
	{
		CPacket* packet;
		if (!session->completeSendBuf.pop(packet) || packet == nullptr)
		{
			DisConnect(session);
			break;
		}

		packet->SubRef();
		InterlockedIncrement((LONG*)&sendTPS);
	}

	InterlockedExchange(&session->isSendFlag, 0);

	if (session->isReleased != TRUE)
	{
		PostSend(session);
	}
}

void IOCPServer::RecvComplete(Session* session, DWORD dwTransferred)
{
	if (dwTransferred == 0)
	{
		//종료
		DisConnect(session);
		return;
	}

	session->lastRecvTime = GetTickCount64();
	session->recvBuf.MoveRear(dwTransferred);

	while (true)
	{
		int qSize = session->recvBuf.GetUseSize();
		if (sizeof(st_PACKET_HEADER) > qSize)
			break;

		st_PACKET_HEADER stHeader;
		session->recvBuf.Peek((char*)&stHeader, sizeof(st_PACKET_HEADER));


		if (stHeader.code != PACKET_CODE)
		{
			DisConnect(session);
			return;
		}
		
		if (stHeader.len == 0)
		{
			DisConnect(session);
			return;
		}

		if (MESSAGE_HEADER_LEN + stHeader.len > qSize)
			break;		

		session->recvBuf.MoveFront(sizeof(st_PACKET_HEADER));


		CPacket* cPacket = CPacket::Alloc();
		cPacket->Clear();
		cPacket->SetMessageHeader((char*)&stHeader, MESSAGE_HEADER_LEN);

		if (stHeader.len != session->recvBuf.Dequeue(cPacket->GetBufferPtr(), stHeader.len))
		{
			DisConnect(session);
			return;			
		}

		cPacket->MoveWritePos(stHeader.len);


		if (cPacket->SetDecodingCode() == false)
		{
			cPacket->SubRef();
			DisConnect(session);
			return;
		}

		InterlockedIncrement((LONG*)&recvTPS);				

		OnRecv(session->sessionID, cPacket);
		cPacket->SubRef();
	}


	if (session->isReleased != TRUE)
	{
		PostRecv(session);
	}

	return;
}

void IOCPServer::SendPacket(UINT64 id, CPacket* packet)
{
	Session* session = SessionLock(id);
	if (session == NULL)
		return;

	packet->AddRef();
	packet->SetEncodingCode();
	
	session->sendBuf.push(packet);

	if (session->isReleased != TRUE)
		PostSend(session);

	SessionUnLock(session);
	return;
}

Session* IOCPServer::SessionLock(UINT64 sessionID)
{
	int index = GET_SESSION_INDEX(sessionID);
	Session* session = &sessionArray[index];

	InterlockedIncrement(&session->ioCount);

	if (sessionID != session->sessionID || session->isReleased)
	{
		if (InterlockedDecrement(&session->ioCount) == 0)
			ReleaseSession(session);

		return NULL;
	}

	if (sessionID != sessionArray[index].sessionID)
	{
		InterlockedDecrement(&session->ioCount);
		return NULL;
	}

	if (session->isReleased == true)
	{
		if (InterlockedDecrement(&session->ioCount) == 0)
			ReleaseSession(session);

		return NULL;
	}
	else
	{
		return &sessionArray[index];
	}

	return NULL;
}

void IOCPServer::SessionUnLock(Session* session)
{
	if (InterlockedDecrement(&session->ioCount) == 0)
		ReleaseSession(session);
}

void IOCPServer::PushIndex(INT64 index)
{
	std::lock_guard<std::mutex> lock(indexMutex);
	indexStack.push(index);
}

UINT64 IOCPServer::PopIndex()
{
	std::lock_guard<std::mutex> lock(indexMutex);

	UINT64 index = indexStack.top();
	indexStack.pop();

	return index;
}