AM_CPPFLAGS = -I${top_srcdir}/src

bin_PROGRAMS =

if HAVE_MPI
bin_PROGRAMS += ce-mpi
endif

# ce-mpi
ce_mpi_SOURCES = \
    ce-mpi.c \
    bufs.c bufs.h \
    Ddlts.c Ddlts.h \
    Dtaudlts.c Dtaudlts.h \
    groups.c groups.h \
    paint.c paint.h	\
    sortcount.c sortcount.h

ce_mpi_LDADD = ../hre-mpi/libhre.la ../libltsmin.la

