
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

libmcrl_pins_la_SOURCES  = mcrl-pins.c mcrl-pins.h
libmcrl_pins_la_SOURCES += at-map.c at-map.h
libmcrl_pins_la_CPPFLAGS = -DMCRL -DNDEBUG $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl_pins_la_LDFLAGS  = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)
libmcrl_pins_la_LIBADD   = $(MCRL_LIBS) 

libmcrl2_pins_la_SOURCES  = mcrl2-pins.cpp mcrl2-pins.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)

libpbes_pins_la_SOURCES  = pbes-pins.cpp pbes-pins.h
libpbes_pins_la_CPPFLAGS = -DPBES -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libpbes_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)

libpins_la_SOURCES  =
## libpins_la_SOURCES  = at-map.c at-map.h 	## include separately
										   	## just like the pins implementations
										   	## *-pins.[ch].
libpins_la_SOURCES += pins.c pins.h
libpins_la_SOURCES += pins2pins-cache.c
libpins_la_SOURCES += pins2pins-group.c
libpins_la_SOURCES += pins2pins-ltl.c
libpins_la_SOURCES += pins2pins-por.c
libpins_la_SOURCES += property-semantics.c property-semantics.h

libpins_la_LIBADD   =
libpins_la_LIBADD  += $(LTSMIN_LIB)
libpins_la_LIBADD  += ../hre/libhre.la
libpins_la_LIBADD  += ../hre-io/libhre-io.la 
libpins_la_LIBADD  += ../lts-io/liblts-io.la
libpins_la_LIBADD  += ../dm/libdm.la

libpins_la_CPPFLAGS  = -DNXT
libpins_la_CPPFLAGS += -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)