bin_PROGRAMS = lts-tracepp

# lts-tracepp
lts_tracepp_SOURCES  = lts-tracepp.c
lts_tracepp_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
lts_tracepp_LDADD    = ../lts-lib/liblts-lib.la
lts_tracepp_LDADD   += ../lts-io/liblts-io.la
lts_tracepp_LDADD   += ../hre/libhre.la
lts_tracepp_LDADD   += ../libltsmin-hre.la
