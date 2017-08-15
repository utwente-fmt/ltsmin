#!/bin/bash
set -e
set -o xtrace

. travis/configure-osx.sh "--disable-doxygen-doc"

FAIL=0
make check-LPS || ( FAIL=1 ; cat testsuite/check-LPS.log )
make check-PBES || ( FAIL=1 ; cat testsuite/check-PBES.log )
make check-LTS || ( FAIL=1 ; cat testsuite/check-LTS.log )
make check-MU || ( FAIL=1 ; cat testsuite/check-MU.log )

test $FAIL -eq 1 && exit 1

