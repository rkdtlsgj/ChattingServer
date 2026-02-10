#include "CpuMonitor.h"
#include <vector>
#include <string>

static bool PdhOk(PDH_STATUS s) { return s == ERROR_SUCCESS; }

CpuMonitor::CpuMonitor(HANDLE processHandle)
{
	if (processHandle == INVALID_HANDLE_VALUE || processHandle == nullptr)
		mProcessHandle = GetCurrentProcess();
	else
		mProcessHandle = processHandle;

	mPid = GetProcessId(mProcessHandle);

	SYSTEM_INFO si{};
	GetSystemInfo(&si);
	mNumberOfProcessors = (int)si.dwNumberOfProcessors;

	// PDH Query
	if (!PdhOk(PdhOpenQueryW(nullptr, 0, &mQuery)))
	{
		mQuery = nullptr;
		return;
	}

	if (!ResolvePdhProcessInstanceByPid(mPid, mPdhProcessInstance, _countof(mPdhProcessInstance)))
	{
		WCHAR exeBase[260]{};
		if (GetExeBaseNameFromHandle(mProcessHandle, exeBase, _countof(exeBase)))
			wcscpy_s(mPdhProcessInstance, exeBase);
		else
			wcscpy_s(mPdhProcessInstance, L"");
	}

	AddProcessCounters();
	AddEthernetCounters();

	// PDH는 rate counter(*/sec)가 "두 번의 Collect"가 있어야 의미있는 값이 나오기도 함
	// 첫 Update에서 네트워크는 0일 수 있음.
	PdhCollectQueryData(mQuery);

	// 첫 샘플에서 CPU 초기화만 하도록 Update에서 처리
}

CpuMonitor::~CpuMonitor()
{
	if (mQuery)
	{
		PdhCloseQuery(mQuery);
		mQuery = nullptr;
	}
}

bool CpuMonitor::GetExeBaseNameFromHandle(HANDLE hProcess, WCHAR* outName, size_t outCount)
{
	if (!outName || outCount == 0) return false;

	WCHAR full[MAX_PATH]{};
	DWORD sz = (DWORD)_countof(full);

	// PROCESS_QUERY_LIMITED_INFORMATION 권한이 있는 handle이면 동작
	if (!QueryFullProcessImageNameW(hProcess, 0, full, &sz))
		return false;

	// basename만 추출
	const WCHAR* base = full;
	for (const WCHAR* p = full; *p; ++p)
		if (*p == L'\\') base = p + 1;

	WCHAR tmp[MAX_PATH]{};
	wcscpy_s(tmp, base);

	// 확장자 제거
	for (int i = (int)wcslen(tmp) - 1; i >= 0; --i)
	{
		if (tmp[i] == L'.') { tmp[i] = L'\0'; break; }
		if (tmp[i] == L'\\') break;
	}

	wcscpy_s(outName, outCount, tmp);
	return true;
}

// PID로 PDH Process 인스턴스명 찾아오기
// 방식: Process 객체의 인스턴스들을 열거 -> 각 인스턴스의 "ID Process"를 읽어서 pid 매칭
bool CpuMonitor::ResolvePdhProcessInstanceByPid(DWORD pid, WCHAR* outInstance, size_t outCount)
{
	if (!outInstance || outCount == 0) return false;

	DWORD counterSize = 0, instSize = 0;
	PDH_STATUS s = PdhEnumObjectItemsW(
		nullptr, nullptr,
		L"Process",
		nullptr, &counterSize,
		nullptr, &instSize,
		PERF_DETAIL_WIZARD,
		0
	);

	if (s != PDH_MORE_DATA || instSize == 0)
		return false;

	std::vector<WCHAR> counters(counterSize);
	std::vector<WCHAR> insts(instSize);

	s = PdhEnumObjectItemsW(
		nullptr, nullptr,
		L"Process",
		counters.data(), &counterSize,
		insts.data(), &instSize,
		PERF_DETAIL_WIZARD,
		0
	);

	if (!PdhOk(s))
		return false;

	// 임시 Query로 ID Process 카운터를 하나씩 확인
	PDH_HQUERY q = nullptr;
	if (!PdhOk(PdhOpenQueryW(nullptr, 0, &q)))
		return false;

	bool found = false;

	for (WCHAR* p = insts.data(); *p != L'\0'; p += wcslen(p) + 1)
	{
		// Idle / _Total 같은 특수 인스턴스는 skip
		if (_wcsicmp(p, L"_Total") == 0 || _wcsicmp(p, L"Idle") == 0)
			continue;

		WCHAR path[512]{};
		swprintf_s(path, L"\\Process(%s)\\ID Process", p);

		PDH_HCOUNTER c = nullptr;
		if (!PdhOk(PdhAddCounterW(q, path, 0, &c)))
			continue;

		// Collect
		PdhCollectQueryData(q);

		PDH_FMT_COUNTERVALUE v{};
		if (PdhOk(PdhGetFormattedCounterValue(c, PDH_FMT_LONG, nullptr, &v)))
		{
			if ((DWORD)v.longValue == pid)
			{
				wcscpy_s(outInstance, outCount, p);
				found = true;
				break;
			}
		}

		// 다음 인스턴스 확인을 위해 Query 리셋(간단하게 닫고 다시 열어도 되지만)
		// 여기서는 RemoveCounter가 없어도 CloseQuery로 한번에 정리하므로 그냥 진행.
	}

	PdhCloseQuery(q);
	return found;
}

