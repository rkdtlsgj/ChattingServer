#include "stdafx.h"
#include "ChatServer.h"

int main() 
{
    ChatServer* server = new ChatServer();
    server->Start(L"127.0.0.1", 6000, 4, true,5000);
    while (1)
    {

    }
    return 0;
}