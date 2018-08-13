#!/bin/bash
set -e
#set -o xtrace

mkdir "$HOME/static-libs"
cp "$HOME/ltsmin-deps/lib/libzmq.a" "$HOME/static-libs"
cp "$HOME/ltsmin-deps/lib/libczmq.a" "$HOME/static-libs"
cp /usr/local/lib/libgmp.a "$HOME/static-libs"
cp /usr/local/lib/libpopt.a "$HOME/static-libs"

libxml2_version=$(brew list --versions libxml2 | cut -d' ' -f2)
cp "/usr/local/Cellar/libxml2/$libxml2_version/lib/libxml2.a" \
    "$HOME/static-libs"

. travis/configure-osx.sh --disable-doxygen-doc \
    "--prefix=/tmp/$TAG_OR_BRANCH --enable-pkgconf-static"  "--disable-mcrl2-jittyc"

export MAKEFLAGS=-j2

# make sure we compile LTSmin with a patched Boost
export CPLUS_INCLUDE_PATH="$CPLUS_INCLUDE_PATH:$TRAVIS_BUILD_DIR/travis/include-fix"

make CFLAGS="-flto -O3" CPPFLAGS="-DNDEBUG" CXXFLAGS="-flto -O3" \
    LDFLAGS="-flto -O3 -Wl,-search_paths_first -L$HOME/static-libs -weak-liconv"
make install

# install DiVinE so that it is included in the distribution
. travis/install-DiVinE.sh

strip /tmp/$TAG_OR_BRANCH/bin/* || true
cp "$HOME/ltsmin-deps/bin/divine" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/mCRL2.app/Contents/bin/txt2lps" "/tmp/$TAG_OR_BRANCH/bin"
cp "$HOME/ltsmin-deps/mCRL2.app/Contents/bin/txt2pbes" "/tmp/$TAG_OR_BRANCH/bin"

pushd /tmp
tar cfz "$LTSMIN_DISTNAME.tgz" "$TAG_OR_BRANCH"
popd

set +e

