CC = $(MPICC)
AM_CPPFLAGS = -DWITH_MPI -std=gnu99 -I${top_srcdir}/src -I${top_builddir}/src

LTSMIN_LIBS  =
LTSMIN_LIBS += ../lts-io/liblts-io.la
LTSMIN_LIBS += ../hre-io/libhre-io.la
LTSMIN_LIBS += ../hre-mpi/libhre.la
LTSMIN_LIBS += ../ltsmin-lib/libltsmin.la
LTSMIN_LIBS += ../util-lib/libutil.la

bin_PROGRAMS 	= ce-mpi

# ce-mpi
ce_mpi_SOURCES 	= \
    ce-mpi.c \
    bufs.c bufs.h \
    Ddlts.c Ddlts.h \
    Dtaudlts.c Dtaudlts.h \
    groups.c groups.h \
    paint.c paint.h	\
    sortcount.c sortcount.h
ce_mpi_LDADD 	= ../lts-io/liblts-io.la $(LTSMIN_LIBS)
ce_mpi_CPPFLAGS = $(AM_CPPFLAGS)
