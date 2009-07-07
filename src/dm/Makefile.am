noinst_PROGRAMS = regroup test
noinst_LTLIBRARIES = libdm.la libdm_debug.la

libdm_la_SOURCES = dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_SOURCES = dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_CFLAGS = -DDMDEBUG

regroup_SOURCES = regroup.c
regroup_LDADD = libdm.la

test_SOURCES = test.c
test_LDADD = libdm_debug.la
test_CFLAGS = -DDMDEBUG

