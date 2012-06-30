bin_PROGRAMS = sigmin-mpi

# sigmin-mpi
sigmin_mpi_SOURCES  = seg-lts.c seg-lts.h
sigmin_mpi_SOURCES += set.c set.h
sigmin_mpi_SOURCES += sigmin-array.c sigmin-array.h
sigmin_mpi_SOURCES += sigmin-mpi.c
sigmin_mpi_SOURCES += sigmin-set.c sigmin-set.h
sigmin_mpi_SOURCES += sigmin-sig.h
sigmin_mpi_SOURCES += sigmin-types.h
sigmin_mpi_SOURCES += sig-array.c sig-array.h
sigmin_mpi_LDFLAGS  = $(AM_LDFLAGS)
sigmin_mpi_LDADD    = ../lts-io/liblts-io.la
sigmin_mpi_LDADD   += ../hre-mpi/libhre.la
sigmin_mpi_LDADD   += ../libltsmin.la
sigmin_mpi_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
