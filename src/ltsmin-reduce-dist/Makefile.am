bin_PROGRAMS = ltsmin-reduce-dist

LTSMIN_LIBS = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la

# ltsmin-reduce-dist
ltsmin_reduce_dist_SOURCES  = seg-lts.c seg-lts.h
ltsmin_reduce_dist_SOURCES += sigmin-array.c sigmin-array.h
ltsmin_reduce_dist_SOURCES += ltsmin-reduce-dist.c
ltsmin_reduce_dist_SOURCES += sigmin-set.c sigmin-set.h
ltsmin_reduce_dist_SOURCES += sigmin-sig.h
ltsmin_reduce_dist_SOURCES += sigmin-types.h
ltsmin_reduce_dist_SOURCES += sig-array.c sig-array.h
ltsmin_reduce_dist_LDFLAGS  = $(AM_LDFLAGS) $(LTSMIN_LIBS)
ltsmin_reduce_dist_LDADD    = ../lts-io/liblts-io.la
ltsmin_reduce_dist_LDADD   += ../hre-mpi/libhre.la
ltsmin_reduce_dist_LDADD   += ../hre-io/libhre-io.la
ltsmin_reduce_dist_LDADD   += ../util-lib/libutil.la
ltsmin_reduce_dist_LDADD   += ../lts-lib/liblts-lib.la
ltsmin_reduce_dist_CPPFLAGS = $(AM_CPPFLAGS)
