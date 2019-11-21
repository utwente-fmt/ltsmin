noinst_LTLIBRARIES     	= libdm.la

if HAVE_BOOST
noinst_LTLIBRARIES		+= libdm_boost.la
endif

if HAVE_VIENNACL 
noinst_LTLIBRARIES		+=  libdm_viennacl.la
endif

check_LTLIBRARIES  		= libdm_debug.la

pkginclude_HEADERS = dm.h bitvector.h

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# libdm_boost
libdm_boost_la_SOURCES  = dm_boost.cpp dm_boost.h
libdm_boost_la_LIBADD   = $(top_builddir)/lib/libgnu.la
libdm_boost_la_CXXFLAGS = $(AM_CXXFLAGS) $(BOOST_CPPFLAGS)
# libdm_boost_la_LDFLAGS  = $(BOOST_LDFLAGS)
# above line is commented because BOOST_LDFLAGS always contains an -L flag,
# which does not allow is to properly use -L by ourselves.
# E.g. when we specify a library path that contains libpopt.a, 
# the value given by -L in BOOST_LDFLAGS always takes precedence.
# So if libpopt can also be found in the -L value in BOOST_LDFLAGS this value will take
# precedence, instead of the value in our -L flag (given by LDFLAGS.

# libdm_viennacl
libdm_viennacl_la_SOURCES   = dm_viennacl.cpp dm_viennacl.h
libdm_viennacl_la_CXXFLAGS  = $(AM_CXXFLAGS) $(VIENNACL_CPPFLAGS)
libdm_viennacl_la_LIBADD    = $(VIENNACL_LIBS)
libdm_viennacl_la_LIBADD   += $(top_builddir)/lib/libgnu.la

# libdm
libdm_la_SOURCES       	= dm.c dm.h bitvector.c bitvector.h
libdm_la_LIBADD         = $(top_builddir)/lib/libgnu.la

#libdm_debug
libdm_debug_la_SOURCES 	= dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_CPPFLAGS = -DDMDEBUG $(AM_CPPFLAGS)
