HRE_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)

noinst_LTLIBRARIES = libhre.la

# libhre(-mpi)
libhre_la_SOURCES  = user.h
libhre_la_SOURCES += hre_mpi.c
libhre_la_SOURCES += mpi_event_loop.c mpi_event_loop.h

libhre_la_CPPFLAGS = $(HRE_CPPFLAGS) $(AM_CPPFLAGS)
libhre_la_LIBADD = ../hre/libhre.la
