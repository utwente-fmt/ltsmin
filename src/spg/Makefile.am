AM_CPPFLAGS = -I${top_srcdir}/src

LTSMIN_LIBS = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la

bin_PROGRAMS = spgsolver

# spgsolver
spgsolver_SOURCES  = spgsolver.c
spgsolver_CPPFLAGS = $(AM_CPPFLAGS) -I ${top_srcdir}/src
spgsolver_LDFLAGS  = $(AM_LDFLAGS)
spgsolver_LDADD    = ../spg-lib/libspg.la ../vset-lib/libvset.la $(LTSMIN_LIBS)
