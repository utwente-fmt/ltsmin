SUBDIRS = maxsum 

SYM_CPP_FLAGS = $(AM_CPPFLAGS) $(PROFILER_CPPFLAGS)
SYM_LD_FLAGS = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
SYM_C_FLAGS = $(AM_CFLAGS) $(PROFILER_CFLAGS)

LIBZZZ = ../pins-lib/libpins.la
LIBZZZ += ../vset-lib/libvset.la
LIBZZZ += maxsum/libmaxsum.la
LIBZZZ += ../spg-lib/libspg.la
LIBZZZ += ../mc-lib/libmc.la

if HAVE_SYLVAN
LIBZZZ += $(SYLVAN_LIBS)
SYM_C_FLAGS += $(SYLVAN_CFLAGS)
endif

PROB_LIB_LIBS = ../prob-lib/libprob.la

*2lts-sym$(EXEEXT): pins2lts-sym.c

bin_PROGRAMS = etf2lts-sym pins2lts-sym

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-sym
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-sym
endif

if HAVE_SCOOP
bin_PROGRAMS += mapa2lts-sym
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-sym
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-sym
endif

if HAVE_SPINS
bin_PROGRAMS += prom2lts-sym
endif

if HAVE_PNML
bin_PROGRAMS += pnml2lts-sym
endif

if HAVE_PROB
bin_PROGRAMS += prob2lts-sym
endif

# etf2lts-sym
etf2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
etf2lts_sym_SOURCES  += ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
etf2lts_sym_LDADD     = $(LIBZZZ)
etf2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
etf2lts_sym_CPPFLAGS  = -DETF $(SYM_CPP_FLAGS)
etf2lts_sym_LDFLAGS   = $(SYM_LDFLAGS)
nodist_EXTRA_etf2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# pins2lts-sym
pins2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
pins2lts_sym_SOURCES  += ../pins-lib/modules/dlopen-pins.c ../pins-lib/modules/dlopen-pins.h
pins2lts_sym_LDADD     = $(LIBZZZ)
pins2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
pins2lts_sym_CPPFLAGS  = -DPINS_DLL $(SYM_CPP_FLAGS)
if !BUILD_OS_IS_DARWIN
pins2lts_sym_LDFLAGS   = -Wl,-E $(SYM_LD_FLAGS)
endif
nodist_EXTRA_pins2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# lpo2lts-sym
lpo2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
lpo2lts_sym_LDADD     = $(LIBZZZ) $(MCRL_LIBS)
lpo2lts_sym_LDADD    += ../pins-lib/libmcrl-pins.la
lpo2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
lpo2lts_sym_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(SYM_CPP_FLAGS)
lpo2lts_sym_LDFLAGS   = $(MCRL_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_lpo2lts_sym_SOURCES = automake-force-linker.cxx # req. by mcrl

# lps2lts-sym
lps2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
lps2lts_sym_LDADD     = $(LIBZZZ) 
lps2lts_sym_LDADD    += ../pins-lib/libmcrl2-pins.la
lps2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
lps2lts_sym_CPPFLAGS  = -DMCRL2 $(SYM_CPP_FLAGS)
lps2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_lps2lts_sym_SOURCES = automake-force-linker.cxx # req. by mcrl2

# mapa2lts-sym
mapa2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
mapa2lts_sym_LDADD     = ../pins-lib/libmapa-pins.la
mapa2lts_sym_LDADD    += $(LIBZZZ) -lscoop
mapa2lts_sym_LDFLAGS   = -L${top_builddir}/scoop/src $(SYM_LD_FLAGS)
mapa2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
mapa2lts_sym_CPPFLAGS  = -DMAPA $(SYM_CPP_FLAGS)
nodist_EXTRA_mapa2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# pbes2lts-sym
pbes2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
pbes2lts_sym_LDADD     = $(LIBZZZ)
pbes2lts_sym_LDADD    += ../pins-lib/libpbes-pins.la
pbes2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
pbes2lts_sym_CPPFLAGS  = -DLTSMIN_PBES $(SYM_CPP_FLAGS)
pbes2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_pbes2lts_sym_SOURCES = automake-force-linker.cxx # req. by pbes

# dve2lts-sym
dve2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
dve2lts_sym_SOURCES  += ../pins-lib/modules/dve-pins.c ../pins-lib/modules/dve-pins.h
dve2lts_sym_LDADD     = $(LIBZZZ) $(DVE_LIBS)
dve2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
dve2lts_sym_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(SYM_CPP_FLAGS)
dve2lts_sym_LDFLAGS   = $(DIVINE_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_dve2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# prom2lts-sym
prom2lts_sym_SOURCES  = options.c pins2lts-sym.c options.h
prom2lts_sym_SOURCES += ../pins-lib/modules/prom-pins.c ../pins-lib/modules/prom-pins.h
prom2lts_sym_LDADD    = $(LIBZZZ) $(SPINS_LIBS)
prom2lts_sym_CFLAGS    = $(SYM_C_FLAGS)
prom2lts_sym_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(SYM_CPP_FLAGS)
prom2lts_sym_LDFLAGS  = $(SPINS_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_prom2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# pnml2lts-sym
pnml2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
pnml2lts_sym_SOURCES  += ../pins-lib/modules/pnml-pins.c ../pins-lib/modules/pnml-pins.h
pnml2lts_sym_LDADD     = $(PNML_LIBS) $(LIBZZZ) ../andl-lib/libandl.la
pnml2lts_sym_CPPFLAGS  = -DPNML $(SYM_CPP_FLAGS)
pnml2lts_sym_LDFLAGS   = $(SYM_LD_FLAGS)
pnml2lts_sym_CFLAGS    = $(SYM_C_FLAGS) $(PNML_CFLAGS)
nodist_EXTRA_pnml2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset

# prob2lts-sym
prob2lts_sym_SOURCES   = options.c pins2lts-sym.c options.h
prob2lts_sym_SOURCES  += ../pins-lib/modules/prob-pins.c ../pins-lib/modules/prob-pins.h
prob2lts_sym_LDADD     = $(PROB_LIB_LIBS) $(LIBZZZ)
prob2lts_sym_CFLAGS    = $(SYM_CFLAGS) $(PROB_CFLAGS)
prob2lts_sym_CPPFLAGS  = -DPROB $(SYM_CPP_FLAGS)
prob2lts_sym_LDFLAGS   = $(SYM_LD_FLAGS)
nodist_EXTRA_prob2lts_sym_SOURCES = automake-force-linker.cxx # req. by vset
