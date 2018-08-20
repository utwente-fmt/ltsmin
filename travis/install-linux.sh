#!/bin/bash
set -e
#set -o xtrace

export GHCVER="7.10.3"
export HAPPYVER="1.19.5"

# set correct compiler
export CC="gcc-6"
export CXX="g++-6"
export AR="gcc-ar-6"
export RANLIB="gcc-ranlib-6"
export NM="gcc-nm-6"

travis/install-generic.sh

set +e

