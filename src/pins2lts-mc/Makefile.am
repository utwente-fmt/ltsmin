HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = $(AM_CPPFLAGS)
PINS_LIBS  =
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += $(HRE_LIB)
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../util-lib/libutil.la
PINS_LIBS += ../dm/libdm.la
MC_LIBS = ../mc-lib/libmc.la $(PINS_LIBS)

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

mc_sources  = algorithm/algorithm.c
mc_sources += algorithm/algorithm.h
mc_sources += algorithm/algorithm_object.h
mc_sources += algorithm/cndfs.c
mc_sources += algorithm/cndfs.h
mc_sources += algorithm/dfs-fifo.c
mc_sources += algorithm/dfs-fifo.h
mc_sources += algorithm/lndfs.c
mc_sources += algorithm/lndfs.h
mc_sources += algorithm/ltl.c
mc_sources += algorithm/ltl.h
mc_sources += algorithm/ndfs.c
mc_sources += algorithm/ndfs.h
mc_sources += algorithm/owcty.c
mc_sources += algorithm/owcty.h
mc_sources += algorithm/reach.c
mc_sources += algorithm/reach.h
mc_sources += algorithm/timed-cndfs.c
mc_sources += algorithm/timed-cndfs.h
mc_sources += algorithm/timed.c
mc_sources += algorithm/timed.h
mc_sources += parallel/color.c
mc_sources += parallel/color.h
mc_sources += parallel/global.c
mc_sources += parallel/global.h
mc_sources += parallel/options.c
mc_sources += parallel/options.h
mc_sources += parallel/permute.c
mc_sources += parallel/permute.h
mc_sources += parallel/run.h
mc_sources += parallel/run.c
mc_sources += parallel/state-store.c
mc_sources += parallel/state-store.h
mc_sources += parallel/state-info.c
mc_sources += parallel/state-info.h
mc_sources += parallel/worker.h
mc_sources += parallel/worker.c
mc_sources += pins2lts-mc.c

# etf2lts-mc
etf2lts_mc_SOURCES   = $(mc_sources)
etf2lts_mc_SOURCES  += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
etf2lts_mc_LDADD     = $(MC_LIBS) 
etf2lts_mc_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# pins2lts-mc
pins2lts_mc_SOURCES   = $(mc_sources)
pins2lts_mc_SOURCES  += ../pins-lib/dlopen-pins.c ../pins-lib/dlopen-pins.h
pins2lts_mc_LDADD     = $(MC_LIBS) 
pins2lts_mc_CPPFLAGS  = -DPINS_DLL $(DIST_CPPFLAGS)

## lpo2lts-mc
lpo2lts_mc_SOURCES   = $(mc_sources)
lpo2lts_mc_LDADD     = $(HRE_LIB) ../pins-lib/libmcrl-pins.la
lpo2lts_mc_LDADD    += $(MC_LIBS) $(MCRL_LIBS)
lpo2lts_mc_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_mc_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_mc_SOURCES = automake-force-linker.cxx

## lps2lts-mc
lps2lts_mc_SOURCES   = $(mc_sources)
lps2lts_mc_LDADD     = $(MC_LIBS)  ../pins-lib/libmcrl2-pins.la
lps2lts_mc_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_mc_SOURCES = automake-force-linker.cxx

## pbes2lts-mc
pbes2lts_mc_SOURCES   = $(mc_sources)
pbes2lts_mc_LDADD     = $(MC_LIBS) ../pins-lib/libpbes-pins.la
pbes2lts_mc_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_mc_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_mc_SOURCES = automake-force-linker.cxx

# dve2lts-mc
dve2lts_mc_SOURCES   = $(mc_sources)
dve2lts_mc_SOURCES  += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
dve2lts_mc_LDADD     = $(MC_LIBS) $(DVEC_LIBS)
dve2lts_mc_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_mc_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-mc
prom2lts_mc_SOURCES  = $(mc_sources)
prom2lts_mc_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
prom2lts_mc_LDADD    = $(MC_LIBS) $(SPINS_LIBS)
prom2lts_mc_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_mc_LDFLAGS  = $(SPINS_LDFLAGS) $(AM_LDFLAGS)

# opaal2lts-mc
opaal2lts_mc_SOURCES   = $(mc_sources)
opaal2lts_mc_SOURCES  += ../pins-lib/opaal-pins.c ../pins-lib/opaal-pins.h
opaal2lts_mc_LDADD     = $(MC_LIBS) $(OPAAL_LDFLAGS) 
opaal2lts_mc_CPPFLAGS  = -DOPAAL $(DIST_CPPFLAGS)
