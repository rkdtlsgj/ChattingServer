#pragma once
#include "stdafx.h"

#define PRO_BEGIN(TagName)		ProfileBegin(TagName)
#define PRO_END(TagName)		ProfileEnd(TagName)

#define dfTAG_NAME_MAX 64
#define dfTHREAD_MAX 50
#define dfSAMPLE_MAX 50

typedef struct
{
	bool				bFlag;

	WCHAR				szName[dfTAG_NAME_MAX];

	LARGE_INTEGER		lStartTime;;

	double				dTotalTime;

	double				dMin[2];
	double				dMax[2];

	__int64				iCall;
}PROFILE_SAMPLE;


struct st_THREAD_SAMPLE
{
	DWORD				lThreadID;
	PROFILE_SAMPLE* pSample;
};


bool ProfileBegin(WCHAR* szName);
bool ProfileEnd(WCHAR* szName);

bool ProfileInit();
bool SaveProfile();
bool GetSample(WCHAR* pszName, PROFILE_SAMPLE** pOutSample);

void ProfileReset();

