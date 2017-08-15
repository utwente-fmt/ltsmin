#!/bin/bash
set -e
set -o xtrace

TAG_OR_BRANCH=${TRAVIS_TAG:-$TRAVIS_BRANCH}

. travis/configure-linux.sh
make distcheck DISTCHECK_CONFIGURE_FLAGS="$CONFIGURE_FLAGS"

# get the LTSmin version number
export LTSMIN_VERSION=$(grep "PACKAGE_VERSION" src/hre/config.h | cut -d" " -f3 | \
    cut -d\" -f2)

# rename the source tarball
mv "ltsmin-$LTSMIN_VERSION.tar.gz" "ltsmin-$TAG_OR_BRANCH-source.tgz"

