#!/bin/bash
set -e
#set -o xtrace

export LTSMIN_LDFLAGS="-all-static -Wl,--no-export-dynamic"
# the lto-type-mismatch warnings seems to be a bug in the GCC-6 compiler
export LTSMIN_CFLAGS="-Wno-lto-type-mismatch"
export LTSMIN_CXXFLAGS="-Wno-lto-type-mismatch"
export STRIP_FLAGS="-s"
export MCRL2_LIB_DIR=""

. travis/build-release-generic.sh

set +e

