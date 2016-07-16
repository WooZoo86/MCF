// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2016, LH_Mouse. All wrongs reserved.

#include "thread_env.h"
#include "mcfwin.h"
#include "mutex.h"
#include "condition_variable.h"
#include "thread.h"
#include "_seh_top.h"
#include "avl_tree.h"
#include "heap.h"
#include "../ext/assert.h"

static _MCFCRT_Mutex      g_vControlMutex = { 0 };
static _MCFCRT_AvlRoot    g_avlControls   = nullptr;

static DWORD              g_dwTlsIndex    = TLS_OUT_OF_INDEXES;
static volatile uintptr_t g_uKeyCounter   = 0;

bool __MCFCRT_ThreadEnvInit(void){
	const DWORD dwTlsIndex = TlsAlloc();
	if(dwTlsIndex == TLS_OUT_OF_INDEXES){
		return false;
	}

	g_dwTlsIndex = dwTlsIndex;
	return true;
}
void __MCFCRT_ThreadEnvUninit(void){
	const DWORD dwTlsIndex = g_dwTlsIndex;
	g_dwTlsIndex = TLS_OUT_OF_INDEXES;

	const bool bSucceeded = TlsFree(dwTlsIndex);
	_MCFCRT_ASSERT(bSucceeded);
}

typedef enum tagMopthreadState {
	kStateJoinable,
	kStateZombie,
	kStateJoining,
	kStateJoined,
	kStateDetached,
} MopthreadState;

typedef struct tagMopthreadControl {
	_MCFCRT_AvlNodeHeader avlhTidIndex;

	MopthreadState eState;
	_MCFCRT_ConditionVariable vTermination;

	uintptr_t uTid;
	_MCFCRT_ThreadHandle hThread;

	void (*pfnProc)(void *);
	size_t uSizeOfParams;
	unsigned char abyParams[];
} MopthreadControl;

static inline int MopthreadControlComparatorNodeKey(const _MCFCRT_AvlNodeHeader *lhs, intptr_t rhs){
	const uintptr_t u1 = (uintptr_t)(((const MopthreadControl *)lhs)->uTid);
	const uintptr_t u2 = (uintptr_t)rhs;
	if(u1 != u2){
		return (u1 < u2) ? -1 : 1;
	}
	return 0;
}
static inline int MopthreadControlComparatorNodes(const _MCFCRT_AvlNodeHeader *lhs, const _MCFCRT_AvlNodeHeader *rhs){
	return MopthreadControlComparatorNodeKey(lhs, (intptr_t)(((const MopthreadControl *)rhs)->uTid));
}

static intptr_t UnlockCallbackNative(intptr_t nContext){
	_MCFCRT_Mutex *const pMutex = (_MCFCRT_Mutex *)nContext;

	_MCFCRT_SignalMutex(pMutex);
	return 1;
}
static void RelockCallbackNative(intptr_t nContext, intptr_t nUnlocked){
	_MCFCRT_Mutex *const pMutex = (_MCFCRT_Mutex *)nContext;

	_MCFCRT_ASSERT((size_t)nUnlocked == 1);
	_MCFCRT_WaitForMutexForever(pMutex, _MCFCRT_MUTEX_SUGGESTED_SPIN_COUNT);
}

__attribute__((__noreturn__))
static void SignalMutexAndExitThread(_MCFCRT_Mutex *restrict pMutex, MopthreadControl *restrict pControl, void (*pfnModifier)(void *, intptr_t), intptr_t nContext){
	if(pControl){
		switch(pControl->eState){
		case kStateJoinable:
			if(pfnModifier){
				(*pfnModifier)(pControl->abyParams, nContext);
			}
			pControl->eState = kStateZombie;
			break;
		case kStateZombie:
			_MCFCRT_ASSERT(false);
		case kStateJoining:
			if(pfnModifier){
				(*pfnModifier)(pControl->abyParams, nContext);
			}
			pControl->eState = kStateJoined;
			_MCFCRT_BroadcastConditionVariable(&(pControl->vTermination));
			break;
		case kStateJoined:
			_MCFCRT_ASSERT(false);
		case kStateDetached:
			pControl->eState = kStateJoined;
			_MCFCRT_AvlDetach((_MCFCRT_AvlNodeHeader *)pControl);
			_MCFCRT_CloseThread(pControl->hThread);
			_MCFCRT_free(pControl);
			break;
		default:
			_MCFCRT_ASSERT(false);
		}
	}
	_MCFCRT_SignalMutex(pMutex);

	ExitThread(0);
	__builtin_trap();
}

