#!/bin/bash
set -e
#set -o xtrace

export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:/usr/local/Cellar/libxml2/$(brew list --versions libxml2 | cut -d' ' -f2)/lib/pkgconfig"
export PATH="/usr/local/opt/bison/bin:$PATH"
export XML_CATALOG_FILES=/usr/local/etc/xml/catalog
export PATH="$HOME/.cabal/bin:$PATH"
export DYLD_LIBRARY_PATH="$HOME/ltsmin-deps/lib:$HOME/ProB/lib:$DYLD_LIBRARY_PATH"
export MCRL2_LIB_DIR="/mCRL2.app/Contents"

export PKG_CONFIG_PATH="$PKG_CONFIG_PATH:$HOME/ltsmin-deps/lib/pkgconfig"

. travis/configure-generic.sh "$@"

set +e

