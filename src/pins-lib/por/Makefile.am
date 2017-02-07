
HRE_LIB = ../../hre/libhre.la
LIBS  =
LIBS += ../../lts-io/liblts-io.la
LIBS += ../../hre-io/libhre-io.la
LIBS += $(HRE_LIB)
LIBS += ../../util-lib/libutil.la
LIBS += ../../ltsmin-lib/libltsmin.la
LIBS += ../../dm/libdm.la

noinst_LTLIBRARIES  = libpor.la

# libpor
libpor_la_SOURCES  =
libpor_la_SOURCES += pins2pins-por.h
libpor_la_SOURCES += pins2pins-por.c
libpor_la_SOURCES += por-ample.h
libpor_la_SOURCES += por-ample.c
libpor_la_SOURCES += por-beam.h
libpor_la_SOURCES += por-beam.c
libpor_la_SOURCES += por-deletion.h
libpor_la_SOURCES += por-deletion.c
libpor_la_SOURCES += por-internal.h
libpor_la_SOURCES += por-leap.h
libpor_la_SOURCES += por-leap.c
libpor_la_SOURCES += por-lipton.h
libpor_la_SOURCES += por-lipton.c
libpor_la_SOURCES += pins2pins-por-check.h
libpor_la_SOURCES += pins2pins-por-check.c

libpor_la_CPPFLAGS = $(AM_CPPFLAGS)
libpor_la_LIBADD   =
libpor_la_LIBADD  += $(LIBS)
