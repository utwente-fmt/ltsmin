HRE_LIB = ../hre/libhre.la
SYM_CPP_FLAGS = $(AM_CPPFLAGS) $(PROFILER_CPPFLAGS)
SYM_LD_FLAGS = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
SYM_C_FLAGS = $(AM_CFLAGS) $(PROFILER_CFLAGS)
LIBZZZ  =
LIBZZZ += ../lts-lib/liblts-lib.la
LIBZZZ += ../lts-io/liblts-io.la
LIBZZZ += ../hre-io/libhre-io.la
LIBZZZ += $(HRE_LIB)
LIBZZZ += ../pins-lib/libpins.la
LIBZZZ += ../ltsmin-lib/libltsmin.la
LIBZZZ += ../util-lib/libutil.la
LIBZZZ += ../dm/libdm.la
LIBZZZ += ../spg-lib/libspg.la
LIBZZZ += ../vset-lib/libvset.la

bin_PROGRAMS = pinssim pinssim-etf

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += pinssim-lpo
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += pinssim-lps
endif

if HAVE_SCOOP
bin_PROGRAMS += pinssim-mapa
endif

if HAVE_LIBPBES
bin_PROGRAMS += pinssim-pbes
endif

if HAVE_DVE
bin_PROGRAMS += pinssim-dve
endif

if HAVE_SPINS
bin_PROGRAMS += pinssim-prom
endif

if HAVE_SYLVAN
SYM_C_FLAGS += $(SYLVAN_CFLAGS)
LIBZZZ += $(SYLVAN_LIBS)
endif

# pinssim-etf
pinssim_etf_SOURCES   = pinssim.c
pinssim_etf_SOURCES  += ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
pinssim_etf_LDADD     = $(LIBZZZ)
pinssim_etf_CFLAGS    = $(SYM_C_FLAGS)
pinssim_etf_CPPFLAGS  = -DETF $(SYM_CPP_FLAGS)
pinssim_etf_LDFLAGS   = $(SYM_LD_FLAGS)
nodist_EXTRA_pinssim_etf_SOURCES = automake-force-linker.cxx # req. by vset

# pinssim
pinssim_SOURCES   = pinssim.c
pinssim_SOURCES  += ../pins-lib/modules/dlopen-pins.c ../pins-lib/modules/dlopen-pins.h
pinssim_LDADD     = $(LIBZZZ)
pinssim_CFLAGS    = $(SYM_C_FLAGS)
pinssim_CPPFLAGS  = -DPINS_DLL $(SYM_CPP_FLAGS)
pinssim_LDFLAGS   = $(SYM_LD_FLAGS)
nodist_EXTRA_pinssim_SOURCES = automake-force-linker.cxx # req. by vset

# pinssim-lpo
pinssim_lpo_SOURCES   = pinssim.c
pinssim_lpo_LDADD     = $(LIBZZZ)
pinssim_lpo_LDADD    += ../pins-lib/libmcrl-pins.la
pinssim_lpo_LDADD    += $(MCRL_LIBS)
pinssim_lpo_CFLAGS    = $(SYM_C_FLAGS)
pinssim_lpo_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(SYM_CPP_FLAGS)
pinssim_lpo_LDFLAGS   = $(MCRL_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_pinssim_lpo_SOURCES = automake-force-linker.cxx # req. by mcrl

# pinssim-lps
pinssim_lps_SOURCES   = pinssim.c
pinssim_lps_LDADD     = $(LIBZZZ)
pinssim_lps_LDADD    += ../pins-lib/libmcrl2-pins.la
pinssim_lps_LDADD    += $(MCRL2_LIBS)
pinssim_lps_CFLAGS    = $(SYM_C_FLAGS)
pinssim_lps_CPPFLAGS  = -DMCRL2 $(SYM_CPP_FLAGS)
pinssim_lps_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_pinssim_lps_SOURCES = automake-force-linker.cxx # req. by mcrl2

# pinssim-mapa
pinssim_mapa_SOURCES   = pinssim.c
pinssim_mapa_LDADD     = ../pins-lib/libmapa-pins.la -lscoop
pinssim_mapa_LDADD    += $(LIBZZZ)
pinssim_mapa_LDFLAGS   = -Wl,-rpath,$(DESTDIR)$(pkglibdir) -L${top_builddir}/scoop/src $(SYM_LD_FLAGS)
pinssim_mapa_CFLAGS    = $(SYM_C_FLAGS)
pinssim_mapa_CPPFLAGS  = -DMAPA $(SYM_CPP_FLAGS)
nodist_EXTRA_pinssim_mapa_SOURCES = automake-force-linker.cxx # req. by vset

# pinssim-pbes
pinssim_pbes_SOURCES   = pinssim.c
pinssim_pbes_LDADD     = $(LIBZZZ)
pinssim_pbes_LDADD    += ../pins-lib/libpbes-pins.la
pinssim_pbes_CFLAGS    = $(SYM_C_FLAGS)
pinssim_pbes_CPPFLAGS  = -DLTSMIN_PBES $(SYM_CPP_FLAGS)
pinssim_pbes_LDFLAGS   = $(MCRL2_LDFLAGS) $(SYM_LD_FLAGS)
nodist_EXTRA_pinssim_pbes_SOURCES = automake-force-linker.cxx # req. by pbes

# pinssim-dve
pinssim_dve_SOURCES   = pinssim.c
pinssim_dve_SOURCES  += ../pins-lib/modules/dve-pins.c ../pins-lib/modules/dve-pins.h
pinssim_dve_LDADD     = $(LIBZZZ)
pinssim_dve_LDADD    += $(DVE_LIBS)
pinssim_dve_CFLAGS    = $(SYM_C_FLAGS)
pinssim_dve_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(SYM_CPPFLAGS)
pinssim_dve_LDFLAGS   = $(DIVINE_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinssim_dve_SOURCES = automake-force-linker.cxx # req. by vset

# pinssim-prom
pinssim_prom_SOURCES  = pinssim.c
pinssim_prom_SOURCES += ../pins-lib/modules/prom-pins.c ../pins-lib/modules/prom-pins.h
pinssim_prom_LDADD    = $(LIBZZZ)
pinssim_prom_LDADD   += $(SPINS_LIBS)
pinssim_prom_CFLAGS    = $(SYM_C_FLAGS)
pinssim_prom_CPPFLAGS = -DSPINS $(SPINS_CPPFLAGS) $(SYM_CPPFLAGS)
pinssim_prom_LDFLAGS  = $(SPINS_LDFLAGS) $(SYM_LDFLAGS)
nodist_EXTRA_pinssim_prom_SOURCES = automake-force-linker.cxx # req. by vset
