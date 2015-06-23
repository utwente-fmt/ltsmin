noinst_LTLIBRARIES     	= libdm.la

if HAVE_BOOST
noinst_LTLIBRARIES		+= libdm_boost.la
endif

if HAVE_VIENNACL 
noinst_LTLIBRARIES		+=  libdm_viennacl.la
endif

check_LTLIBRARIES  		= libdm_debug.la

pkginclude_HEADERS = dm.h bitvector.h

# libdm_boost
libdm_boost_la_SOURCES  = dm_boost.cpp dm_boost.h
libdm_boost_la_CXXFLAGS = $(AM_CXXFLAGS) $(BOOST_CPPFLAGS)
libdm_boost_la_LIBADD   = ../hre/libhre.la
libdm_boost_la_LDFLAGS  = $(BOOST_LDFLAGS)

# libdm_viennacl
libdm_viennacl_la_SOURCES   = dm_viennacl.cpp dm_viennacl.h
libdm_viennacl_la_CXXFLAGS  = $(AM_CXXFLAGS) $(VIENNACL_CPPFLAGS)
libdm_viennacl_la_LIBADD    = ../hre/libhre.la $(VIENNACL_LIBS)

# libdm
libdm_la_SOURCES       	= dm.c dm.h bitvector.c bitvector.h
libdm_la_CPPFLAGS      	= $(AM_CPPFLAGS)

#libdm_debug
libdm_debug_la_SOURCES 	= dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_CPPFLAGS = -DDMDEBUG $(AM_CPPFLAGS)
