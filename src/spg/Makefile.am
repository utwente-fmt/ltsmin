AM_CPPFLAGS = -I${top_srcdir}/src

bin_PROGRAMS = spgsolver

# spgsolver
spgsolver_SOURCES  = spgsolver.c
spgsolver_CPPFLAGS = $(AM_CPPFLAGS)
spgsolver_LDFLAGS  = $(AM_LDFLAGS)
spgsolver_LDADD    = ../libspg.la ../libvset.la ../libltsmin.la ../bignum/libbignum.la
