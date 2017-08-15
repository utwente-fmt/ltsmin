#!/bin/bash
set -e
set -o xtrace
export PATH=/opt/ghc/$GHCVER/bin:/opt/happy/$HAPPYVER/bin:$PATH &&
export MAKEFLAGS=-j2
export LTSMIN_NUM_CPUS=2
export LD_LIBRARY_PATH="$HOME/ltsmin-deps/lib:$HOME/ProB/lib:$LD_LIBRARY_PATH"

# set correct compiler
export CC="gcc-7"
export CXX="g++-7"
export AR="gcc-ar-7"
export RANLIB="gcc-ranlib-7"
export NM="gcc-nm-7"

. travis/configure-generic.sh "$@"
