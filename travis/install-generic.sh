#!/bin/bash
set -e
#set -o xtrace

export MAKEFLAGS=-j2

export CZMQ_VERSION="3.0.2"
export CZMQ_URL="https://github.com/zeromq/czmq/archive/v$CZMQ_VERSION.tar.gz"
export VIENNACL_NAME="ViennaCL-1.7.1"
export VIENNACL_URL="http://netcologne.dl.sourceforge.net/project/viennacl/1.7.x/$VIENNACL_NAME.tar.gz"
export ZMQ_VERSION="4.1.5"
export ZMQ_NAME="zeromq-$ZMQ_VERSION"
export ZMQ_URL="https://github.com/zeromq/zeromq4-1/releases/download/v$ZMQ_VERSION/$ZMQ_NAME.tar.gz"
export DDD_NAME="ddd"
export DDD_VERSION="$DDD_NAME-1.8.1"
export DDD_URL="http://ddd.lip6.fr/download/$DDD_VERSION.tar.gz"
export SYLVAN_VERSION="1.4.1"
export SYLVAN_URL="https://github.com/trolando/sylvan/archive/v$SYLVAN_VERSION.tar.gz"
export SYLVAN_NAME="sylvan-$SYLVAN_VERSION"
export SPOT_VERSION="2.3.3"
export SPOT_NAME="spot-$SPOT_VERSION"
export SPOT_URL="http://www.lrde.epita.fr/dload/spot/$SPOT_NAME.tar.gz"

mkdir -p "$HOME/ltsmin-deps"

# install Sylvan from source
if [ ! -f "$HOME/ltsmin-deps/lib/libsylvan.a" -a "$BUILD_TARGET" != "windows" ]; then
    wget "$SYLVAN_URL" -P /tmp
    tar -xf "/tmp/v$SYLVAN_VERSION.tar.gz" -C /tmp
    pushd /tmp/sylvan-$SYLVAN_VERSION
    mkdir build
    cd build
    cmake .. -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$HOME/ltsmin-deps"
    make
    make install
    popd
fi

# install zmq from source, since libzmq3-dev in apt is missing dependencies for
# a full static LTSmin build (pgm and sodium are missing)
# I filed a bug report here: https://github.com/travis-ci/travis-ci/issues/5701
if [ ! -f "$HOME/ltsmin-deps/lib/libzmq.a" -a "$BUILD_TARGET" != "windows" ]; then
    wget "$ZMQ_URL" -P /tmp
    tar -xf "/tmp/$ZMQ_NAME.tar.gz" -C /tmp
    pushd /tmp/$ZMQ_NAME
    ./autogen.sh
    ./configure --enable-static --enable-shared --prefix="$HOME/ltsmin-deps" \
        --without-libsodium --without-pgm --without-documentation
    make CFLAGS="-Wno-error -g -O2"
    make install
    popd
fi

# install czmq from source
# since czmq is not distributed in Ubuntu.
# the LDFLAGS is necessary, because of a bug: https://github.com/zeromq/czmq/issues/1323
# the CFLAGS is necessary, because we need to unset NDEBUG: https://github.com/zeromq/czmq/issues/1519
if [ ! -f "$HOME/ltsmin-deps/lib/libczmq.a" -a "$BUILD_TARGET" != "windows" ]; then
    wget "$CZMQ_URL" -P /tmp
    tar -xf "/tmp/v$CZMQ_VERSION.tar.gz" -C /tmp
    pushd /tmp/czmq-$CZMQ_VERSION
    ./autogen.sh
    ./configure --enable-static --enable-shared --prefix="$HOME/ltsmin-deps" --with-libzmq="$HOME/ltsmin-deps"
    make CFLAGS="-Wno-error -g -O2" LDFLAGS="-lpthread"
    make install
    popd
fi

# install Spot from source
if [ ! -f "$HOME/ltsmin-deps/lib/libspot.a" ]; then
    wget "$SPOT_URL" -P /tmp
    tar xf "/tmp/$SPOT_NAME.tar.gz" -C /tmp
    pushd "/tmp/$SPOT_NAME"
    ./configure --disable-dependency-tracking --disable-python --enable-static --disable-shared --prefix="$HOME/ltsmin-deps" || cat config.log
    make
    make install
    popd
fi

# install ViennaCL on linux
if [ ! -d "$HOME/ltsmin-deps/include/viennacl" -a "$BUILD_TARGET" = "linux" ]; then
    wget "$VIENNACL_URL" -P /tmp &&
    tar xf "/tmp/$VIENNACL_NAME.tar.gz" -C /tmp &&
    cp -R "/tmp/$VIENNACL_NAME/viennacl" "$HOME/ltsmin-deps/include"
fi

set +e

