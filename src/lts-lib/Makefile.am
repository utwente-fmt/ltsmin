noinst_LTLIBRARIES = liblts-lib.la

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# liblts
liblts_lib_la_SOURCES  = bfs_reorder.c
liblts_lib_la_SOURCES += determinize.c
liblts_lib_la_SOURCES += dir-info.c dir-info.h
liblts_lib_la_SOURCES += find-states.c
liblts_lib_la_SOURCES += lts.c lts.h
liblts_lib_la_SOURCES += lts_access.c
liblts_lib_la_SOURCES += lts-io.c
liblts_lib_la_SOURCES += lts_print.c
liblts_lib_la_SOURCES += lts-pg-io.h lts-pg-io.c
liblts_lib_la_SOURCES += lts-quant-io.c
liblts_lib_la_SOURCES += lts_uniq.c
liblts_lib_la_SOURCES += lowmem.h
liblts_lib_la_SOURCES += lowmem_b.c
liblts_lib_la_SOURCES += lowmem_lump.c
liblts_lib_la_SOURCES += lowmem_s.c
liblts_lib_la_SOURCES += sigmin-lib.c
liblts_lib_la_SOURCES += set.c set.h
liblts_lib_la_SOURCES += setbased_weak.c
liblts_lib_la_SOURCES += silent_compress.c

liblts_lib_la_LIBADD   = $(top_builddir)/lib/libgnu.la

