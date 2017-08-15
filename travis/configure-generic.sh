#!/bin/bash
set -e
set -o xtrace

export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$HOME/ltsmin-deps/lib/pkgconfig"

CONFIGURE_FLAGS="-q --with-viennacl=$HOME/ltsmin-deps/include --enable-werror"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --enable-silent-rules --disable-scoop"
CONFIGURE_FLAGS="$CONFIGURE_FLAGS --disable-dependency-tracking"
export CONFIGURE_FLAGS="$CONFIGURE_FLAGS --with-mcrl2=$HOME/ltsmin-deps $@"

autoreconf -i
./configure $CONFIGURE_FLAGS

export PATH="$PATH:$HOME/ltsmin-deps/bin:$HOME/ProB"