__MCFCRT_C_STDCALL __attribute__((__noreturn__))
static unsigned long MopthreadProcNative(void *pParam){
	MopthreadControl *const pControl = pParam;
	_MCFCRT_ASSERT(pControl);

	__MCFCRT_SEH_TOP_BEGIN
	{
		(*(pControl->pfnProc))(pControl->abyParams);
	}
	__MCFCRT_SEH_TOP_END

	SignalMutexAndExitThread(&g_vControlMutex, pControl, nullptr, 0);
}

uintptr_t __MCFCRT_MopthreadCreate(void (*pfnProc)(void *), const void *pParams, size_t uSizeOfParams){
	const size_t uSizeToAlloc = sizeof(MopthreadControl) + uSizeOfParams;
	if(uSizeToAlloc < sizeof(MopthreadControl)){
		return 0;
	}
	MopthreadControl *const pControl = _MCFCRT_malloc(uSizeToAlloc);
	if(!pControl){
		return 0;
	}
	pControl->pfnProc       = pfnProc;
	pControl->uSizeOfParams = uSizeOfParams;
	if(pParams){
		memcpy(pControl->abyParams, pParams, uSizeOfParams);
	} else {
		memset(pControl->abyParams, 0, uSizeOfParams);
	}
	pControl->eState = kStateJoinable;
	_MCFCRT_InitializeConditionVariable(&(pControl->vTermination));

	uintptr_t uTid;
	const _MCFCRT_ThreadHandle hThread = _MCFCRT_CreateNativeThread(&MopthreadProcNative, pControl, true, &uTid);
	if(!hThread){
		_MCFCRT_free(pControl);
		return 0;
	}
	pControl->uTid    = uTid;
	pControl->hThread = hThread;

	_MCFCRT_WaitForMutexForever(&g_vControlMutex, _MCFCRT_MUTEX_SUGGESTED_SPIN_COUNT);
	{
		_MCFCRT_AvlAttach(&g_avlControls, (_MCFCRT_AvlNodeHeader *)pControl, &MopthreadControlComparatorNodes);
	}
	_MCFCRT_SignalMutex(&g_vControlMutex);

	_MCFCRT_ResumeThread(hThread);
	return uTid;
}
void __MCFCRT_MopthreadExit(void (*pfnModifier)(void *, intptr_t), intptr_t nContext){
	const uintptr_t uTid = _MCFCRT_GetCurrentThreadId();

	_MCFCRT_WaitForMutexForever(&g_vControlMutex, _MCFCRT_MUTEX_SUGGESTED_SPIN_COUNT);
	MopthreadControl *const pControl = (MopthreadControl *)_MCFCRT_AvlFind(&g_avlControls, (intptr_t)uTid, &MopthreadControlComparatorNodeKey);
	SignalMutexAndExitThread(&g_vControlMutex, pControl, pfnModifier, nContext);
}
bool __MCFCRT_MopthreadJoin(uintptr_t uTid, void *restrict pParams){
	bool bJoined = false;

	_MCFCRT_WaitForMutexForever(&g_vControlMutex, _MCFCRT_MUTEX_SUGGESTED_SPIN_COUNT);
	{
		MopthreadControl *const pControl = (MopthreadControl *)_MCFCRT_AvlFind(&g_avlControls, (intptr_t)uTid, &MopthreadControlComparatorNodeKey);
		if(pControl){
			switch(pControl->eState){
			case kStateJoinable:
				pControl->eState = kStateJoining;
				do {
					_MCFCRT_WaitForConditionVariableForever(&(pControl->vTermination), &UnlockCallbackNative, &RelockCallbackNative, (intptr_t)&g_vControlMutex);
				} while(pControl->eState != kStateJoined);
				bJoined = true;
				_MCFCRT_AvlDetach((_MCFCRT_AvlNodeHeader *)pControl);
				_MCFCRT_WaitForThreadForever(pControl->hThread);
				if(pParams){
					memcpy(pParams, pControl->abyParams, pControl->uSizeOfParams);
				}
				_MCFCRT_CloseThread(pControl->hThread);
				_MCFCRT_free(pControl);
				break;
			case kStateZombie:
				pControl->eState = kStateJoined;
				bJoined = true;
				_MCFCRT_AvlDetach((_MCFCRT_AvlNodeHeader *)pControl);
				_MCFCRT_WaitForThreadForever(pControl->hThread);
				if(pParams){
					memcpy(pParams, pControl->abyParams, pControl->uSizeOfParams);
				}
				_MCFCRT_CloseThread(pControl->hThread);
				_MCFCRT_free(pControl);
				break;
			case kStateJoining:
			case kStateJoined:
			case kStateDetached:
				break;
			default:
				_MCFCRT_ASSERT(false);
			}
		}
	}
	_MCFCRT_SignalMutex(&g_vControlMutex);

	return bJoined;
}
bool __MCFCRT_MopthreadDetach(uintptr_t uTid){
	bool bDetached = false;

	_MCFCRT_WaitForMutexForever(&g_vControlMutex, _MCFCRT_MUTEX_SUGGESTED_SPIN_COUNT);
	{
		MopthreadControl *const pControl = (MopthreadControl *)_MCFCRT_AvlFind(&g_avlControls, (intptr_t)uTid, &MopthreadControlComparatorNodeKey);
		if(pControl){
			switch(pControl->eState){
			case kStateJoinable:
				pControl->eState = kStateDetached;
				bDetached = true;
				break;
			case kStateZombie:
				pControl->eState = kStateJoined;
				bDetached = true;
				_MCFCRT_AvlDetach((_MCFCRT_AvlNodeHeader *)pControl);
				_MCFCRT_CloseThread(pControl->hThread);
				_MCFCRT_free(pControl);
				break;
			case kStateJoining:
			case kStateJoined:
			case kStateDetached:
				break;
			default:
				_MCFCRT_ASSERT(false);
			}
		}
	}
	_MCFCRT_SignalMutex(&g_vControlMutex);

	return bDetached;
}

