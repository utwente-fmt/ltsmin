OPEN_LIBS  = ../hre/libhre.la
OPEN_LIBS += ../util-lib/libutil.la
OPEN_LIBS += ../mc-lib/libmc.la
OPEN_LIBS += ../ltsmin-lib/libltsmin.la
OPEN_LIBS += ../pins-lib/libpins.la

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
libetf_open_la_SOURCES  += ../pins-lib/modules/etf-pins.h
libetf_open_la_CPPFLAGS  = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD    = $(OPEN_LIBS)

# liblpo-open.la
liblpo_open_la_SOURCES   = pins-open.c
liblpo_open_la_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(CADP_CPPFLAGS)
liblpo_open_la_CPPFLAGS += ../pins-lib/libmcrl-pins.la $(AM_CPPFLAGS)
liblpo_open_la_LIBADD    = $(OPEN_LIBS)
nodist_EXTRA_liblpo_open_la_SOURCES = automake-force-linker.cxx

# liblps-open.la
liblps_open_la_SOURCES   = pins-open.c
liblps_open_la_CPPFLAGS  = -DMCRL2 $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblps_open_la_LIBADD    = ../pins-lib/libmcrl2-pins.la $(OPEN_LIBS)
nodist_EXTRA_liblps_open_la_SOURCES = automake-force-linker.cxx

# libpbes-open.la
libpbes_open_la_SOURCES   = pins-open.c
libpbes_open_la_CPPFLAGS  = -DLTSMIN_PBES $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_open_la_LIBADD    = ../pins-lib/libpbes-pins.la $(OPEN_LIBS)
nodist_EXTRA_libpbes_open_la_SOURCES = automake-force-linker.cxx

# libdve-open.la
libdve_open_la_SOURCES   = pins-open.c ../pins-lib/modules/dve-pins.c
libdve_open_la_SOURCES  += ../pins-lib/modules/dve-pins.h
libdve_open_la_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS)
libdve_open_la_CPPFLAGS += $(AM_CPPFLAGS)
libdve_open_la_LIBADD    = $(OPEN_LIBS)

### libopaal-open.la
##libopaal_open_la_SOURCES = pins-open.c ../pins-lib/modules/opaal-pins.c
##libopaal_open_la_SOURCES+= ../pins-lib/opaal-pins.h
##libopaal_open_la_CPPFLAGS= -DOPAAL $(OPAAL_CPPFLAGS) $(CADP_CPPFLAGS)
##libopaal_open_la_CPPFLAGS+=$(AM_CPPFLAGS)
##libopaal_open_la_LIBADD  = $(PINS_LIB)

# libprom-open.la
libprom_open_la_SOURCES  = pins-open.c ../pins-lib/modules/prom-pins.c
libprom_open_la_SOURCES += ../pins-lib/modules/prom-pins.h
libprom_open_la_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(CADP_CPPFLAGS)
libprom_open_la_CPPFLAGS+= $(AM_CPPFLAGS)
libprom_open_la_LIBADD   = $(OPEN_LIBS)

