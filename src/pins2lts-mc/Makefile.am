HRE_LIB = ../hre/libhre.la
MC_CPPFLAGS = $(AM_CPPFLAGS)
PINS_LIBS  =
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += $(HRE_LIB)
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../util-lib/libutil.la
PINS_LIBS += ../dm/libdm.la
MC_LIBS	 = ../mc-lib/libmc.la
MC_LIBS	+= parallel/libparallel.la
MC_LIBS	+= algorithm/libalgorithm.la
MC_LIBS	+= $(PINS_LIBS)

PROB_LIBS = ../prob-lib/libprob.la

SUBDIRS  = parallel
SUBDIRS += algorithm

bin_PROGRAMS  = etf2lts-mc pins2lts-mc

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

if HAVE_SPINS
bin_PROGRAMS += prom2lts-mc
endif

if HAVE_OPAAL
bin_PROGRAMS += opaal2lts-mc
endif

if HAVE_PNML
bin_PROGRAMS += pnml2lts-mc
endif

if HAVE_PROB
bin_PROGRAMS += prob2lts-mc
endif

mc_sources  = pins2lts-mc.c

# etf2lts-mc
etf2lts_mc_SOURCES   = $(mc_sources)
etf2lts_mc_SOURCES  += ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
etf2lts_mc_LDADD     = $(MC_LIBS) 
etf2lts_mc_CPPFLAGS  = -DETF $(MC_CPPFLAGS)
nodist_EXTRA_etf2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

# pins2lts-mc
pins2lts_mc_SOURCES   = $(mc_sources)
pins2lts_mc_SOURCES  += ../pins-lib/modules/dlopen-pins.c ../pins-lib/modules/dlopen-pins.h
pins2lts_mc_LDADD     = $(MC_LIBS) 
pins2lts_mc_CPPFLAGS  = -DPINS_DLL $(MC_CPPFLAGS)
nodist_EXTRA_pins2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

## lpo2lts-mc
lpo2lts_mc_SOURCES   = $(mc_sources)
lpo2lts_mc_LDADD     = $(HRE_LIB) ../pins-lib/libmcrl-pins.la
lpo2lts_mc_LDADD    += $(MC_LIBS) $(MCRL_LIBS)
lpo2lts_mc_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(MC_CPPFLAGS)
lpo2lts_mc_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_mc_SOURCES = automake-force-linker.cxx

## lps2lts-mc
lps2lts_mc_SOURCES   = $(mc_sources)
lps2lts_mc_LDADD     = $(MC_LIBS)  ../pins-lib/libmcrl2-pins.la
lps2lts_mc_CPPFLAGS  = -DMCRL2 $(MC_CPPFLAGS)
lps2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_mc_SOURCES = automake-force-linker.cxx

## pbes2lts-mc
pbes2lts_mc_SOURCES   = $(mc_sources)
pbes2lts_mc_LDADD     = $(MC_LIBS) ../pins-lib/libpbes-pins.la
pbes2lts_mc_CPPFLAGS  = -DLTSMIN_PBES $(MC_CPPFLAGS)
pbes2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_mc_SOURCES = automake-force-linker.cxx

# dve2lts-mc
dve2lts_mc_SOURCES   = $(mc_sources)
dve2lts_mc_SOURCES  += ../pins-lib/modules/dve-pins.c ../pins-lib/modules/dve-pins.h
dve2lts_mc_LDADD     = $(MC_LIBS) $(DVEC_LIBS)
dve2lts_mc_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(MC_CPPFLAGS)
dve2lts_mc_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_dve2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

# prom2lts-mc
prom2lts_mc_SOURCES  = $(mc_sources)
prom2lts_mc_SOURCES += ../pins-lib/modules/prom-pins.c ../pins-lib/modules/prom-pins.h
prom2lts_mc_LDADD    = $(MC_LIBS) $(SPINS_LIBS)
prom2lts_mc_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(MC_CPPFLAGS)
prom2lts_mc_LDFLAGS  = $(SPINS_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_prom2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

# opaal2lts-mc
opaal2lts_mc_SOURCES   = $(mc_sources)
opaal2lts_mc_SOURCES  += ../pins-lib/modules/opaal-pins.c ../pins-lib/modules/opaal-pins.h
opaal2lts_mc_LDADD     = $(MC_LIBS) $(OPAAL_LDFLAGS) 
opaal2lts_mc_CPPFLAGS  = -DOPAAL $(MC_CPPFLAGS)
nodist_EXTRA_opaal2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

# pnml2lts-mc
pnml2lts_mc_SOURCES   = $(mc_sources)
pnml2lts_mc_SOURCES  += ../pins-lib/modules/pnml-pins.c ../pins-lib/modules/pnml-pins.h
pnml2lts_mc_LDADD     = $(XML_LIBS) $(MC_LIBS)
pnml2lts_mc_CPPFLAGS  = -DPNML $(MC_CPPFLAGS)
pnml2lts_mc_CFLAGS    = $(XML_CFLAGS)
nodist_EXTRA_pnml2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL

# prob2lts-mc
prob2lts_mc_SOURCES   = $(mc_sources)
prob2lts_mc_SOURCES  += ../pins-lib/modules/prob-pins.c ../pins-lib/modules/prob-pins.h
prob2lts_mc_LDADD     = $(PROB_LIBS) $(MC_LIBS)
prob2lts_mc_CPPFLAGS  = -DPROB $(MC_CPPFLAGS)
nodist_EXTRA_prob2lts_mc_SOURCES = automake-force-linker.cxx # required by Boost & ViennaCL