typedef struct tagTlsObject TlsObject;
typedef struct tagTlsThread TlsThread;
typedef struct tagTlsKey    TlsKey;

typedef struct tagTlsObjectKey {
	TlsKey *pKey;
	uintptr_t uCounter;
} TlsObjectKey;

struct tagTlsObject {
	_MCFCRT_AvlNodeHeader avlhNodeByKey;
	TlsObjectKey vObjectKey;

	_MCFCRT_TlsDestructor pfnDestructor;
	intptr_t nContext;

	TlsThread *pThread;
	TlsObject *pPrevByThread;
	TlsObject *pNextByThread;

	alignas(max_align_t) unsigned char abyStorage[];
};

static inline int TlsObjectComparatorNodeKey(const _MCFCRT_AvlNodeHeader *pObj1, intptr_t nKey2){
	const TlsObjectKey *const pIndex1 = &((const TlsObject *)pObj1)->vObjectKey;
	const TlsObjectKey *const pIndex2 = (const TlsObjectKey *)nKey2;
	if(pIndex1->pKey != pIndex2->pKey){
		return (pIndex1->pKey < pIndex2->pKey) ? -1 : 1;
	}
	if(pIndex1->uCounter != pIndex2->uCounter){
		return (pIndex1->uCounter < pIndex2->uCounter) ? -1 : 1;
	}
	return 0;
}
static inline int TlsObjectComparatorNodes(const _MCFCRT_AvlNodeHeader *pObj1, const _MCFCRT_AvlNodeHeader *pObj2){
	return TlsObjectComparatorNodeKey(pObj1, (intptr_t)&((const TlsObject *)pObj2)->vObjectKey);
}

struct tagTlsThread {
	_MCFCRT_AvlRoot avlObjects;
	TlsObject *pFirstByThread;
	TlsObject *pLastByThread;
};

static TlsThread *GetTlsForCurrentThread(void){
	_MCFCRT_ASSERT(g_dwTlsIndex != TLS_OUT_OF_INDEXES);

	TlsThread *const pThread = TlsGetValue(g_dwTlsIndex);
	return pThread;
}
static TlsThread *RequireTlsForCurrentThread(void){
	TlsThread *pThread = GetTlsForCurrentThread();
	if(!pThread){
		pThread = _MCFCRT_malloc(sizeof(TlsThread));
		if(!pThread){
			return nullptr;
		}
		pThread->avlObjects     = nullptr;
		pThread->pFirstByThread = nullptr;
		pThread->pLastByThread  = nullptr;

		if(!TlsSetValue(g_dwTlsIndex, pThread)){
			const DWORD dwErrorCode = GetLastError();
			_MCFCRT_free(pThread);
			SetLastError(dwErrorCode);
			return nullptr;
		}
	}
	return pThread;
}

struct tagTlsKey {
	uintptr_t uCounter;

	size_t uSize;
	_MCFCRT_TlsConstructor pfnConstructor;
	_MCFCRT_TlsDestructor pfnDestructor;
	intptr_t nContext;
};

