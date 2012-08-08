bin_PROGRAMS = ltsmin-printtrace

# ltsmin-printtrace
ltsmin_printtrace_SOURCES  = lts-tracepp.c
ltsmin_printtrace_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
ltsmin_printtrace_LDADD    = ../lts-lib/liblts-lib.la
ltsmin_printtrace_LDADD   += ../lts-io/liblts-io.la
ltsmin_printtrace_LDADD   += ../hre/libhre.la
ltsmin_printtrace_LDADD   += ../util-lib/libutil.la
ltsmin_printtrace_LDADD   += ../ltsmin-lib/libltsmin.la
