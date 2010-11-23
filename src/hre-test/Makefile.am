check_PROGRAMS = test-hre

# test-hre
test_hre_SOURCES = test-hre.c
test_hre_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
test_hre_LDADD = ../hre/libhre.la ../libutil.la

if HAVE_MPI
check_PROGRAMS += test-mpi-hre

test_mpi_hre_SOURCES = test-hre.c
test_mpi_hre_CPPFLAGS = -DTESTMPI  -I ${top_srcdir}/src $(AM_CPPFLAGS)
test_mpi_hre_LDADD = ../hre-mpi/libhre.la ../libltsmin-hre.la
endif


