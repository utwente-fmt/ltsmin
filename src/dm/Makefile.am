noinst_LTLIBRARIES     	= libdm.la
check_PROGRAMS  		= libdm_debug.la

# libdm
libdm_la_SOURCES       	= dm.c dm.h bitvector.c bitvector.h
libdm_la_CPPFLAGS      	= $(AM_CPPFLAGS)

#libdm_debug
libdm_debug_la_SOURCES 	= dm.c dm.h bitvector.c bitvector.h
libdm_debug_la_CPPFLAGS = -DDMDEBUG $(AM_CPPFLAGS)
