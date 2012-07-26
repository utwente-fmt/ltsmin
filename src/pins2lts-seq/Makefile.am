HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
MC_LIBS = ../libmc.la
VSET_LIBS = ../libvset.la

bin_PROGRAMS = etf2lts-seq

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

if HAVE_SPINJA
bin_PROGRAMS += prom2lts-seq
endif

# etf2lts-seq
etf2lts_seq_SOURCES   = pins2lts-seq.c
etf2lts_seq_SOURCES  += ../etf-greybox.c ../etf-greybox.h
etf2lts_seq_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
etf2lts_seq_LDADD    += $(VSET_LIBS) $(MC_LIBS)
etf2lts_seq_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# lpo2lts-seq
lpo2lts_seq_SOURCES   = pins2lts-seq.c
lpo2lts_seq_SOURCES  += ../mcrl-greybox.c ../mcrl-greybox.h
lpo2lts_seq_SOURCES  += ../at-map.c ../at-map.h
lpo2lts_seq_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lpo2lts_seq_LDADD    += $(VSET_LIBS) $(MC_LIBS) $(MCRL_LIBS)
lpo2lts_seq_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_seq_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-seq
lps2lts_seq_SOURCES   = pins2lts-seq.c
lps2lts_seq_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lps2lts_seq_LDADD    += $(VSET_LIBS) $(MC_LIBS)  ../libmcrl2-pins.la
lps2lts_seq_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_seq_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_seq_SOURCES = ../automake-force-linker.cxx

# pbes2lts-seq
pbes2lts_seq_SOURCES   = pins2lts-seq.c
pbes2lts_seq_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
pbes2lts_seq_LDADD    += $(VSET_LIBS) $(MC_LIBS)  ../libpbes-pins.la
pbes2lts_seq_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_seq_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_seq_SOURCES = ../automake-force-linker.cxx

# dve2lts-seq
dve2lts_seq_SOURCES   = pins2lts-seq.c
dve2lts_seq_SOURCES  += ../dve-greybox.c ../dve-greybox.h
dve2lts_seq_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
dve2lts_seq_LDADD    += $(VSET_LIBS) $(MC_LIBS) $(DVEC_LIBS)
dve2lts_seq_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_seq_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-seq
prom2lts_seq_SOURCES  = pins2lts-seq.c
prom2lts_seq_SOURCES += ../prom-greybox.c ../prom-greybox.h
prom2lts_seq_LDADD    = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
prom2lts_seq_LDADD   += $(VSET_LIBS) $(MC_LIBS) $(SPINJA_LIBS)
prom2lts_seq_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_seq_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
