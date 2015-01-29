CC = $(MPICC)
CXX = $(MPICXX)
CCLD = $(MPICC)
CXXLD = $(MPICXX)
AM_CPPFLAGS = -DWITH_MPI -std=gnu99 -I${top_srcdir}/src -I${top_builddir}/src
HRE_LIB = ../../hre-mpi/libhre.la $(MPILIBS)
DIST_CPPFLAGS = -DUSEMPI $(AM_CPPFLAGS) -DTESTMPI 

include ../include.am

