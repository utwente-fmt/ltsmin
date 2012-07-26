if HAVE_MPI
HRE_LIB = ../hre-mpi/libhre.la
DIST_CPPFLAGS = -DUSEMPI -I ${top_srcdir}/src $(AM_CPPFLAGS)
else
HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
endif

VSET_LIBS = ../libvset.la

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
etf2lts_sym_SOURCES  += ../etf-greybox.c ../etf-greybox.h
etf2lts_sym_LDADD     = $(VSET_LIBS) ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
etf2lts_sym_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# lpo2lts-sym
lpo2lts_sym_SOURCES   = pins2lts-sym.c
lpo2lts_sym_SOURCES  += ../mcrl-greybox.c ../mcrl-greybox.h
lpo2lts_sym_SOURCES  += ../at-map.c ../at-map.h
lpo2lts_sym_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lpo2lts_sym_LDADD    += $(VSET_LIBS) $(MCRL_LIBS)
lpo2lts_sym_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_sym_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-sym
lps2lts_sym_SOURCES   = pins2lts-sym.c
lps2lts_sym_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lps2lts_sym_LDADD    += $(VSET_LIBS)  ../libmcrl2-pins.la
lps2lts_sym_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_sym_SOURCES = ../automake-force-linker.cxx

# pbes2lts-sym
pbes2lts_sym_SOURCES   = pins2lts-sym.c
pbes2lts_sym_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
pbes2lts_sym_LDADD    += $(VSET_LIBS)  ../libpbes-pins.la
pbes2lts_sym_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_sym_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_sym_SOURCES = ../automake-force-linker.cxx

# dve2lts-sym
dve2lts_sym_SOURCES   = pins2lts-sym.c
dve2lts_sym_SOURCES  += ../dve-greybox.c ../dve-greybox.h
dve2lts_sym_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
dve2lts_sym_LDADD    += $(VSET_LIBS) $(DVEC_LIBS)
dve2lts_sym_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_sym_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-sym
prom2lts_sym_SOURCES  = pins2lts-sym.c
prom2lts_sym_SOURCES += ../prom-greybox.c ../prom-greybox.h
prom2lts_sym_LDADD    = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
prom2lts_sym_LDADD   += $(VSET_LIBS) $(SPINJA_LIBS)
prom2lts_sym_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_sym_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
