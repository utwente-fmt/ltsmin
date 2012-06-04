AM_CPPFLAGS = -I${top_srcdir}/src

if HAVE_LIBSPG
bin_PROGRAMS = spgsolver
endif

# spgsolver
spgsolver_SOURCES  = spgsolver.c
spgsolver_CPPFLAGS = $(AM_CPPFLAGS)
spgsolver_LDFLAGS  = $(AM_LDFLAGS)
spgsolver_LDADD    = ../libspg.la ../libvset.la ../libltsmin.la ../bignum/libbignum.la
