#!/bin/bash
set -e
#set -o xtrace

brew update
brew install \
bison \
viennacl \
ant \
popt \
libtool \
hwloc \
pastebinit \
dejagnu
#ghc \
#cabal-install \

#if [ ! -f "$HOME/.cabal/bin/happy" ]; then
#    cabal update
#    cabal install happy
#fi

set +e
