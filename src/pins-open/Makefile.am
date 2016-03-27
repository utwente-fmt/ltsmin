
DM_LIB = ../dm/libdm.la
HRE_LIB = ../hre/libhre.la
UTIL_LIB = ../util-lib/libutil.la $(HRE_LIB)
MC_LIB = ../mc-lib/libmc.la $(UTIL_LIB)
LTSMIN_LIB = ../ltsmin-lib/libltsmin.la $(UTIL_LIB)
PINS_LIB = ../pins-lib/libpins.la $(LTSMIN_LIB)
VSET_LIB = ../vset-lib/libvset.la

pkglib_LTLIBRARIES  = libetf-open.la
if HAVE_SPINS
pkglib_LTLIBRARIES += libprom-open.la
endif
if HAVE_LIBSTEP_INFO
pkglib_LTLIBRARIES += liblpo-open.la
endif
if HAVE_LIBMCRL2
pkglib_LTLIBRARIES += liblps-open.la
endif
if HAVE_DVE
pkglib_LTLIBRARIES += libdve-open.la
endif
##if HAVE_OPAAL
##pkglib_LTLIBRARIES += libopaal-open.la
##endif
if HAVE_LIBPBES
pkglib_LTLIBRARIES += libpbes-open.la
endif


# libetf-open.la
libetf_open_la_SOURCES   = pins-open.c ../pins-lib/modules/etf-pins.c
libetf_open_la_SOURCES  += ../pins-lib/etf-pins.h
libetf_open_la_CPPFLAGS  = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD    = $(PINS_LIB)

# liblpo-open.la
liblpo_open_la_SOURCES   = pins-open.c
liblpo_open_la_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(CADP_CPPFLAGS)
liblpo_open_la_CPPFLAGS += ../pins-lib/libmcrl-pins.la $(AM_CPPFLAGS)
liblpo_open_la_LIBADD    = $(PINS_LIB)
nodist_EXTRA_liblpo_open_la_SOURCES = automake-force-linker.cxx

# liblps-open.la
liblps_open_la_SOURCES   = pins-open.c
liblps_open_la_CPPFLAGS  = -DMCRL2 $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblps_open_la_LIBADD    = ../pins-lib/libmcrl2-pins.la $(PINS_LIB)
nodist_EXTRA_liblps_open_la_SOURCES = automake-force-linker.cxx

# libpbes-open.la
libpbes_open_la_SOURCES   = pins-open.c
libpbes_open_la_CPPFLAGS  = -DLTSMIN_PBES $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_open_la_LIBADD    = ../pins-lib/libpbes-pins.la $(PINS_LIB)
nodist_EXTRA_libpbes_open_la_SOURCES = automake-force-linker.cxx

# libdve-open.la
libdve_open_la_SOURCES   = pins-open.c ../pins-lib/modules/dve-pins.c
libdve_open_la_SOURCES  += ../pins-lib/dve-pins.h
libdve_open_la_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS)
libdve_open_la_CPPFLAGS += $(AM_CPPFLAGS)
libdve_open_la_LIBADD    = $(PINS_LIB)

### libopaal-open.la
##libopaal_open_la_SOURCES = pins-open.c ../pins-lib/modules/opaal-pins.c
##libopaal_open_la_SOURCES+= ../pins-lib/opaal-pins.h
##libopaal_open_la_CPPFLAGS= -DOPAAL $(OPAAL_CPPFLAGS) $(CADP_CPPFLAGS)
##libopaal_open_la_CPPFLAGS+=$(AM_CPPFLAGS)
##libopaal_open_la_LIBADD  = $(PINS_LIB)

# libprom-open.la
libprom_open_la_SOURCES  = pins-open.c ../pins-lib/modules/prom-pins.c
libprom_open_la_SOURCES += ../pins-lib/prom-pins.h
libprom_open_la_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(CADP_CPPFLAGS)
libprom_open_la_CPPFLAGS+= $(AM_CPPFLAGS)
libprom_open_la_LIBADD   = $(PINS_LIB)

