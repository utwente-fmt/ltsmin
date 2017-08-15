#!/bin/bash
set -e
set -o xtrace
export MAKEFLAGS=-j1
brew install \
bison \
viennacl \
ant \
popt \
libtool \
homebrew/science/hwloc \
dejagnu
#ghc \
#cabal-install \

#if [ ! -f "$HOME/.cabal/bin/happy" ]; then
#    cabal update
#    cabal install happy
#fi

travis/install-generic.sh
