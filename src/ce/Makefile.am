LTSMIN_LIBS  = ../util-lib/libutil.la ../ltsmin-lib/libltsmin.la
LTSMIN_LIBS += ../hre-mpi/libhre.la ../hre-io/libhre-io.la

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
