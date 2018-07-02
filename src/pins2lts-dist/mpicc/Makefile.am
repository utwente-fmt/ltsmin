# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

CC = $(MPICC)
CXX = $(MPICXX)
CCLD = $(MPICC)
CXXLD = $(MPICXX)
AM_CPPFLAGS += -DWITH_MPI -std=gnu99 -I${top_srcdir}/src -I${top_builddir}/src
HRE_LIB = ../../hre-mpi/libhre.la $(MPILIBS)
DIST_CPPFLAGS = -DUSEMPI $(AM_CPPFLAGS) 
DIST_CFLAGS = $(AM_CFLAGS)

include ../include.am

