
HRE_LIB = ../hre/libhre.la
LIBS  =
LIBS += ../util-lib/libutil.la
LIBS += ../lts-io/liblts-io.la
LIBS += ../hre-io/libhre-io.la
LIBS += $(HRE_LIB)
LIBS += ../ltsmin-lib/libltsmin.la
LIBS += ../dm/libdm.la

noinst_LTLIBRARIES  = libpins.la

if HAVE_LIBSTEP_INFO
noinst_LTLIBRARIES += libmcrl-pins.la
endif

if HAVE_LIBMCRL2
noinst_LTLIBRARIES += libmcrl2-pins.la
endif

if HAVE_LIBPBES
noinst_LTLIBRARIES += libpbes-pins.la
endif

# Warning overwriting user variable! (Avoid PBES warnings)
#CXXFLAGS = @CXXFLAGS@ -Wno-unused-parameter

# libmcrl-pins
libmcrl_pins_la_SOURCES   = mcrl-pins.c mcrl-pins.h
libmcrl_pins_la_SOURCES  += at-map.c at-map.h
libmcrl_pins_la_CPPFLAGS  = -DMCRL -DNDEBUG $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl_pins_la_LDFLAGS   = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)
libmcrl_pins_la_LIBADD    = $(MCRL_LIBS) $(LIBS) 

# libmcrl2-pins
libmcrl2_pins_la_SOURCES  = mcrl2-pins.cpp mcrl2-pins.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_CXXFLAGS = $(MCRL2_PINS_CXXFLAGS) $(AM_CXXFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_PINS_LIBS) $(LIBS)

# libpbes-pins
libpbes_pins_la_SOURCES   = pbes-pins.cpp pbes-pins.h
libpbes_pins_la_CPPFLAGS  = -DPBES -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_pins_la_CXXFLAGS = $(MCRL2_PINS_CXXFLAGS) $(AM_CXXFLAGS)
libpbes_pins_la_LDFLAGS   = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libpbes_pins_la_LIBADD    = $(MCRL2_PINS_LIBS) $(LIBS)

# libpins
libpins_la_SOURCES  =
libpins_la_SOURCES += pins.c pins.h
libpins_la_SOURCES += pins-impl.h
libpins_la_SOURCES += pins2pins-cache.c
libpins_la_SOURCES += pins2pins-group.c
libpins_la_SOURCES += pg-types.c pg-types.h
libpins_la_SOURCES += pins2pins-mucalc.c pins2pins-mucalc.h
libpins_la_SOURCES += pins2pins-ltl.c
libpins_la_SOURCES += pins2pins-por.c
libpins_la_SOURCES += property-semantics.c property-semantics.h
libpins_la_CPPFLAGS = -DNXT $(AM_CPPFLAGS) -I${top_srcdir}/ltl2ba
libpins_la_LIBADD    =
libpins_la_LIBADD   += ../ltsmin-lib/libmucalc.la 
