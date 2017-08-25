
noinst_LTLIBRARIES = libspg.la

# libspg
libspg_la_SOURCES  =
libspg_la_SOURCES += spg.c spg.h
libspg_la_SOURCES += spg-options.c spg-options.h
libspg_la_SOURCES += spg-solve.c spg-solve.h
libspg_la_SOURCES += spg-attr.c spg-attr.h
libspg_la_SOURCES += spg-strategy.c spg-strategy.h
libspg_la_CPPFLAGS = $(AM_CPPFLAGS) $(PROFILER_CPPFLAGS)
libspg_la_LIBADD   =
libspg_la_CFLAGS   = $(AM_CFLAGS)
if HAVE_SYLVAN
libspg_la_CFLAGS  += $(SYLVAN_CFLAGS)
libspg_la_LIBADD  += $(SYLVAN_LIBS)
endif
libspg_la_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
libspg_la_LIBADD  += ../bignum/libbignum.la
