#!/bin/bash
set -e
#set -o xtrace

export LDFLAGS="-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -Wl,-lwinpthread -lmman -Wl,--no-whole-archive"

. travis/configure-windows.sh --disable-doxygen-doc \
    "--prefix=/tmp/$TAG_OR_BRANCH --enable-pkgconf-static"  ""

unset LDFLAGS

export MAKEFLAGS=-j2

# make sure we compile LTSmin with a patched Boost
export CPLUS_INCLUDE_PATH="$CPLUS_INCLUDE_PATH:$TRAVIS_BUILD_DIR/travis/include-fix"

make CFLAGS="-O3" CPPFLAGS="-DNDEBUG" CXXFLAGS="-O3"
make install

x86_64-w64-mingw32.static-strip -s /tmp/$TAG_OR_BRANCH/bin/* || true

pushd /tmp
tar cfz "$LTSMIN_DISTNAME.tgz" "$TAG_OR_BRANCH"
popd

set +e

