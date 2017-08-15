#!/bin/bash
set -e
set -o xtrace

mkdir "$HOME/static-libs"
cp "$HOME/ltsmin-deps/lib/libzmq.a" "$HOME/static-libs"
cp "$HOME/ltsmin-deps/lib/libczmq.a" "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libpopt.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libgmp.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libltdl.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libxml2.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/liblzma.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libhwloc.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libnuma.a "$HOME/static-libs"
cp /usr/lib/x86_64-linux-gnu/libz.a "$HOME/static-libs"

export LTSMIN_LDFLAGS="-L$HOME/static-libs -static-libgcc -static-libstdc++"
# the lto-type-mismatch warnings seems to be a bug in the GCC-7 compiler
export LTSMIN_CFLAGS="-Wno-lto-type-mismatch"
export LTSMIN_CXXFLAGS="-Wno-lto-type-mismatch"
export STRIP_FLAGS="-s"

. travis/build-release-generic.sh

