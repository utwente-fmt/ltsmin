#!/bin/bash
set -e
set -o xtrace
export MAKEFLAGS=-j2
export GHCVER="7.10.3"
export HAPPYVER="1.19.5"

# set correct compiler
export CC="gcc-7"
export CXX="g++-7"
export AR="gcc-ar-7"
export RANLIB="gcc-ranlib-7"
export NM="gcc-nm-7"

travis/install-generic.sh