static TlsObject *GetTlsObject(TlsThread *pThread, TlsKey *pKey){
	_MCFCRT_ASSERT(pThread);

	if(!pKey){
		return nullptr;
	}

	const TlsObjectKey vObjectKey = { pKey, pKey->uCounter };
	TlsObject *const pObject = (TlsObject *)_MCFCRT_AvlFind(&(pThread->avlObjects), (intptr_t)&vObjectKey, &TlsObjectComparatorNodeKey);
	return pObject;
}
static TlsObject *RequireTlsObject(TlsThread *pThread, TlsKey *pKey, size_t uSize, _MCFCRT_TlsConstructor pfnConstructor, _MCFCRT_TlsDestructor pfnDestructor, intptr_t nContext){
	TlsObject *pObject = GetTlsObject(pThread, pKey);
	if(!pObject){
		const size_t uSizeToAlloc = sizeof(TlsObject) + uSize;
		if(uSizeToAlloc < sizeof(TlsObject)){
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return nullptr;
		}
		pObject = _MCFCRT_malloc(uSizeToAlloc);
		if(!pObject){
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return nullptr;
		}
#ifndef NDEBUG
		memset(pObject, 0xAA, sizeof(TlsObject));
#endif
		memset(pObject->abyStorage, 0, uSize);

		if(pfnConstructor){
			const DWORD dwErrorCode = (*pfnConstructor)(nContext, pObject->abyStorage);
			if(dwErrorCode != 0){
				_MCFCRT_free(pObject);
				SetLastError(dwErrorCode);
				return nullptr;
			}
		}

		pObject->pfnDestructor = pfnDestructor;
		pObject->nContext      = nContext;

		TlsObject *const pPrev = pThread->pLastByThread;
		TlsObject *const pNext = nullptr;
		if(pPrev){
			pPrev->pNextByThread = pObject;
		} else {
			pThread->pFirstByThread = pObject;
		}
		if(pNext){
			pNext->pPrevByThread = pObject;
		} else {
			pThread->pLastByThread = pObject;
		}
		pObject->pThread = pThread;
		pObject->pPrevByThread = pPrev;
		pObject->pNextByThread = pNext;

		if(pKey){
			pObject->vObjectKey.pKey     = pKey;
			pObject->vObjectKey.uCounter = pKey->uCounter;
			_MCFCRT_AvlAttach(&(pThread->avlObjects), (_MCFCRT_AvlNodeHeader *)pObject, &TlsObjectComparatorNodes);
		}
	}
	return pObject;
}

void __MCFCRT_TlsCleanup(void){
	TlsThread *const pThread = GetTlsForCurrentThread();
	if(!pThread){
		return;
	}

	for(;;){
		TlsObject *pObject;
		{
			pObject = pThread->pLastByThread;
			if(pObject){
				TlsObject *const pPrev = pObject->pPrevByThread;
				if(pPrev){
					pPrev->pNextByThread = nullptr;
				}
				pThread->pLastByThread = pPrev;
			}
		}
		if(!pObject){
			break;
		}

		const _MCFCRT_TlsDestructor pfnDestructor = pObject->pfnDestructor;
		if(pfnDestructor){
			(*pfnDestructor)(pObject->nContext, pObject->abyStorage);
		}
		_MCFCRT_free(pObject);
	}

	const bool bSucceeded = TlsSetValue(g_dwTlsIndex, nullptr);
	_MCFCRT_ASSERT(bSucceeded);
	_MCFCRT_free(pThread);
}

_MCFCRT_TlsKeyHandle _MCFCRT_TlsAllocKey(size_t uSize, _MCFCRT_TlsConstructor pfnConstructor, _MCFCRT_TlsDestructor pfnDestructor, intptr_t nContext){
	TlsKey *const pKey = _MCFCRT_malloc(sizeof(TlsKey));
	if(!pKey){
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return nullptr;
	}
	pKey->uCounter       = __atomic_add_fetch(&g_uKeyCounter, 1, __ATOMIC_RELAXED);
	pKey->uSize          = uSize;
	pKey->pfnConstructor = pfnConstructor;
	pKey->pfnDestructor  = pfnDestructor;
	pKey->nContext       = nContext;

	return (_MCFCRT_TlsKeyHandle)pKey;
}
void _MCFCRT_TlsFreeKey(_MCFCRT_TlsKeyHandle hTlsKey){
	TlsKey *const pKey = (TlsKey *)hTlsKey;
	_MCFCRT_free(pKey);
}

