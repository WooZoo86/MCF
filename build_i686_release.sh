#!/bin/sh

set -e

prefix="$(pwd)/Release/mingw32"
mkdir -p "$prefix"

builddir="$(pwd)/build_i686_release"
mkdir -p "$builddir"

build=i686-w64-mingw32

(cd MCFCRT &&
  mkdir -p m4
  autoreconf -i)

(cd "$builddir" &&
  CPPFLAGS='-DNrelease'	\
  CFLAGS='-O3 -ffunction-sections -fdata-sections'	\
  LDFLAGS='-Wl,-s,--gc-sections'	\
  ../MCFCRT/configure --build="$build" --host="$build" --prefix="$prefix" &&
  make -j4 &&
  make install)
