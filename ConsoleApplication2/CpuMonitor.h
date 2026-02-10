#pragma once
#define WIN32_LEAN_AND_MEAN

#pragma comment(lib, "pdh.lib")

#include <Windows.h>
#include <Pdh.h>
#include <PdhMsg.h>

class CpuMonitor
{
private:
	struct st_ETHERNET
	{
		bool        bUse = false;
		WCHAR       szName[128] = {};

		PDH_HCOUNTER pdhRecv = nullptr; // Bytes Received/sec
		PDH_HCOUNTER pdhSend = nullptr; // Bytes Sent/sec
	};

public:
	enum { PDH_ETHERNET_MAX = 8 };

	explicit CpuMonitor(HANDLE processHandle = INVALID_HANDLE_VALUE);
	~CpuMonitor();

	// 주기적으로 호출
	void Update();

	// System CPU (%)
	float ProcessorTotal()  const { return mProcessorTotal; }
	float ProcessorUser()   const { return mProcessorUser; }
	float ProcessorKernel() const { return mProcessorKernel; }

	// Process CPU (%)
	float ProcessTotal()  const { return mProcessTotal; }
	float ProcessUser()   const { return mProcessUser; }
	float ProcessKernel() const { return mProcessKernel; }

	// Memory / Handles / Threads
	LONGLONG ProcessUserMemory()     const { return mProcessUserMemory; }      // Private Bytes
	LONGLONG ProcessNonPagedMemory() const { return mProcessNonPagedMemory; }  // Pool Nonpaged Bytes
	LONGLONG AvailableMemoryMB()     const { return mAvailableMemoryMB; }      // Available MBytes
	LONGLONG NonPagedMemoryBytes()   const { return mNonPagedMemoryBytes; }    // Pool Nonpaged Bytes
	DWORD    ProcessHandleCount()    const { return mProcessHandleCount; }
	DWORD    ProcessThreadCount()    const { return mProcessThreadCount; }

	// Network (Bytes/sec) - 전체 NIC 합산
	double NetworkRecvBytesPerSec() const { return mNetworkRecvBps; }
	double NetworkSendBytesPerSec() const { return mNetworkSendBps; }

	static void GetBigNumberStr(LONGLONG value, WCHAR* s, int size);

private:
	bool ResolvePdhProcessInstanceByPid(DWORD pid, WCHAR* outInstance, size_t outCount);
	bool AddProcessCounters();
	bool AddEthernetCounters();

	static bool GetExeBaseNameFromHandle(HANDLE hProcess, WCHAR* outName, size_t outCount);

private:
	HANDLE mProcessHandle = INVALID_HANDLE_VALUE;
	DWORD  mPid = 0;
	int    mNumberOfProcessors = 0;

	// CPU 결과
	float mProcessorTotal = 0.0f;
	float mProcessorUser = 0.0f;
	float mProcessorKernel = 0.0f;

	float mProcessTotal = 0.0f;
	float mProcessUser = 0.0f;
	float mProcessKernel = 0.0f;

	// 이전 샘플 (System)
	ULARGE_INTEGER mSysLastKernel{};
	ULARGE_INTEGER mSysLastUser{};
	ULARGE_INTEGER mSysLastIdle{};
	bool mSysCpuInitialized = false;

	// 이전 샘플 (Process)
	ULARGE_INTEGER mProcLastKernel{};
	ULARGE_INTEGER mProcLastUser{};
	ULARGE_INTEGER mProcLastTime{};
	bool mProcCpuInitialized = false;

	// PDH
	PDH_HQUERY   mQuery = nullptr;
	WCHAR        mPdhProcessInstance[260] = {};

	PDH_HCOUNTER mProcessUserMemoryCounter = nullptr;
	LONGLONG     mProcessUserMemory = 0;

	PDH_HCOUNTER mProcessNonPagedMemoryCounter = nullptr;
	LONGLONG     mProcessNonPagedMemory = 0;

	PDH_HCOUNTER mAvailableMemoryCounter = nullptr;
	LONGLONG     mAvailableMemoryMB = 0;

	PDH_HCOUNTER mNonPagedMemoryCounter = nullptr;
	LONGLONG     mNonPagedMemoryBytes = 0;

	PDH_HCOUNTER mProcessHandleCountCounter = nullptr;
	DWORD        mProcessHandleCount = 0;

	PDH_HCOUNTER mProcessThreadCountCounter = nullptr;
	DWORD        mProcessThreadCount = 0;

	// Network
	st_ETHERNET  mNics[PDH_ETHERNET_MAX]{};
	double       mNetworkRecvBps = 0.0;
	double       mNetworkSendBps = 0.0;
};