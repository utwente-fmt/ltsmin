HRE_LIB = ../hre/libhre.la
SYM_CPPFLAGS = $(AM_CPPFLAGS) $(PROFILER_CPPFLAGS)
SYM_LDFLAGS = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
PINS_LIBS  =
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += $(HRE_LIB)
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../util-lib/libutil.la
PINS_LIBS += ../dm/libdm.la
PINS_LIBS += ../spg-lib/libspg.la

VSET_LIBS = ../vset-lib/libvset.la

bin_PROGRAMS = pinsim pinsim-etf

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += pinsim-lpo
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += pinsim-lps
endif

if HAVE_SCOOP
bin_PROGRAMS += pinsim-mapa
endif

if HAVE_LIBPBES
bin_PROGRAMS += pinsim-pbes
endif

if HAVE_DVE
bin_PROGRAMS += pinsim-dve
endif

if HAVE_SPINS
bin_PROGRAMS += pinsim-prom
endif

if HAVE_SYLVAN
SYM_CPPFLAGS += $(SYLVAN_CPPFLAGS)
endif

# pinsim-etf
pinsim_etf_SOURCES   = pinsim.c
pinsim_etf_SOURCES  += ../pins-lib/etf-pins.c ../pins-lib/etf-pins.h
pinsim_etf_LDADD     = $(PINS_LIBS) $(VSET_LIBS)
pinsim_etf_CPPFLAGS  = -DETF $(SYM_CPPFLAGS)
pinsim_etf_sym_LDFLAGS   = $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_etf_SOURCES = automake-force-linker.cxx # req. by vset

# pinsim
pinsim_SOURCES   = pinsim.c
pinsim_SOURCES  += ../pins-lib/dlopen-pins.c ../pins-lib/dlopen-pins.h
pinsim_LDADD     = $(PINS_LIBS) $(VSET_LIBS)
pinsim_CPPFLAGS  = -DPINS_DLL $(SYM_CPPFLAGS)
pinsim_LDFLAGS   = $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_SOURCES = automake-force-linker.cxx # req. by vset

# pinsim-lpo
pinsim_lpo_SOURCES   = pinsim.c
pinsim_lpo_LDADD     = $(PINS_LIBS)
pinsim_lpo_LDADD    += ../pins-lib/libmcrl-pins.la
pinsim_lpo_LDADD    += $(VSET_LIBS) $(MCRL_LIBS)
pinsim_lpo_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(SYM_CPPFLAGS)
pinsim_lpo_LDFLAGS   = $(MCRL_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_lpo_SOURCES = automake-force-linker.cxx # req. by mcrl

# pinsim-lps
pinsim_lps_SOURCES   = pinsim.c
pinsim_lps_LDADD     = $(PINS_LIBS)
pinsim_lps_LDADD    += ../pins-lib/libmcrl2-pins.la
pinsim_lps_LDADD    += $(VSET_LIBS) $(MCRL2_LIBS)
pinsim_lps_CPPFLAGS  = -DMCRL2 $(SYM_CPPFLAGS)
pinsim_lps_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_lps_SOURCES = automake-force-linker.cxx # req. by mcrl2

# pinsim-mapa
pinsim_mapa_SOURCES   = pinsim.c
pinsim_mapa_LDADD     = ../pins-lib/libmapa-pins.la -lscoop
pinsim_mapa_LDADD    += $(PINS_LIBS) $(VSET_LIBS)
pinsim_mapa_LDFLAGS   = -Wl,-rpath,$(DESTDIR)$(pkglibdir) -L${top_builddir}/scoop/src $(SYM_LDFLAGS)
pinsim_mapa_CPPFLAGS  = -DMAPA $(SYM_CPPFLAGS)
nodist_EXTRA_pinsim_mapa_SOURCES = automake-force-linker.cxx # req. by vset

# pinsim-pbes
pinsim_pbes_SOURCES   = pinsim.c
pinsim_pbes_LDADD     = $(PINS_LIBS)
pinsim_pbes_LDADD    += $(VSET_LIBS) ../pins-lib/libpbes-pins.la
pinsim_pbes_CPPFLAGS  = -DLTSMIN_PBES $(SYM_CPPFLAGS)
pinsim_pbes_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_pbes_SOURCES = automake-force-linker.cxx # req. by pbes

# pinsim-dve
pinsim_dve_SOURCES   = pinsim.c
pinsim_dve_SOURCES  += ../pins-lib/dve-pins.c ../pins-lib/dve-pins.h
pinsim_dve_LDADD     = $(PINS_LIBS)
pinsim_dve_LDADD    += $(VSET_LIBS) $(DVE_LIBS)
pinsim_dve_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(SYM_CPPFLAGS)
pinsim_dve_LDFLAGS   = $(DIVINE_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_dve_SOURCES = automake-force-linker.cxx # req. by vset

# pinsim-prom
pinsim_prom_SOURCES  = pinsim.c
pinsim_prom_SOURCES += ../pins-lib/prom-pins.c ../pins-lib/prom-pins.h
pinsim_prom_LDADD    = $(PINS_LIBS)
pinsim_prom_LDADD   += $(VSET_LIBS) $(SPINS_LIBS)
pinsim_prom_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(SYM_CPPFLAGS)
pinsim_prom_LDFLAGS  = $(SPINS_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinsim_prom_SOURCES = automake-force-linker.cxx # req. by vset