size_t _MCFCRT_TlsGetSize(_MCFCRT_TlsKeyHandle hTlsKey){
	TlsKey *const pKey = (TlsKey *)hTlsKey;
	return pKey->uSize;
}
_MCFCRT_TlsConstructor _MCFCRT_TlsGetConstructor(_MCFCRT_TlsKeyHandle hTlsKey){
	TlsKey *const pKey = (TlsKey *)hTlsKey;
	return pKey->pfnConstructor;
}
_MCFCRT_TlsDestructor _MCFCRT_TlsGetDestructor(_MCFCRT_TlsKeyHandle hTlsKey){
	TlsKey *const pKey = (TlsKey *)hTlsKey;
	return pKey->pfnDestructor;
}
intptr_t _MCFCRT_TlsGetContext(_MCFCRT_TlsKeyHandle hTlsKey){
	TlsKey *const pKey = (TlsKey *)hTlsKey;
	return pKey->nContext;
}

bool _MCFCRT_TlsGet(_MCFCRT_TlsKeyHandle hTlsKey, void **restrict ppStorage){
#ifndef NDEBUG
	*ppStorage = (void *)0xDEADBEEF;
#endif

	TlsKey *const pKey = (TlsKey *)hTlsKey;
	if(!pKey){
		SetLastError(ERROR_INVALID_PARAMETER);
		return false;
	}
	TlsThread *const pThread = GetTlsForCurrentThread();
	if(!pThread){
		*ppStorage = nullptr;
		return true;
	}
	TlsObject *const pObject = GetTlsObject(pThread, pKey);
	if(!pObject){
		*ppStorage = nullptr;
		return true;
	}
	*ppStorage = pObject->abyStorage;
	return true;
}
bool _MCFCRT_TlsRequire(_MCFCRT_TlsKeyHandle hTlsKey, void **restrict ppStorage){
#ifndef NDEBUG
	*ppStorage = (void *)0xDEADBEEF;
#endif

	TlsKey *const pKey = (TlsKey *)hTlsKey;
	if(!pKey){
		SetLastError(ERROR_INVALID_PARAMETER);
		return false;
	}
	TlsThread *const pThread = RequireTlsForCurrentThread();
	if(!pThread){
		return false;
	}
	TlsObject *const pObject = RequireTlsObject(pThread, pKey, pKey->uSize, pKey->pfnConstructor, pKey->pfnDestructor, pKey->nContext);
	if(!pObject){
		return false;
	}
	*ppStorage = pObject->abyStorage;
	return true;
}

typedef struct tagAtExitCallback {
	_MCFCRT_AtThreadExitCallback pfnProc;
	intptr_t nContext;
} AtExitCallback;

#define CALLBACKS_PER_BLOCK   64u

typedef struct tagAtExitCallbackBlock {
	size_t uSize;
	AtExitCallback aCallbacks[CALLBACKS_PER_BLOCK];
} AtExitCallbackBlock;

static unsigned long CrtAtThreadExitConstructor(intptr_t nUnused, void *pStorage){
	(void)nUnused;

	AtExitCallbackBlock *const pBlock = pStorage;
	pBlock->uSize = 0;
	return 0;
}
static void CrtAtThreadExitDestructor(intptr_t nUnused, void *pStorage){
	(void)nUnused;

	AtExitCallbackBlock *const pBlock = pStorage;
	for(size_t i = pBlock->uSize; i != 0; --i){
		const AtExitCallback *const pCallback = pBlock->aCallbacks + i - 1;
		const _MCFCRT_AtThreadExitCallback pfnProc = pCallback->pfnProc;
		const intptr_t nContext = pCallback->nContext;
		(*pfnProc)(nContext);
	}
}

bool _MCFCRT_AtThreadExit(_MCFCRT_AtThreadExitCallback pfnProc, intptr_t nContext){
	TlsThread *const pThread = RequireTlsForCurrentThread();
	if(!pThread){
		return false;
	}
	AtExitCallbackBlock *pBlock = nullptr;
	TlsObject *pObject = pThread->pLastByThread;
	if(pObject && (pObject->pfnDestructor == &CrtAtThreadExitDestructor)){
		pBlock = (void *)pObject->abyStorage;
	}
	if(!pBlock || (pBlock->uSize >= CALLBACKS_PER_BLOCK)){
		pObject = RequireTlsObject(pThread, nullptr, sizeof(AtExitCallbackBlock), &CrtAtThreadExitConstructor, &CrtAtThreadExitDestructor, 0);
		if(!pObject){
			return false;
		}
		pBlock = (void *)pObject->abyStorage;
	}
	AtExitCallback *const pCallback = pBlock->aCallbacks + ((pBlock->uSize)++);
	pCallback->pfnProc = pfnProc;
	pCallback->nContext = nContext;
	return true;
}
