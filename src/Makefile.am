
pkglib_LTLIBRARIES =
noinst_LTLIBRARIES =

#independent libs
# dependency matrix lib
SUBDIRS  = dm
# bignum lib
SUBDIRS += bignum

# hybrid runtime environment lib
SUBDIRS += hre-io
SUBDIRS += hre
if HAVE_MPI
SUBDIRS += hre-mpi
endif
SUBDIRS += hre-test
SUBDIRS += lts-io
SUBDIRS += lts-lib

# util lib with basic data structures
SUBDIRS += util-lib
#dependents:
SUBDIRS += gcf-tool

# LTSmin lib with LTS data types and parsers for property languages
SUBDIRS += ltsmin-lib
#dependents:
SUBDIRS += etf-convert
SUBDIRS += ltsmin-printtrace
SUBDIRS += ltsmin-compare
SUBDIRS += ltsmin-convert
SUBDIRS += ltsmin-reduce
if HAVE_MPI
SUBDIRS += ltsmin-reduce-dist
endif
SUBDIRS += ce

# PINS lib, defining the partitioned next-state interface
SUBDIRS += pins-lib
#dependents:
if HAVE_CADP
SUBDIRS += pins-open
endif
SUBDIRS += pins2lts-dist
SUBDIRS += torx

# vset (BDD/MDD) lib
SUBDIRS += vset-lib
#dependents:

# symbolic parity game lib 
if HAVE_LIBSPG
SUBDIRS += spg-lib
#dependents:
SUBDIRS += spg
endif
SUBDIRS += pins2lts-sym

# multi-core lib, lockless data structures and a load balancer
if HAVE_ATOMICS
SUBDIRS += mc-lib
SUBDIRS += pins2lts-mc
SUBDIRS += pins2lts-seq
endif

SUBDIRS += scripts
SUBDIRS += tests

# make
.DELETE_ON_ERROR:
