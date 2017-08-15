
# LIB DEPENDENCIES (transitive depends on relations)
#
# vset-lib  ---->  bignum-lib
#    |
#    |
#    v
#   HRE   <------>   HRE-IO
#    ^
#    |
#    v
# util-lib
#    ^
#    |
#    |
# ltsmin-lib  <--->  lts-io  <----  lts-lib 
#    ^   ^
#    |    \
#    |     \
#    |      \
#    |      mc-lib
#    |            \
#    |             v
# pins-lib  ---->  dm-lib
#    
#
SUBDIRS =

# hybrid runtime environment lib
SUBDIRS += hre-io
SUBDIRS += util-lib
SUBDIRS += hre
if HAVE_MPI
SUBDIRS += hre-mpi
endif

# dependency matrix lib
SUBDIRS += dm
# bignum lib
SUBDIRS += bignum

SUBDIRS += gcf-tool

# LTSmin lib with LTS data types and parsers for property languages
SUBDIRS += ltsmin-lib

SUBDIRS += andl-lib

#JM: disabled, causes a name clash with clang.
#SUBDIRS += etf-convert

SUBDIRS += lts-io
SUBDIRS += lts-lib
SUBDIRS += ltsmin-printtrace
SUBDIRS += ltsmin-compare
SUBDIRS += ltsmin-convert
SUBDIRS += ltsmin-reduce
if HAVE_MPI
SUBDIRS += ltsmin-reduce-dist
SUBDIRS += ce
endif

# PINS lib, defining the partitioned next-state interface
SUBDIRS += pins-lib

if HAVE_PROB
SUBDIRS += prob-lib
endif

if HAVE_CADP
SUBDIRS += pins-open
endif
SUBDIRS += pins2lts-dist
SUBDIRS += torx

# multi-core lib, lockless data structures and a load balancer
SUBDIRS += mc-lib

# vset (BDD/MDD) lib
SUBDIRS += vset-lib

# symbolic parity game lib
SUBDIRS += spg-lib

SUBDIRS += spg

# multi-core lib, lockless data structures and a load balancer
SUBDIRS += mc-lib

SUBDIRS += pins2lts-sym
if HAVE_SYLVAN
SUBDIRS += ldd2bdd
endif
SUBDIRS += pins2lts-mc
SUBDIRS += pins2lts-seq

SUBDIRS += scripts

SUBDIRS += tests

# make
.DELETE_ON_ERROR:
