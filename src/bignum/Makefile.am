
noinst_LTLIBRARIES    = libbignum.la

# libbignum
libbignum_la_SOURCES  = bignum.h
if HAVE_LIBGMP
libbignum_la_SOURCES += bignum-gmp.c
libbignum_la_LIBADD   = -lgmp
else
if HAVE_LIBTOMMATH
libbignum_la_SOURCES += bignum-tommath.c
libbignum_la_LIBADD   = -ltommath
else
libbignum_la_SOURCES += bignum-ll.c
endif
endif
libbignum_la_CPPFLAGS = $(AM_CPPFLAGS)