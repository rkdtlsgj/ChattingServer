#include "monitor.h"


DWORD _dwTlsIndex; //tls ÀÎµ¦½º
st_THREAD_SAMPLE* _stProfileThread;
LARGE_INTEGER			_IFrequency;
double					_dMicroFrequency;

bool ProfileInit()
{
	QueryPerformanceFrequency(&_IFrequency);
	_dMicroFrequency = (double)_IFrequency.QuadPart / (double)1000000;


	_stProfileThread = new st_THREAD_SAMPLE[dfTHREAD_MAX];
	ZeroMemory(_stProfileThread, sizeof(st_THREAD_SAMPLE) * dfTHREAD_MAX);

	_dwTlsIndex = TlsAlloc();
	if (TLS_OUT_OF_INDEXES == _dwTlsIndex)
	{
		return false;
	}

	return true;
}


bool ProfileBegin(WCHAR* szName)
{
	LARGE_INTEGER	lStartTime;
	PROFILE_SAMPLE* pSample = NULL;

	GetSample(szName, &pSample);
	if (pSample == NULL)
		return false;

	QueryPerformanceCounter(&lStartTime);

	if (pSample->lStartTime.QuadPart != 0)
		return false;

	pSample->lStartTime = lStartTime;

	return true;
}


bool ProfileEnd(WCHAR* szName)
{
	LARGE_INTEGER lEndTime;
	LARGE_INTEGER lSampleTime;
	PROFILE_SAMPLE* pSample = NULL;
	DWORD dwTlsIndex = -1;


	QueryPerformanceCounter(&lEndTime);

	GetSample(szName, &pSample);
	if (pSample == NULL)
		return false;

	lSampleTime.QuadPart = lEndTime.QuadPart - pSample->lStartTime.QuadPart;

	if (pSample->dMax[1] < lSampleTime.QuadPart)
	{
		pSample->dMax[0] = pSample->dMax[1];
		pSample->dMax[1] = (double)lSampleTime.QuadPart / _dMicroFrequency;
	}

	if (pSample->dMin[1] > lSampleTime.QuadPart)
	{
		pSample->dMin[0] = pSample->dMin[1];
		pSample->dMin[1] = (double)lSampleTime.QuadPart / _dMicroFrequency;
	}

	pSample->dTotalTime += (double)lSampleTime.QuadPart / _dMicroFrequency;
	pSample->iCall++;
	pSample->lStartTime.QuadPart = 0;

	return true;
}



bool GetSample(WCHAR* pszName, PROFILE_SAMPLE** pOutSample)
{
	PROFILE_SAMPLE* pSample;

	pSample = (PROFILE_SAMPLE*)TlsGetValue(_dwTlsIndex);
	if (pSample == NULL)
	{
		pSample = new PROFILE_SAMPLE[dfSAMPLE_MAX];

		for (int iCnt = 0; iCnt < dfSAMPLE_MAX; iCnt++)
			memset(&pSample[iCnt], 0, sizeof(dfSAMPLE_MAX));

		if (TlsSetValue(_dwTlsIndex, (LPVOID)pSample) == false)
			return false;

		for (int iCnt = 0; iCnt < dfSAMPLE_MAX; iCnt++)
		{
			if ((_stProfileThread[iCnt].lThreadID == 0) && (_stProfileThread[iCnt].pSample == NULL))
			{
				_stProfileThread[iCnt].pSample = pSample;
				_stProfileThread[iCnt].lThreadID = GetCurrentThreadId();
				//pOutSample = pSample;
				break;
			}
		}
	}

	for (int iCnt = 0; iCnt < dfSAMPLE_MAX; iCnt++)
	{
		if ((pSample[iCnt].bFlag) && wcscmp(pSample[iCnt].szName, pszName) == 0)
		{
			*pOutSample = &pSample[iCnt];
			break;
		}

		if (wcscmp(pSample[iCnt].szName, L"") == 0)
		{
			wsprintf(pSample[iCnt].szName, pszName);

			pSample[iCnt].bFlag = true;

			pSample[iCnt].lStartTime.QuadPart = 0;
			pSample[iCnt].dTotalTime = 0;

			pSample[iCnt].dMax[0] = 0;
			pSample[iCnt].dMin[0] = DBL_MAX;

			pSample[iCnt].dMax[1] = 0;
			pSample[iCnt].dMin[1] = DBL_MAX;

			pSample[iCnt].iCall = 0;

			*pOutSample = &pSample[iCnt];
			break;
		}
	}

	return true;
}



bool SaveProfile()
{
	SYSTEMTIME stNowTime;
	WCHAR fileName[256];
	WCHAR wBuffer[500];
	FILE* fp;

	GetLocalTime(&stNowTime);
	wsprintf(fileName, L"%04d-%02d-%02d %02d_%02d ProFiling.txt", stNowTime.wYear, stNowTime.wMonth, stNowTime.wDay, stNowTime.wHour, stNowTime.wMinute);

	_wfopen_s(&fp, fileName, L"ab");
	fseek(fp, 0, SEEK_END);

	fwprintf(fp, L"------------------------------------------------------------------------------------------------------------ - \r\n");
	fwprintf(fp, L"ThreadID |                Name  |           Average  |            Min   |            Max   |          Call |\r\n");
	fwprintf(fp, L"------------------------------------------------------------------------------------------------------------ - \r\n");

	for (int iThCnt = 0; dfTHREAD_MAX; iThCnt++)
	{
		if (_stProfileThread[iThCnt].lThreadID == 0)
			break;

		for (int iSampleCnt = 0; iSampleCnt < dfSAMPLE_MAX; iSampleCnt++)
		{
			if (_stProfileThread[iThCnt].pSample[iSampleCnt].bFlag == false)
				break;

			swprintf_s(wBuffer, L"%8d | %20s | %15.4lf §Á | %13.4lf §Á | %13.4lf §Á | %13lld |\r\n",
				_stProfileThread[iThCnt].lThreadID,
				_stProfileThread[iThCnt].pSample[iSampleCnt].szName,
				(_stProfileThread[iThCnt].pSample[iSampleCnt].dTotalTime / _stProfileThread[iThCnt].pSample[iSampleCnt].iCall),
				_stProfileThread[iThCnt].pSample[iSampleCnt].dMin[1],
				_stProfileThread[iThCnt].pSample[iSampleCnt].dMax[1],
				_stProfileThread[iThCnt].pSample[iSampleCnt].iCall);

			fwprintf(fp, L"%s", wBuffer);
		}

		fwprintf(fp, L"------------------------------------------------------------------------------------------------------------ - \r\n");
	}

	return true;
}

