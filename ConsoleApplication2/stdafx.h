#pragma once

// Windows 헤더 포함 전에 NOMINMAX로 매크로 충돌 방지
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <stdio.h>
#include <tchar.h>
#include <float.h>
#include <dbghelp.h>
#include <psapi.h>
#include <crtdbg.h>
#include <iostream>              
#include <mutex>                  
#include <atomic>                
#include <new.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "Psapi.lib")