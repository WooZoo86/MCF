// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2014. LH_Mouse. All wrongs reserved.

#ifndef MCF_CRT_HEAP_H_
#define MCF_CRT_HEAP_H_

#include "_crtdef.h"

__MCF_EXTERN_C_BEGIN

extern bool __MCF_CRT_HeapInit(void) MCF_NOEXCEPT;
extern void __MCF_CRT_HeapUninit(void) MCF_NOEXCEPT;

extern unsigned char *__MCF_CRT_HeapAlloc(MCF_STD size_t uSize, const void *pRetAddr) MCF_NOEXCEPT;
extern unsigned char *__MCF_CRT_HeapReAlloc(void *pBlock /* NON-NULL */, MCF_STD size_t uSize, const void *pRetAddr) MCF_NOEXCEPT;
extern void __MCF_CRT_HeapFree(void *pBlock /* NON-NULL */, const void *pRetAddr) MCF_NOEXCEPT;

typedef struct MCF_tagBadAllocHandler {
	int (*pfnProc)(MCF_STD intptr_t);
	MCF_STD intptr_t nContext;
} MCF_BAD_ALLOC_HANDLER;

extern MCF_BAD_ALLOC_HANDLER MCF_GetBadAllocHandler(void) MCF_NOEXCEPT;
extern MCF_BAD_ALLOC_HANDLER MCF_SetBadAllocHandler(MCF_BAD_ALLOC_HANDLER NewHandler) MCF_NOEXCEPT;

__MCF_EXTERN_C_END

#endif
