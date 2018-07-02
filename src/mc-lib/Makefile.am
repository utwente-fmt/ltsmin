
noinst_LTLIBRARIES = libmc.la

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# libmc
libmc_la_SOURCES =
libmc_la_SOURCES += atomics.h
libmc_la_SOURCES += bitvector-ll.c bitvector-ll.h
libmc_la_SOURCES += cctables.c cctables.h
libmc_la_SOURCES += clt_table.c clt_table.h
libmc_la_SOURCES += dbs-ll.c dbs-ll.h
libmc_la_SOURCES += dlopen_extra.c
libmc_la_SOURCES += dlopen_extra.h
libmc_la_SOURCES += hashtable.c hashtable.h
libmc_la_SOURCES += lb.c lb.h
libmc_la_SOURCES += lmap.c lmap.h
libmc_la_SOURCES += renault-unionfind.c
libmc_la_SOURCES += renault-unionfind.h
libmc_la_SOURCES += set-ll.c set-ll.h
libmc_la_SOURCES += statistics.c statistics.h
libmc_la_SOURCES += stats.h
libmc_la_SOURCES += trace.c trace.h
libmc_la_SOURCES += treedbs-ll.c treedbs-ll.h
libmc_la_SOURCES += unionfind.c
libmc_la_SOURCES += unionfind.h
libmc_la_LIBADD   = ../util-lib/libutil.la
libmc_la_LIBADD  += $(top_builddir)/lib/libgnu.la
