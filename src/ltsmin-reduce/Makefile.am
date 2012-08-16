bin_PROGRAMS = ltsmin-reduce

LTSMIN_LIBS = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la

# ltsmin-reduce
ltsmin_reduce_SOURCES   = ltsmin-reduce.c
ltsmin_reduce_LDADD     = ../lts-lib/liblts-lib.la ../lts-io/liblts-io.la
ltsmin_reduce_LDADD    += ../hre/libhre.la ../hre-io/libhre-io.la $(LTSMIN_LIBS)
ltsmin_reduce_CPPFLAGS  = $(AM_CPPFLAGS)
ltsmin_reduce_LDFLAGS   = $(AM_LDFLAGS)
