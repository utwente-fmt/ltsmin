#!/bin/bash
set -e
set -o xtrace
export MAKEFLAGS=-j1
export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/usr/local/Cellar/libxml2/$(brew list --versions libxml2 | cut -d' ' -f2)/lib/pkgconfig"
export PATH="/usr/local/opt/bison/bin:$PATH"
export XML_CATALOG_FILES=/usr/local/etc/xml/catalog
export PATH="$HOME/.cabal/bin:$PATH"
export DYLD_LIBRARY_PATH="$HOME/ltsmin-deps/lib:$HOME/ProB/lib:$DYLD_LIBRARY_PATH"
. travis/configure-generic.sh "$@"
