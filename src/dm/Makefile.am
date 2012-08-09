noinst_PROGRAMS = regroup test
noinst_LTLIBRARIES = libdm.la libdm_debug.la

libdm_la_SOURCES = dm.c dm.h bitvector.c bitvector.h
libdm_la_CFLAGS = -I ${top_srcdir}/src
libdm_debug_la_SOURCES = dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_CFLAGS = -DDMDEBUG -I ${top_srcdir}/src

regroup_SOURCES = regroup.c
regroup_LDADD = libdm.la
regroup_CFLAGS = -I ${top_srcdir}/src

test_SOURCES = test.c
test_LDADD = libdm_debug.la
test_CFLAGS = -I ${top_srcdir}/src -DDMDEBUG

