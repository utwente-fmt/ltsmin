noinst_LTLIBRARIES  = libutil.la

libutil_la_SOURCES  = balloc.c balloc.h
libutil_la_SOURCES += bitset.c bitset.h
libutil_la_SOURCES += chunk_support.c chunk_support.h
libutil_la_SOURCES += chunk_table_factory.c
libutil_la_SOURCES += dynamic-array.c dynamic-array.h
libutil_la_SOURCES += fast_hash.c fast_hash.h
libutil_la_SOURCES += qsortex.c
libutil_la_SOURCES += string-map.c string-map.h
libutil_la_SOURCES += stringindex.c stringindex.h
libutil_la_SOURCES += tables.c tables.h
libutil_la_SOURCES += treedbs.c treedbs.h
libutil_la_SOURCES += unix.h

libutil_la_LIBADD   = ../hre/libhre.la
libutil_la_LIBADD  += ../hre-io/libhre-io.la

libutil_la_CFLAGS   = -I ${top_srcdir}/src 