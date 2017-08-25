SUBDIRS  = por

noinst_LTLIBRARIES  = libpins.la

pkginclude_HEADERS = dlopen-api.h pins.h pins-util.h

if HAVE_LIBSTEP_INFO
noinst_LTLIBRARIES += libmcrl-pins.la
endif

if HAVE_LIBMCRL2
noinst_LTLIBRARIES += libmcrl2-pins.la
endif

if HAVE_SCOOP
noinst_LTLIBRARIES += libmapa-pins.la
endif

if HAVE_LIBPBES
noinst_LTLIBRARIES += libpbes-pins.la
endif

# Warning overwriting user variable! (Avoid PBES warnings)
CXXFLAGS = @CXXFLAGS@ -Wno-unused-parameter

# libmapa-pins
libmapa_pins_la_SOURCES   = modules/mapa-pins.c modules/mapa-pins.h
libmapa_pins_la_CPPFLAGS  = -I${top_srcdir}/scoop/src $(AM_CPPFLAGS)
libmapa_pins_la_LDFLAGS   = $(AM_LDFLAGS)
#libmapa_pins_la_LDFLAGS   = -L${top_builddir}/scoop/src $(AM_LDFLAGS)
# libmapa_pins_la_LIBADD    = -lscoop 

# libmcrl-pins
libmcrl_pins_la_SOURCES   = modules/mcrl-pins.c modules/mcrl-pins.h
libmcrl_pins_la_SOURCES  += modules/at-map.c modules/at-map.h
libmcrl_pins_la_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl_pins_la_LDFLAGS   = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)
libmcrl_pins_la_LIBADD    = $(MCRL_LIBS)

# libmcrl2-pins
libmcrl2_pins_la_SOURCES  = modules/mcrl2-pins.cpp modules/mcrl2-pins.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_CXXFLAGS = $(MCRL2_PINS_CXXFLAGS) $(AM_CXXFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)

# libpbes-pins
libpbes_pins_la_SOURCES   = modules/pbes-pins.cpp modules/pbes-pins.h
libpbes_pins_la_CPPFLAGS  = -DLTSMIN_PBES $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_pins_la_CXXFLAGS  = $(MCRL2_PINS_CXXFLAGS) $(AM_CXXFLAGS)
libpbes_pins_la_LDFLAGS   = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libpbes_pins_la_LIBADD    = $(MCRL2_PINS_LIBS)

# libpins
libpins_la_SOURCES  =
libpins_la_SOURCES += pins.c pins.h
libpins_la_SOURCES += pins-util.c pins-util.h
libpins_la_SOURCES += pins-impl.h
libpins_la_SOURCES += pins2pins-cache.c pins2pins-cache.h
libpins_la_SOURCES += pins2pins-check.c pins2pins-check.h
libpins_la_SOURCES += pins2pins-fork.c pins2pins-fork.h
libpins_la_SOURCES += pins2pins-group.c pins2pins-group.h
libpins_la_SOURCES += pins2pins-guards.c pins2pins-guards.h
libpins_la_SOURCES += pg-types.c pg-types.h
libpins_la_SOURCES += pins2pins-mucalc.c pins2pins-mucalc.h
libpins_la_SOURCES += pins2pins-ltl.c pins2pins-ltl.h
libpins_la_SOURCES += property-semantics.c property-semantics.h
libpins_la_CFLAGS   = $(AM_CFLAGS) -I${top_srcdir}/ltl2ba
libpins_la_LIBADD   = ../lts-io/liblts-io.la
libpins_la_LIBADD  += ../ltsmin-lib/libmucalc.la
libpins_la_LIBADD  += ../hre-io/libhre-io.la
libpins_la_LIBADD  += ../hre/libhre.la
libpins_la_LIBADD  += ../ltsmin-lib/libltsmin.la
libpins_la_LIBADD  += ../dm/libdm.la
libpins_la_LIBADD  += por/libpor.la

if HAVE_BOOST
libpins_la_LIBADD  += ../dm/libdm_boost.la
endif

if HAVE_VIENNACL
libpins_la_LIBADD  += ../dm/libdm_viennacl.la
endif