bool CpuMonitor::AddProcessCounters()
{
	if (!mQuery) return false;
	if (mPdhProcessInstance[0] == L'\0') return false;

	WCHAR path[512]{};

	swprintf_s(path, L"\\Process(%s)\\Private Bytes", mPdhProcessInstance);
	if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mProcessUserMemoryCounter))) return false;

	swprintf_s(path, L"\\Process(%s)\\Pool Nonpaged Bytes", mPdhProcessInstance);
	if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mProcessNonPagedMemoryCounter))) return false;

	swprintf_s(path, L"\\Process(%s)\\Handle Count", mPdhProcessInstance);
	if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mProcessHandleCountCounter))) return false;

	swprintf_s(path, L"\\Process(%s)\\Thread Count", mPdhProcessInstance);
	if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mProcessThreadCountCounter))) return false;

	if (!PdhOk(PdhAddCounterW(mQuery, L"\\Memory\\Available MBytes", 0, &mAvailableMemoryCounter))) return false;
	if (!PdhOk(PdhAddCounterW(mQuery, L"\\Memory\\Pool Nonpaged Bytes", 0, &mNonPagedMemoryCounter))) return false;

	return true;
}

bool CpuMonitor::AddEthernetCounters()
{
	if (!mQuery) return false;

	DWORD counterSize = 0, instSize = 0;
	PDH_STATUS s = PdhEnumObjectItemsW(
		nullptr, nullptr,
		L"Network Interface",
		nullptr, &counterSize,
		nullptr, &instSize,
		PERF_DETAIL_WIZARD,
		0
	);

	if (s != PDH_MORE_DATA || instSize == 0)
		return false;

	std::vector<WCHAR> counters(counterSize);
	std::vector<WCHAR> insts(instSize);

	s = PdhEnumObjectItemsW(
		nullptr, nullptr,
		L"Network Interface",
		counters.data(), &counterSize,
		insts.data(), &instSize,
		PERF_DETAIL_WIZARD,
		0
	);
	if (!PdhOk(s))
		return false;

	int idx = 0;
	for (WCHAR* p = insts.data(); *p != L'\0' && idx < PDH_ETHERNET_MAX; p += wcslen(p) + 1)
	{
		// 이름 저장
		mNics[idx].bUse = true;
		wcscpy_s(mNics[idx].szName, p);

		WCHAR path[512]{};

		swprintf_s(path, L"\\Network Interface(%s)\\Bytes Received/sec", p);
		if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mNics[idx].pdhRecv)))
			mNics[idx].bUse = false;

		swprintf_s(path, L"\\Network Interface(%s)\\Bytes Sent/sec", p);
		if (!PdhOk(PdhAddCounterW(mQuery, path, 0, &mNics[idx].pdhSend)))
			mNics[idx].bUse = false;

		++idx;
	}
	return true;
}

