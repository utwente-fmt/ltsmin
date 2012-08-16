
HRE_LIB = ../hre/libhre.la ../hre-io/libhre-io.la
UTIL_LIB = ../util-lib/libutil.la $(HRE_LIB)
LTSMIN_LIB = ../ltsmin-lib/libltsmin.la $(UTIL_LIB)

bin_PROGRAMS 		= etf-convert

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD   = $(LTSMIN_LIB)
etf_convert_CPPFLAGS= $(AM_CPPFLAGS)