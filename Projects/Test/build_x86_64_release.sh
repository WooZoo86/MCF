#!/bin/sh

CPPFLAGS+=" -O3 -DNDEBUG -Wall -Wextra -pedantic -pedantic-errors -Wno-error=unused-parameter -Winvalid-pch	\
	-Wwrite-strings -Wconversion -Wsign-conversion -Wsuggest-attribute=noreturn -Wundef -Wshadow -Wstrict-aliasing=2 -Wstrict-overflow=5	\
	-pipe -mfpmath=both -march=nocona -mno-stack-arg-probe -mno-accumulate-outgoing-args -mpush-args -masm=intel	\
	-I../../release/mingw64/include"
CXXFLAGS+=" -O3 -std=c++17 -Wzero-as-null-pointer-constant -Wnoexcept -Woverloaded-virtual -fnothrow-opt"
LDFLAGS+=" -O3 -nostdlib -L../../release/mingw64/lib -static -lmcf -lsupc++ -lmingwex -lgcc -lgcc_eh -static -lmcfcrt-pre-exe -static -lmcfcrt -lmsvcrt -lkernel32 -lntdll -Wl,--disable-runtime-pseudo-reloc,-e@__MCFCRT_ExeStartup"

cp -fp ../../release/mingw64/bin/*.dll ./

x86_64-w64-mingw32-g++ ${CPPFLAGS} ${CXXFLAGS} main.cpp ${LDFLAGS}
