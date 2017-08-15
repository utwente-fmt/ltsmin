#!/bin/bash
set -e
set -o xtrace

. travis/configure-$TRAVIS_OS_NAME.sh "--disable-doxygen-doc --without-mcrl2"

FAIL=0
make check-DVE || ( FAIL=1 ; cat testsuite/check-DVE.log )
make check-ETF || ( FAIL=1 ; cat testsuite/check-ETF.log )
make check-PNML || ( FAIL=1 ; cat testsuite/check-PNML.log )
make check-ProB || ( FAIL=1 ; cat testsuite/check-ProB.log )
make check-Promela || ( FAIL=1 ; cat testsuite/check-Promela.log )
make check-DFS-FIFO || ( FAIL=1 ; cat testsuite/check-DFS-FIFO.log )
make check-LTL || ( FAIL=1 ; cat testsuite/check-LTL.log )
make check-POR || ( FAIL=1 ; cat testsuite/check-POR.log )
make check-safety || ( FAIL=1 ; cat testsuite/check-safety.log )
make check-SCC || ( FAIL=1 ; cat testsuite/check-SCC.log )

test $FAIL -eq 1 && exit 1

