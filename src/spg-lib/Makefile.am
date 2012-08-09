HRE_LIB  = ../hre/libhre.la
UTIL_LIB = ../util-lib/libutil.la $(HRE_LIB)
VSET_LIB = ../vset-lib/libvset.la

noinst_LTLIBRARIES = libspg.la

# libspg
libspg_la_SOURCES  = spg-solve.c spg-solve.h
libspg_la_CPPFLAGS = -DNDEBUG $(AM_CPPFLAGS) -I ${top_srcdir}/src
libspg_la_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
libspg_la_LIBADD   = $(VSET_LIB) $(UTIL_LIB)
