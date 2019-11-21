#!/bin/bash
set -e
#set -o xtrace

. travis/configure-linux.sh --disable-doxygen-doc \
    "--prefix=/tmp/$TAG_OR_BRANCH --enable-pkgconf-static"  ""

export MAKEFLAGS=-j2

# make sure we compile LTSmin with a patched Boost
export CPLUS_INCLUDE_PATH="$CPLUS_INCLUDE_PATH:$TRAVIS_BUILD_DIR/travis/include-fix"

make CPPFLAGS="-DNDEBUG" \
    CFLAGS="-flto -O3 -Wno-lto-type-mismatch" \
    CXXFLAGS="-flto -O3 -Wno-lto-type-mismatch" \
    LDFLAGS="-flto -O3 -all-static -Wl,--no-export-dynamic"
make install

# install DiVinE so that it is included in the distribution
. travis/install-DiVinE.sh

strip -s /tmp/$TAG_OR_BRANCH/bin/* || true
cp "$HOME/ltsmin-deps/bin/divine" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/bin/txt2lps" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/bin/txt2pbes" "/tmp/$TAG_OR_BRANCH/bin"

pushd /tmp
tar cfz "$LTSMIN_DISTNAME.tgz" "$TAG_OR_BRANCH"
popd

set +e

