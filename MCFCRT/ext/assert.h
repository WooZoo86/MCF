// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013 - 2016, LH_Mouse. All wrongs reserved.

#ifndef __MCFCRT_EXT_ASSERT_H_
#define __MCFCRT_EXT_ASSERT_H_

#include "../env/_crtdef.h"
#include "../env/bail.h"

static inline __attribute__((__noreturn__))
void __MCFCRT_OnAssertFail(const wchar_t *__pwszExpression, const char *__pszFile, unsigned long __ulLine, const wchar_t *__pwszMessage) MCF_NOEXCEPT {
	MCFCRT_BailF(L"调试断言失败。\n\n表达式：%ls\n文件　：%hs\n行号　：%lu\n描述　：%ls", __pwszExpression, __pszFile, __ulLine, __pwszMessage);
}

#define ASSERT(__expr_)                 (__MCFCRT_ASSERT_MSG(#__expr_, (__expr_), L""))
#define ASSERT_MSG(__expr_, __msg_)     (__MCFCRT_ASSERT_MSG(#__expr_, (__expr_), (__msg_)))

#endif

// 这部分每次 #include 都可能会变。
#undef __MCFCRT_ASSERT_MSG

#ifdef NDEBUG
#	define __MCFCRT_ASSERT_MSG(__plain_, __expr_, __msg_)  ((void)(__typeof__((void)(__expr_), 1))0)
#else
#	define __MCFCRT_ASSERT_MSG(__plain_, __expr_, __msg_)  ((void)(!(__expr_) && (__MCFCRT_OnAssertFail(L ## __plain_, __FILE__, __LINE__, (__msg_)), 1)))
#endif
