noinst_LTLIBRARIES = libparallel.la

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

libparallel_la_SOURCES  = color.c
libparallel_la_SOURCES += color.h
libparallel_la_SOURCES += counter.c
libparallel_la_SOURCES += counter.h
libparallel_la_SOURCES += global.c
libparallel_la_SOURCES += global.h
libparallel_la_SOURCES += options.c
libparallel_la_SOURCES += options.h
libparallel_la_SOURCES += permute.c
libparallel_la_SOURCES += permute.h
libparallel_la_SOURCES += run.h
libparallel_la_SOURCES += run.c
libparallel_la_SOURCES += state-info.c
libparallel_la_SOURCES += state-info.h
libparallel_la_SOURCES += state-store.c
libparallel_la_SOURCES += state-store.h
libparallel_la_SOURCES += stream-serializer.c
libparallel_la_SOURCES += stream-serializer.h
libparallel_la_SOURCES += worker.h
libparallel_la_SOURCES += worker.c

libparallel_la_LIBADD   = $(MC_LIBS)
libparallel_la_LIBADD  += $(top_builddir)/lib/libgnu.la
