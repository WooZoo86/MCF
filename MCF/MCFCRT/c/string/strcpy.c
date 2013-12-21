// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2013. LH_Mouse. All wrongs reserved.

#include "../../env/_crtdef.h"

__MCF_CRT_EXTERN char *_strcpyout(char *restrict dst, const char *restrict src);

__MCF_CRT_EXTERN char *strcpy(char *restrict dst, const char *restrict src){
	_strcpyout(dst, src);
	return dst;
}
