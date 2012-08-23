
LTSMIN_LIBS = ../hre/libhre.la ../ltsmin-lib/libltsmin.la ../util-lib/libutil.la

bin_PROGRAMS = spgsolver

# spgsolver
spgsolver_SOURCES  = spgsolver.c
nodist_EXTRA_spgsolver_SOURCES = automake-force-linker.cxx # req. by vset
spgsolver_CPPFLAGS = $(AM_CPPFLAGS)
spgsolver_LDFLAGS  = $(AM_LDFLAGS)
spgsolver_LDADD    = ../spg-lib/libspg.la ../vset-lib/libvset.la $(LTSMIN_LIBS)
