#!/bin/bash
set -e
#set -o xtrace

export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$HOME/ltsmin-deps/lib/pkgconfig"

CONFIGURE_FLAGS="-q --with-viennacl=$HOME/ltsmin-deps/include --enable-werror"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-silent-rules"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --disable-scoop"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --disable-dependency-tracking"
export CONFIGURE_FLAGS="$CONFIGURE_FLAGS --with-mcrl2=$HOME/ltsmin-deps$MCRL2_LIB_DIR $@"

autoreconf -i
./configure $CONFIGURE_FLAGS || { cat config.log && exit 1; }

export PATH="$PATH:$HOME/ltsmin-deps/bin:$HOME/ProB"

. travis/check-build-cache.sh

set +e

