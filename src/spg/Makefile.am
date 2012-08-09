AM_CPPFLAGS = -I${top_srcdir}/src

LTSMIN_LIBS = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la

if HAVE_LIBSPG
bin_PROGRAMS = spgsolver
endif

# spgsolver
spgsolver_SOURCES  = spgsolver.c
spgsolver_CPPFLAGS = $(AM_CPPFLAGS)
spgsolver_LDFLAGS  = $(AM_LDFLAGS)
spgsolver_LDADD    = ../libspg.la ../vset-lib/libvset.la $(LTSMIN_LIBS)
