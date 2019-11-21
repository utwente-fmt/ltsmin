bin_PROGRAMS = ltsmin-printtrace

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# ltsmin-printtrace
ltsmin_printtrace_SOURCES  = lts-tracepp.c
ltsmin_printtrace_LDADD    = ../lts-lib/liblts-lib.la
ltsmin_printtrace_LDADD   += ../lts-io/liblts-io.la
ltsmin_printtrace_LDADD   += ../hre-io/libhre-io.la
ltsmin_printtrace_LDADD   += ../hre/libhre.la
ltsmin_printtrace_LDADD   += ../util-lib/libutil.la
ltsmin_printtrace_LDADD   += ../ltsmin-lib/libltsmin.la

ltsmin_printtrace_LDADD   += $(top_builddir)/lib/libgnu.la
