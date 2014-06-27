CC = $(MPICC)
CXX = $(MPICXX)
CCLD = $(MPICC)
CXXLD = $(MPICXX)
AM_CPPFLAGS = -DWITH_MPI -std=gnu99

HRE_CPPFLAGS = -I${top_builddir}/src -I${top_srcdir}/src $(AM_CPPFLAGS)

noinst_LTLIBRARIES = libhre.la

# libhre(-mpi)
libhre_la_SOURCES  = user.h
libhre_la_SOURCES += hre_mpi.c
libhre_la_SOURCES += mpi_event_loop.c mpi_event_loop.h

libhre_la_CPPFLAGS = $(HRE_CPPFLAGS) $(AM_CPPFLAGS)
libhre_la_LIBADD = ../hre/libhre.la
