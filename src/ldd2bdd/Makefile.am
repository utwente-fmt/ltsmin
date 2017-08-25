bin_PROGRAMS = ldd2bdd

ldd2bdd_SOURCES  = ldd2bdd.c
ldd2bdd_CFLAGS   = $(AM_CFLAGS) $(SYLVAN_CFLAGS)
ldd2bdd_LDADD    = ../hre/libhre.la ../vset-lib/libvset.la $(SYLVAN_LIBS)
nodist_EXTRA_ldd2bdd_SOURCES = automake-force-linker.cxx # req. by vset