void CpuMonitor::Update()
{
	// ---------- System CPU ----------
	ULARGE_INTEGER idle{}, kernel{}, user{};
	if (GetSystemTimes((PFILETIME)&idle, (PFILETIME)&kernel, (PFILETIME)&user))
	{
		if (!mSysCpuInitialized)
		{
			mSysLastIdle = idle;
			mSysLastKernel = kernel;
			mSysLastUser = user;
			mSysCpuInitialized = true;
		}
		else
		{
			const ULONGLONG kDiff = kernel.QuadPart - mSysLastKernel.QuadPart;
			const ULONGLONG uDiff = user.QuadPart - mSysLastUser.QuadPart;
			const ULONGLONG iDiff = idle.QuadPart - mSysLastIdle.QuadPart;

			const ULONGLONG total = kDiff + uDiff;

			if (total == 0)
			{
				mProcessorTotal = mProcessorUser = mProcessorKernel = 0.0f;
			}
			else
			{
				mProcessorTotal = (float)(((double)(total - iDiff) / (double)total) * 100.0);
				mProcessorUser = (float)(((double)uDiff / (double)total) * 100.0);
				mProcessorKernel = (float)(((double)(kDiff - iDiff) / (double)total) * 100.0);
			}

			mSysLastIdle = idle;
			mSysLastKernel = kernel;
			mSysLastUser = user;
		}
	}

	// ---------- Process CPU ----------
	ULARGE_INTEGER nowTime{};
	GetSystemTimeAsFileTime((LPFILETIME)&nowTime);

	ULARGE_INTEGER pKernel{}, pUser{};
	ULARGE_INTEGER dummy{};
	if (GetProcessTimes(mProcessHandle, (LPFILETIME)&dummy, (LPFILETIME)&dummy, (LPFILETIME)&pKernel, (LPFILETIME)&pUser))
	{
		if (!mProcCpuInitialized)
		{
			mProcLastTime = nowTime;
			mProcLastKernel = pKernel;
			mProcLastUser = pUser;
			mProcCpuInitialized = true;
		}
		else
		{
			const ULONGLONG timeDiff = nowTime.QuadPart - mProcLastTime.QuadPart;
			const ULONGLONG userDiff = pUser.QuadPart - mProcLastUser.QuadPart;
			const ULONGLONG kernelDiff = pKernel.QuadPart - mProcLastKernel.QuadPart;
			const ULONGLONG total = userDiff + kernelDiff;

			if (timeDiff == 0 || mNumberOfProcessors <= 0)
			{
				mProcessTotal = mProcessUser = mProcessKernel = 0.0f;
			}
			else
			{
				const double denom = (double)mNumberOfProcessors * (double)timeDiff;
				mProcessTotal = (float)((double)total / denom * 100.0);
				mProcessUser = (float)((double)userDiff / denom * 100.0);
				mProcessKernel = (float)((double)kernelDiff / denom * 100.0);
			}

			mProcLastTime = nowTime;
			mProcLastKernel = pKernel;
			mProcLastUser = pUser;
		}
	}

	// ---------- PDH counters ----------
	if (!mQuery) return;

	PdhCollectQueryData(mQuery);

	PDH_FMT_COUNTERVALUE v{};

	// Private Bytes
	if (mProcessUserMemoryCounter && PdhOk(PdhGetFormattedCounterValue(mProcessUserMemoryCounter, PDH_FMT_LARGE, nullptr, &v)))
		mProcessUserMemory = v.largeValue;

	// Pool Nonpaged Bytes (process)
	if (mProcessNonPagedMemoryCounter && PdhOk(PdhGetFormattedCounterValue(mProcessNonPagedMemoryCounter, PDH_FMT_LARGE, nullptr, &v)))
		mProcessNonPagedMemory = v.largeValue;

	// Available MBytes
	if (mAvailableMemoryCounter && PdhOk(PdhGetFormattedCounterValue(mAvailableMemoryCounter, PDH_FMT_LARGE, nullptr, &v)))
		mAvailableMemoryMB = v.largeValue;

	// Pool Nonpaged Bytes (system)
	if (mNonPagedMemoryCounter && PdhOk(PdhGetFormattedCounterValue(mNonPagedMemoryCounter, PDH_FMT_LARGE, nullptr, &v)))
		mNonPagedMemoryBytes = v.largeValue;

	// Handle Count
	if (mProcessHandleCountCounter && PdhOk(PdhGetFormattedCounterValue(mProcessHandleCountCounter, PDH_FMT_LONG, nullptr, &v)))
		mProcessHandleCount = (DWORD)v.longValue;

	// Thread Count
	if (mProcessThreadCountCounter && PdhOk(PdhGetFormattedCounterValue(mProcessThreadCountCounter, PDH_FMT_LONG, nullptr, &v)))
		mProcessThreadCount = (DWORD)v.longValue;

	// ---------- Network (Bytes/sec) ----------
	mNetworkRecvBps = 0.0;
	mNetworkSendBps = 0.0;

	for (int i = 0; i < PDH_ETHERNET_MAX; ++i)
	{
		if (!mNics[i].bUse) continue;

		if (mNics[i].pdhRecv && PdhOk(PdhGetFormattedCounterValue(mNics[i].pdhRecv, PDH_FMT_DOUBLE, nullptr, &v)))
			mNetworkRecvBps += v.doubleValue;

		if (mNics[i].pdhSend && PdhOk(PdhGetFormattedCounterValue(mNics[i].pdhSend, PDH_FMT_DOUBLE, nullptr, &v)))
			mNetworkSendBps += v.doubleValue;
	}
}

void CpuMonitor::GetBigNumberStr(LONGLONG value, WCHAR* s, int size)
{
	if (!s || size <= 0) return;

	if (value < 0)
	{
		// 간단 처리(필요시 확장)
		swprintf_s(s, size, L"%lld", value);
		return;
	}

	int idx = 0;
	bool first = true;

	if (value >= 1000000000)
	{
		if (first) { idx += swprintf_s(s + idx, size - idx, L"%llu,", value / 1000000000); first = false; }
		else { idx += swprintf_s(s + idx, size - idx, L"%03llu,", value / 1000000000); }
	}
	value %= 1000000000;

	if (value >= 1000000)
	{
		if (first) { idx += swprintf_s(s + idx, size - idx, L"%llu,", value / 1000000); first = false; }
		else { idx += swprintf_s(s + idx, size - idx, L"%03llu,", value / 1000000); }
	}
	value %= 1000000;

	if (value >= 1000)
	{
		if (first) { idx += swprintf_s(s + idx, size - idx, L"%llu,", value / 1000); first = false; }
		else { idx += swprintf_s(s + idx, size - idx, L"%03llu,", value / 1000); }
	}

	if (first)
		swprintf_s(s + idx, size - idx, L"%llu", value % 1000);
	else
		swprintf_s(s + idx, size - idx, L"%03llu", value % 1000);
}