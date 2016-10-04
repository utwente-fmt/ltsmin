bin_PROGRAMS = ldd2bdd

ldd2bdd_SOURCES  = ldd2bdd.c
ldd2bdd_CPPFLAGS = $(AM_CPPFLAGS) $(SYLVAN_CPPFLAGS)
ldd2bdd_LDADD    = ../hre/libhre.la ../vset-lib/libvset.la
nodist_EXTRA_ldd2bdd_SOURCES = automake-force-linker.cxx # req. by vset
