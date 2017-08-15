#!/bin/bash
set -e
set -o xtrace

TAG_OR_BRANCH=${TRAVIS_TAG:-$TRAVIS_BRANCH}

. travis/configure-$TRAVIS_OS_NAME.sh --disable-doxygen-doc \
    "--prefix=/tmp/$TAG_OR_BRANCH --enable-pkgconf-static"

make LDFLAGS="-flto -O3 $LTSMIN_LDFLAGS" CFLAGS="-flto -O3 $LTSMIN_CFLAGS" \
    CPPFLAGS="-DNDEBUG" CXXFLAGS="-flto -O3 $LTSMIN_CXXFLAGS"
make install

# install DiVinE so that it is included in the distribution
. travis/install-DiVinE.sh

strip "$STRIP_FLAGS" /tmp/$TAG_OR_BRANCH/bin/* || true
cp "$HOME/ltsmin-deps/bin/divine" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/bin/txt2lps" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/bin/txt2pbes" "/tmp/$TAG_OR_BRANCH/bin"
export LTSMIN_DISTNAME="ltsmin-$TAG_OR_BRANCH-$TRAVIS_OS_NAME"
pushd /tmp
tar cfz "$LTSMIN_DISTNAME.tgz" "$TAG_OR_BRANCH"
popd

