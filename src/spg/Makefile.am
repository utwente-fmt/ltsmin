
LTSMIN_LIBS = ../hre/libhre.la ../ltsmin-lib/libltsmin.la ../util-lib/libutil.la

bin_PROGRAMS = spgsolver

# spgsolver
spgsolver_SOURCES  = spgsolver.c
nodist_EXTRA_spgsolver_SOURCES = automake-force-linker.cxx # req. by vset
spgsolver_CPPFLAGS = $(AM_CPPFLAGS) $(PROFILER_CPPFLAGS)
spgsolver_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
spgsolver_LDADD    = ../spg-lib/libspg.la ../vset-lib/libvset.la $(LTSMIN_LIBS)
spgsolver_CFLAGS   = $(AM_CFLAGS) $(PROFILER_CFLAGS)
if HAVE_SYLVAN
spgsolver_CFLAGS  += $(SYLVAN_CFLAGS)
spgsolver_LDADD   += $(SYLVAN_LIBS)
endif
