CC = $(MPICC)
CXX = $(MPICXX)
CCLD = $(MPICC)
CXXLD = $(MPICXX)
AM_CPPFLAGS = -DWITH_MPI -std=gnu99 -I../../../src
HRE_LIB = ../../hre-mpi/libhre.la $(MPILIBS)
DIST_CPPFLAGS = -DUSEMPI $(AM_CPPFLAGS) -DTESTMPI 

include ../include.am

