// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2016, LH_Mouse. All wrongs reserved.

#include "thread.h"
#include "mcfwin.h"
#include "heap.h"
#include "eh_top.h"
#include "_nt_timeout.h"
#include "../ext/assert.h"
#include <winternl.h>
#include <ntdef.h>

__attribute__((__dllimport__, __stdcall__))
extern NTSTATUS NtDelayExecution(BOOLEAN bAlertable, const LARGE_INTEGER *pInterval);
__attribute__((__dllimport__, __stdcall__))
extern NTSTATUS NtYieldExecution(void);

__attribute__((__dllimport__, __stdcall__))
extern NTSTATUS NtSuspendThread(HANDLE hThread, LONG *plPrevCount);
__attribute__((__dllimport__, __stdcall__))
extern NTSTATUS NtResumeThread(HANDLE hThread, LONG *plPrevCount);

_MCFCRT_ThreadHandle _MCFCRT_CreateNativeThread(_MCFCRT_NativeThreadProc pfnThreadProc, void *pParam, bool bSuspended, uintptr_t *restrict puThreadId){
	DWORD dwThreadId;
	const HANDLE hThread = CreateRemoteThread(GetCurrentProcess(), nullptr, 0, pfnThreadProc, pParam, bSuspended ? CREATE_SUSPENDED : 0, &dwThreadId);
	if(!hThread){
		return nullptr;
	}
	if(puThreadId){
		*puThreadId = dwThreadId;
	}
	return (_MCFCRT_ThreadHandle)hThread;
}

typedef struct tagThreadInitParams {
	_MCFCRT_ThreadProc pfnProc;
	intptr_t nParam;
} ThreadInitParams;

static __MCFCRT_C_STDCALL __MCFCRT_HAS_EH_TOP
DWORD CrtThreadProc(LPVOID pParam){
	const _MCFCRT_ThreadProc pfnProc = ((ThreadInitParams *)pParam)->pfnProc;
	const intptr_t           nParam  = ((ThreadInitParams *)pParam)->nParam;
	_MCFCRT_free(pParam);

	DWORD dwExitCode;

	__MCFCRT_EH_TOP_BEGIN
	{
		dwExitCode = (*pfnProc)(nParam);
	}
	__MCFCRT_EH_TOP_END

	return dwExitCode;
}

_MCFCRT_ThreadHandle _MCFCRT_CreateThread(_MCFCRT_ThreadProc pfnThreadProc, intptr_t nParam, bool bSuspended, uintptr_t *restrict puThreadId){
	ThreadInitParams *const pInitParams = _MCFCRT_malloc(sizeof(ThreadInitParams));
	if(!pInitParams){
		return nullptr;
	}
	pInitParams->pfnProc = pfnThreadProc;
	pInitParams->nParam  = nParam;

	const _MCFCRT_ThreadHandle hThread = _MCFCRT_CreateNativeThread(&CrtThreadProc, pInitParams, bSuspended, puThreadId);
	if(!hThread){
		const DWORD dwLastError = GetLastError();
		_MCFCRT_free(pInitParams);
		SetLastError(dwLastError);
		return nullptr;
	}
	return hThread;
}
void _MCFCRT_CloseThread(_MCFCRT_ThreadHandle hThread){
	const NTSTATUS lStatus = NtClose((HANDLE)hThread);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtClose() 失败。");
}

void _MCFCRT_Sleep(uint64_t u64UntilFastMonoClock){
	LARGE_INTEGER liTimeout;
	__MCF_CRT_InitializeNtTimeout(&liTimeout, u64UntilFastMonoClock);
	const NTSTATUS lStatus = NtDelayExecution(false, &liTimeout);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtDelayExecution() 失败。");
}
bool _MCFCRT_AlertableSleep(uint64_t u64UntilFastMonoClock){
	LARGE_INTEGER liTimeout;
	__MCF_CRT_InitializeNtTimeout(&liTimeout, u64UntilFastMonoClock);
	const NTSTATUS lStatus = NtDelayExecution(true, &liTimeout);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtDelayExecution() 失败。");
	if(lStatus == STATUS_TIMEOUT){
		return false;
	}
	return true;
}
void _MCFCRT_AlertableSleepForever(void){
	LARGE_INTEGER liTimeout;
	liTimeout.QuadPart = INT64_MAX;
	const NTSTATUS lStatus = NtDelayExecution(true, &liTimeout);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtDelayExecution() 失败。");
}
void _MCFCRT_YieldThread(void){
	const NTSTATUS lStatus = NtYieldExecution();
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtYieldExecution() 失败。");
}

long _MCFCRT_SuspendThread(_MCFCRT_ThreadHandle hThread){
	LONG lPrevCount;
	const NTSTATUS lStatus = NtSuspendThread((HANDLE)hThread, &lPrevCount);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtSuspendThread() 失败。");
	return lPrevCount;
}
long _MCFCRT_ResumeThread(_MCFCRT_ThreadHandle hThread){
	LONG lPrevCount;
	const NTSTATUS lStatus = NtResumeThread((HANDLE)hThread, &lPrevCount);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtResumeThread() 失败。");
	return lPrevCount;
}

bool _MCFCRT_WaitForThread(_MCFCRT_ThreadHandle hThread, uint64_t u64UntilFastMonoClock){
	LARGE_INTEGER liTimeout;
	__MCF_CRT_InitializeNtTimeout(&liTimeout, u64UntilFastMonoClock);
	const NTSTATUS lStatus = NtWaitForSingleObject((HANDLE)hThread, false, &liTimeout);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtWaitForSingleObject() 失败。");
	if(lStatus == STATUS_TIMEOUT){
		return false;
	}
	return true;
}
void _MCFCRT_WaitForThreadForever(_MCFCRT_ThreadHandle hThread){
	const NTSTATUS lStatus = NtWaitForSingleObject((HANDLE)hThread, false, nullptr);
	_MCFCRT_ASSERT_MSG(NT_SUCCESS(lStatus), L"NtWaitForSingleObject() 失败。");
}

uintptr_t _MCFCRT_GetCurrentThreadId(void){
	return GetCurrentThreadId();
}
