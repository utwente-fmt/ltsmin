#!/bin/bash
#set -e
#set -o xtrace

MCRL2_NAME="mcrl2-201707.1"
MCRL2_URL="http://www.mcrl2.org/download/release/$MCRL2_NAME.tar.gz"

if [ ! -f "$HOME/ltsmin-deps$MCRL2_LIB_DIR/lib/libmcrl2_core.a" ]; then
    wget $MCRL2_URL -P /tmp
    pushd /tmp
    tar xf "$MCRL2_NAME.tar.gz"
    pushd "$MCRL2_NAME"

    # apply a patch such that static libraries are installed in the correct folder
    sed -i.bak 's|MCRL2_ARCHIVE_PATH share/mcrl2/lib|MCRL2_ARCHIVE_PATH lib|g' \
        build/cmake/ConfigurePlatform.cmake

    export MAKEFLAGS=-j2

    cmake . -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$HOME/ltsmin-deps" \
        -DMCRL2_ENABLE_GUI_TOOLS=OFF -DMCRL2_MAN_PAGES=OFF
    make

    # apply a patch such that libdparser.a is merged into libmcrl2_core.a
    pushd "stage$MCRL2_LIB_DIR/lib"
    ar -x libmcrl2_core.a
    ar -x libdparser.a
    ar -qc libmcrl2_core.a *.o
    popd

    make install
    popd
    popd
fi

