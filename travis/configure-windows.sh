#!/bin/bash
set -e
#set -o xtrace

export LTSMIN_NUM_CPUS=2
export LD_LIBRARY_PATH="$HOME/ltsmin-deps/lib"

# set correct compiler
export BUILD_HOST="x86_64-w64-mingw32.static"
export CC="x86_64-w64-mingw32.static-gcc"
export CXX="x86_64-w64-mingw32.static-g++"

export MCRL2_LIB_DIR=""

export PKG_CONFIG_PATH_x86_64_w64_mingw32_static="$PKG_CONFIG_PATH_x86_64_w64_mingw32_static:$HOME/ltsmin-deps/lib/pkgconfig"

export LDFLAGS='-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -Wl,-lwinpthread -lmman -Wl,--no-whole-archive'

export PATH=/usr/lib/mxe/usr/bin:$PATH

. travis/configure-generic.sh "--host=$BUILD_HOST --without-boost --without-mcrl2 $@"

export LDFLAGS=""

set +e

