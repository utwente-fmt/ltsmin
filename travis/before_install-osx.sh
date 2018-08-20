#!/bin/bash
set -e
#set -o xtrace

ls -al "$HOME/ltsmin-deps" || true
ls -al "$HOME/ltsmin-deps/lib" || true
ls -al "$HOME/ltsmin-deps/include" || true
ls -al "$HOME/ltsmin-deps/lib/pkgconfig" || true

brew update
brew install \
bison \
viennacl \
ant \
popt \
libtool \
dejagnu
#ghc \
#cabal-install \

#if [ ! -f "$HOME/.cabal/bin/happy" ]; then
#    cabal update
#    cabal install happy
#fi

set +e
