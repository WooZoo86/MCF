@echo off

mcfbuild -C"mcfcrt" Release %*

g++ -std=c++11 -Wall -Wextra -O3 main.cpp -Lmcfcrt\.built-release\ -static -nostartfiles -Wl,-e__MCFExeStartup,--disable-runtime-pseudo-reloc,--disable-auto-import,-lmcfcrt,-lstdc++,-lgcc,-lgcc_eh,-lmingwex,-lmcfcrt
