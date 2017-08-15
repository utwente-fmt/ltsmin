#!/bin/bash
set -e
set -o xtrace
mkdir "$HOME/ltsmin-debug"
pushd "$HOME/ltsmin-debug"
autoreconf "$TRAVIS_BUILD_DIR" -i
"$TRAVIS_BUILD_DIR/configure" --with-viennacl="$HOME/ltsmin-deps/include" \
    --without-scoop --disable-dependency-tracking --enable-werror \
    --disable-test-all --with-mcrl2="$HOME/ltsmin-deps"
make
popd

