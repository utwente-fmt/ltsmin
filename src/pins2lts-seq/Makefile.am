HRE_LIB = ../hre/libhre.la
SEQ_CPPFLAGS = $(AM_CPPFLAGS)
SEQ_CFLAGS   = $(AM_CFLAGS)

LIBS  = ../pins-lib/libpins.la
LIBS += ../vset-lib/libvset.la
LIBS += ../mc-lib/libmc.la

PROB_LIB_LIBS = ../prob-lib/libprob.la

bin_PROGRAMS = etf2lts-seq pins2lts-seq

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-seq
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-seq
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-seq
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-seq
endif

if HAVE_SPINS
bin_PROGRAMS += prom2lts-seq
endif

if HAVE_PNML
bin_PROGRAMS += pnml2lts-seq
endif

if HAVE_PROB
bin_PROGRAMS += prob2lts-seq
endif

# pins2lts-seq
pins2lts_seq_SOURCES   = pins2lts-seq.c
pins2lts_seq_SOURCES  += ../pins-lib/modules/dlopen-pins.c ../pins-lib/modules/dlopen-pins.h
pins2lts_seq_LDADD     = $(LIBS)
pins2lts_seq_CPPFLAGS  = -DPINS_DLL $(SEQ_CPPFLAGS)
if !BUILD_OS_IS_DARWIN
pins2lts_seq_LDFLAGS   = -Wl,-E
endif
nodist_EXTRA_pins2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset


# etf2lts-seq
etf2lts_seq_SOURCES   = pins2lts-seq.c
etf2lts_seq_SOURCES  += ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
etf2lts_seq_LDADD     = $(LIBS)
etf2lts_seq_CPPFLAGS  = -DETF $(SEQ_CPPFLAGS)
nodist_EXTRA_etf2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset

# lpo2lts-seq
lpo2lts_seq_SOURCES   = pins2lts-seq.c
lpo2lts_seq_LDADD     = ../pins-lib/libmcrl-pins.la
lpo2lts_seq_LDADD    += $(LIBS) $(MCRL_LIBS)
lpo2lts_seq_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(SEQ_CPPFLAGS)
lpo2lts_seq_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_seq_SOURCES = automake-force-linker.cxx #req. by mcrl

# lps2lts-seq
lps2lts_seq_SOURCES   = pins2lts-seq.c
lps2lts_seq_LDADD     = $(LIBS)
lps2lts_seq_LDADD    += ../pins-lib/libmcrl2-pins.la
lps2lts_seq_CPPFLAGS  = -DMCRL2 $(SEQ_CPPFLAGS)
lps2lts_seq_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_seq_SOURCES = automake-force-linker.cxx #req. by mcrl2s

# pbes2lts-seq
pbes2lts_seq_SOURCES   = pins2lts-seq.c
pbes2lts_seq_LDADD     = $(LIBS)
pbes2lts_seq_LDADD    += ../pins-lib/libpbes-pins.la
pbes2lts_seq_CPPFLAGS  = -DLTSMIN_PBES $(SEQ_CPPFLAGS)
pbes2lts_seq_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_seq_SOURCES = automake-force-linker.cxx #req. by pbes

# dve2lts-seq
dve2lts_seq_SOURCES   = pins2lts-seq.c
dve2lts_seq_SOURCES  += ../pins-lib/modules/dve-pins.c ../pins-lib/modules/dve-pins.h
dve2lts_seq_LDADD     = $(LIBS) $(DVEC_LIBS)
dve2lts_seq_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(SEQ_CPPFLAGS)
dve2lts_seq_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_dve2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset

# prom2lts-seq
prom2lts_seq_SOURCES  = pins2lts-seq.c
prom2lts_seq_SOURCES += ../pins-lib/modules/prom-pins.c ../pins-lib/modules/prom-pins.h
prom2lts_seq_LDADD    = $(LIBS) $(SPINS_LIBS)
prom2lts_seq_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(SEQ_CPPFLAGS)
prom2lts_seq_LDFLAGS  = $(SPINS_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_prom2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset

# pnml2lts-seq
pnml2lts_seq_SOURCES   = pins2lts-seq.c
pnml2lts_seq_SOURCES  += ../pins-lib/modules/pnml-pins.c ../pins-lib/modules/pnml-pins.h
pnml2lts_seq_LDADD     = $(PNML_LIBS) $(LIBS) ../andl-lib/libandl.la
pnml2lts_seq_CPPFLAGS  = -DPNML $(SEQ_CPPFLAGS)
pnml2lts_seq_CFLAGS    = $(SEQ_CFLAGS) $(PNML_CFLAGS)
nodist_EXTRA_pnml2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset

# prob2lts-seq
prob2lts_seq_SOURCES   = pins2lts-seq.c
prob2lts_seq_SOURCES  += ../pins-lib/modules/prob-pins.c ../pins-lib/modules/prob-pins.h 
prob2lts_seq_LDADD     = $(PROB_LIB_LIBS) $(LIBS)
prob2lts_seq_CFLAGS    = $(AM_CFLAGS) $(PROB_CFLAGS)
prob2lts_seq_CPPFLAGS  = -DPROB $(SEQ_CPPFLAGS)
nodist_EXTRA_prob2lts_seq_SOURCES = automake-force-linker.cxx # req. by vset
