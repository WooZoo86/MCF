// 这个文件是 MCF 的一部分。
// 有关具体授权说明，请参阅 MCFLicense.txt。
// Copyleft 2014. LH_Mouse. All wrongs reserved.

#include "../../env/_crtdef.h"
#include "_mathasm.h"

float tanf(float x){
	register float ret;
	__asm__ __volatile__(
		"fld dword ptr[%1] \n"
		"fptan \n"
		"fstsw ax \n"
		"test ah, 4 \n"
		"jz 1f \n"
		"	fldpi \n"
		"	fxch st(1) \n"
		"	2: \n"
		"		fprem \n"
		"		fstsw ax \n"
		"		test ah, 4 \n"
		"	jnz 2b \n"
		"	fstp st(1) \n"
		"	fptan \n"
		"1: \n"
		"fstp st \n"
		__MCF_FLT_RET_ST("%1")
		: __MCF_FLT_RET_CONS(ret)
		: "m"(x)
		: "ax"
	);
	return ret;
}

double tan(double x){
	register double ret;
	__asm__ __volatile__(
		"fld qword ptr[%1] \n"
		"fptan \n"
		"fstsw ax \n"
		"test ah, 4 \n"
		"jz 1f \n"
		"	fldpi \n"
		"	fxch st(1) \n"
		"	2: \n"
		"		fprem \n"
		"		fstsw ax \n"
		"		test ah, 4 \n"
		"	jnz 2b \n"
		"	fstp st(1) \n"
		"	fptan \n"
		"1: \n"
		"fstp st \n"
		__MCF_DBL_RET_ST("%1")
		: __MCF_DBL_RET_CONS(ret)
		: "m"(x)
		: "ax"
	);
	return ret;
}

__MCF_LDBL_DECL(tanl, long double x){
	register long double ret;
	__asm__ __volatile__(
		"fld tbyte ptr[%1] \n"
		"fptan \n"
		"fstsw ax \n"
		"test ah, 4 \n"
		"jz 1f \n"
		"	fldpi \n"
		"	fxch st(1) \n"
		"	2: \n"
		"		fprem \n"
		"		fstsw ax \n"
		"		test ah, 4 \n"
		"	jnz 2b \n"
		"	fstp st(1) \n"
		"	fptan \n"
		"1: \n"
		"fstp st \n"
		__MCF_LDBL_RET_ST()
		: __MCF_LDBL_RET_CONS(ret)
		: "m"(x), __MCF_LDBL_RET_CONS_IN()
		: "ax"
	);
	__MCF_LDBL_RETURN(ret);
}
