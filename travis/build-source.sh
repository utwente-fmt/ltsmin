#!/bin/bash
set -e
#set -o xtrace

TAG_OR_BRANCH=${TRAVIS_TAG:-$TRAVIS_BRANCH}

. travis/configure-linux.sh
make distcheck DISTCHECK_CONFIGURE_FLAGS="$CONFIGURE_FLAGS"

# get the LTSmin version number
. travis/ltsmin-version.sh

# rename the source tarball
mv "ltsmin-$LTSMIN_VERSION.tar.gz" "ltsmin-$TAG_OR_BRANCH-source.tgz"

set +e

