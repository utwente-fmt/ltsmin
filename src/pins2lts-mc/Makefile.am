HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = $(AM_CPPFLAGS)
PINS_LIBS  = ../pins-lib/libpins.la ../util-lib/libutil.la ../hre/libhre.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la ../dm/libdm.la ../lts-io/liblts-io.la
PINS_LIBS +=  ../hre-io/libhre-io.la
MC_LIBS = ../mc-lib/libmc.la $(PINS_LIBS)

bin_PROGRAMS  = etf2lts-mc

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-mc
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-mc
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-mc
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-mc
endif

if HAVE_SPINJA
bin_PROGRAMS += prom2lts-mc
endif

if HAVE_OPAAL
bin_PROGRAMS += opaal2lts-mc
endif

# etf2lts-mc
etf2lts_mc_SOURCES   = pins2lts-mc.c
etf2lts_mc_SOURCES  += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
etf2lts_mc_LDADD     = $(HRE_LIB)
etf2lts_mc_LDADD    += $(MC_LIBS) 
etf2lts_mc_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

## lpo2lts-mc
lpo2lts_mc_SOURCES   = pins2lts-mc.c
lpo2lts_mc_LDADD     = $(HRE_LIB) ../pins-lib/libmcrl-pins.la
lpo2lts_mc_LDADD    += $(MC_LIBS) $(MCRL_LIBS)
lpo2lts_mc_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_mc_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_mc_SOURCES = ../automake-force-linker.cxx

## lps2lts-mc
lps2lts_mc_SOURCES   = pins2lts-mc.c
lps2lts_mc_LDADD     = $(HRE_LIB)
lps2lts_mc_LDADD    += $(MC_LIBS)  ../pins-lib/libmcrl2-pins.la
lps2lts_mc_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_mc_SOURCES = ../automake-force-linker.cxx

## pbes2lts-mc
pbes2lts_mc_SOURCES   = pins2lts-mc.c
pbes2lts_mc_LDADD     = $(HRE_LIB)
pbes2lts_mc_LDADD    += $(MC_LIBS) ../pins-lib/libpbes-pins.la
pbes2lts_mc_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_mc_SOURCES = ../automake-force-linker.cxx

# dve2lts-mc
dve2lts_mc_SOURCES   = pins2lts-mc.c
dve2lts_mc_SOURCES  += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
dve2lts_mc_LDADD     = $(HRE_LIB)
dve2lts_mc_LDADD    += $(MC_LIBS) $(DVEC_LIBS)
dve2lts_mc_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_mc_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-mc
prom2lts_mc_SOURCES  = pins2lts-mc.c
prom2lts_mc_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
prom2lts_mc_LDADD    = $(HRE_LIB)
prom2lts_mc_LDADD   += $(MC_LIBS) $(SPINJA_LIBS)
prom2lts_mc_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_mc_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)

# opaal2lts-mc
opaal2lts_mc_SOURCES   = pins2lts-mc.c
opaal2lts_mc_SOURCES  += ../pins-lib/opaal-pins.c ../pins-lib/opaal-pins.h
opaal2lts_mc_LDADD     = $(HRE_LIB)
opaal2lts_mc_LDADD    += $(MC_LIBS) $(OPAAL_LDFLAGS) 
opaal2lts_mc_CPPFLAGS  = -DOPAAL $(DIST_CPPFLAGS)
