HRE_LIB = ../hre/libhre.la
SYM_CPPFLAGS = $(AM_CPPFLAGS)
PINS_LIBS  =
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += $(HRE_LIB)
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../util-lib/libutil.la
PINS_LIBS += ../dm/libdm.la
VSET_LIBS = ../vset-lib/libvset.la

bin_PROGRAMS = etf2lts-sym

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-sym
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-sym
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-sym
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-sym
endif

if HAVE_SPINJA
bin_PROGRAMS += prom2lts-sym
endif

# etf2lts-sym
etf2lts_sym_SOURCES   = pins2lts-sym.c
etf2lts_sym_SOURCES  += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
etf2lts_sym_LDADD     = $(PINS_LIBS) $(VSET_LIBS)
etf2lts_sym_CPPFLAGS  = -DETF $(SYM_CPPFLAGS)

# lpo2lts-sym
lpo2lts_sym_SOURCES   = pins2lts-sym.c
lpo2lts_sym_LDADD     = $(PINS_LIBS)
lpo2lts_sym_LDADD    += ../pins-lib/libmcrl-pins.la
lpo2lts_sym_LDADD    += $(VSET_LIBS) $(MCRL_LIBS)
lpo2lts_sym_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(SYM_CPPFLAGS)
lpo2lts_sym_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lpo2lts_sym_SOURCES = ../automake-force-linker.cxx

# lps2lts-sym
lps2lts_sym_SOURCES   = pins2lts-sym.c
lps2lts_sym_LDADD     = $(PINS_LIBS)
lps2lts_sym_LDADD    += ../pins-lib/libmcrl2-pins.la
lps2lts_sym_LDADD    += $(VSET_LIBS) $(MCRL2_LIBS)
lps2lts_sym_CPPFLAGS  = -DMCRL2 $(SYM_CPPFLAGS)
lps2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_sym_SOURCES = ../automake-force-linker.cxx

# pbes2lts-sym
pbes2lts_sym_SOURCES   = pins2lts-sym.c
pbes2lts_sym_LDADD     = $(PINS_LIBS)
pbes2lts_sym_LDADD    += $(VSET_LIBS) ../pins-lib/libpbes-pins.la
if HAVE_LIBSPG
pbes2lts_sym_LDADD    += ../spg-lib/libspg.la
endif
pbes2lts_sym_CPPFLAGS  = -DPBES $(SYM_CPPFLAGS)
pbes2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_sym_SOURCES = ../automake-force-linker.cxx

# dve2lts-sym
dve2lts_sym_SOURCES   = pins2lts-sym.c
dve2lts_sym_SOURCES  += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
dve2lts_sym_LDADD     = $(PINS_LIBS)
dve2lts_sym_LDADD    += $(VSET_LIBS) $(DVE_LIBS)
dve2lts_sym_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(SYM_CPPFLAGS)
dve2lts_sym_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-sym
prom2lts_sym_SOURCES  = pins2lts-sym.c
prom2lts_sym_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
prom2lts_sym_LDADD    = $(PINS_LIBS)
prom2lts_sym_LDADD   += $(VSET_LIBS) $(SPINJA_LIBS)
prom2lts_sym_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(SYM_CPPFLAGS)
prom2lts_sym_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
