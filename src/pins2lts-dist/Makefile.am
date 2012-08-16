if HAVE_MPI
HRE_LIB = ../hre-mpi/libhre.la
DIST_CPPFLAGS = -DUSEMPI $(AM_CPPFLAGS)
else
HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = $(AM_CPPFLAGS)
endif
PINS_LIBS  =
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += ../hre/libhre.la
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../util-lib/libutil.la
PINS_LIBS += ../dm/libdm.la

bin_PROGRAMS = etf2lts-dist

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-dist
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-dist
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-dist
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-dist
endif

if HAVE_SPINJA
bin_PROGRAMS += prom2lts-dist
endif

# etf2lts-dist
etf2lts_dist_SOURCES   = pins2lts-dist.c
etf2lts_dist_SOURCES  += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
etf2lts_dist_LDADD     = $(HRE_LIB) $(PINS_LIBS)
etf2lts_dist_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# lpo2lts-dist
lpo2lts_dist_SOURCES   = pins2lts-dist.c
lpo2lts_dist_LDADD     = $(HRE_LIB) $(PINS_LIBS)
lpo2lts_dist_LDADD    += ../pins-lib/libmcrl-pins.la
lpo2lts_dist_LDADD    += $(MCRL_LIBS)
lpo2lts_dist_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_dist_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_dist_SOURCES = ../automake-force-linker.cxx

# lps2lts-dist
lps2lts_dist_SOURCES   = pins2lts-dist.c
lps2lts_dist_LDADD     = $(HRE_LIB) $(PINS_LIBS)
lps2lts_dist_LDADD    += ../pins-lib/libmcrl2-pins.la
lps2lts_dist_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_dist_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_dist_SOURCES = ../automake-force-linker.cxx

# pbes2lts-dist
pbes2lts_dist_SOURCES   = pins2lts-dist.c
pbes2lts_dist_LDADD     = $(HRE_LIB) $(PINS_LIBS)
pbes2lts_dist_LDADD    += ../pins-lib/libpbes-pins.la
pbes2lts_dist_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_dist_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_dist_SOURCES = ../automake-force-linker.cxx

# dve2lts-dist
dve2lts_dist_SOURCES   = pins2lts-dist.c
dve2lts_dist_SOURCES  += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
dve2lts_dist_LDADD     = $(HRE_LIB) $(PINS_LIBS)
dve2lts_dist_LDADD    += $(DVEC_LIBS)
dve2lts_dist_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_dist_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-dist
prom2lts_dist_SOURCES  = pins2lts-dist.c
prom2lts_dist_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
prom2lts_dist_LDADD    = $(HRE_LIB) $(PINS_LIBS)
prom2lts_dist_LDADD   += $(SPINJA_LIBS)
prom2lts_dist_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_dist_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
