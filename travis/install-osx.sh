#!/bin/bash
set -e
#set -o xtrace

# install an older version than the one in homebrew, because hwloc version >=2 currently gives problems with static linking.
export HWLOC_NAME="hwloc-1.11.10"
export HWLOC_URL="https://www.open-mpi.org/software/hwloc/v1.11/downloads/$HWLOC_NAME.tar.gz"

if [ ! -f "$HOME/ltsmin-deps/lib/libhwloc.a" ]; then
    wget "$HWLOC_URL" -P /tmp
    tar -xf "/tmp/$HWLOC_NAME.tar.gz" -C /tmp
    pushd /tmp/$HWLOC_NAME
    ./configure --disable-dependency-tracking --disable-shared --disable-libxml2 \
        --enable-static --without-x --disable-cuda --disable-opencl \
        --disable-debug --disable-doxygen --prefix="$HOME/ltsmin-deps"
    make install
    popd
fi

# make sure Sylvan can find hwloc
export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$HOME/ltsmin-deps/lib/pkgconfig"
# also add hwloc.h to the include path
export C_INCLUDE_PATH="$C_INCLUDE_PATH:$HOME/ltsmin-deps/include"

travis/install-generic.sh

set +e

