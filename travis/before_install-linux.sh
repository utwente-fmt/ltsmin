#!/bin/bash
set -e
#set -o xtrace

ls -al "$HOME/ltsmin-deps" || true
ls -al "$HOME/ltsmin-deps/lib" || true
ls -al "$HOME/ltsmin-deps/include" || true
ls -al "$HOME/ltsmin-deps/lib/pkgconfig" || true

true

set +e

