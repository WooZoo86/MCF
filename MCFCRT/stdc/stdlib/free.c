// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2014. LH_Mouse. All wrongs reserved.

#include "../../env/_crtdef.h"
#include "../../env/heap.h"

__attribute__((__noinline__)) void free(void *p){
	if(p){
		__MCF_CRT_HeapFree(p, __MCF_DEBUG_RET_ADDR);
	}
}
