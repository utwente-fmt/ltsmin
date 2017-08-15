#!/bin/bash
set -e
set -o xtrace
export DIVINE_VERSION="1.3"
export DIVINE_COMPILER="gcc-4.9"
export DIVINE_NAME="divine2-ltsmin-$DIVINE_VERSION-$TRAVIS_OS_NAME-$DIVINE_COMPILER.tgz"
export DIVINE_URL="https://github.com/utwente-fmt/divine2/releases/download/$DIVINE_VERSION/$DIVINE_NAME"

if [ ! -f "$HOME/ltsmin-deps/bin/divine" ]; then
    wget "$DIVINE_URL" -P /tmp
    tar -xf "/tmp/$DIVINE_NAME" -C "$HOME/ltsmin-deps"
fi

