if HAVE_MPI
HRE_LIB = ../hre-mpi/libhre.la
DIST_CPPFLAGS = -DUSEMPI -I ${top_srcdir}/src $(AM_CPPFLAGS)
else
HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
endif

bin_PROGRAMS = etf2lts-hre

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-hre
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-hre
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += pbes2lts-hre
endif

bin_PROGRAMS += nips2lts-hre

if HAVE_DVEC
bin_PROGRAMS += dve2lts-hre
endif

if HAVE_DVEC2
bin_PROGRAMS += dve22lts-hre
endif

bin_PROGRAMS += spinja2lts-hre

# etf2lts-hre
etf2lts_hre_SOURCES   = spec2lts-hre.c
etf2lts_hre_SOURCES  += ../etf-greybox.c ../etf-greybox.h
etf2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
etf2lts_hre_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# lpo2lts-hre
lpo2lts_hre_SOURCES   = spec2lts-hre.c
lpo2lts_hre_SOURCES  += ../mcrl-greybox.c ../mcrl-greybox.h
lpo2lts_hre_SOURCES  += ../at-map.c ../at-map.h
lpo2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
lpo2lts_hre_LDADD    += $(MCRL_LIBS)
lpo2lts_hre_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_hre_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-hre
lps2lts_hre_SOURCES   = spec2lts-hre.c
lps2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
lps2lts_hre_LDADD    +=  ../libmcrl2-pins.la
lps2lts_hre_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_hre_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_hre_SOURCES = ../automake-force-linker.cxx

# pbes2lts-hre
pbes2lts_hre_SOURCES   = spec2lts-hre.c
pbes2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
pbes2lts_hre_LDADD    +=  ../libpbes-pins.la
pbes2lts_hre_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_hre_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_hre_SOURCES = ../automake-force-linker.cxx

# nips2lts-hre
nips2lts_hre_SOURCES   = spec2lts-hre.c
nips2lts_hre_SOURCES  += ../nips-greybox.c ../nips-greybox.h
nips2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
nips2lts_hre_LDADD    += $(NIPS_LIBS)
nips2lts_hre_CPPFLAGS  = -DNIPS $(NIPS_CPPFLAGS) $(DIST_CPPFLAGS)
nips2lts_hre_LDFLAGS   = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-hre
dve2lts_hre_SOURCES   = spec2lts-hre.c
dve2lts_hre_SOURCES  += ../dve-greybox.cpp ../dve-greybox.h
dve2lts_hre_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
dve2lts_hre_LDADD    += $(DVEC_LIBS)
dve2lts_hre_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_hre_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-hre
dve22lts_hre_SOURCES  = spec2lts-hre.c
dve22lts_hre_SOURCES += ../dve2-greybox.c ../dve2-greybox.h
dve22lts_hre_LDADD    = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
dve22lts_hre_LDADD   += $(DVEC2_LIBS)
dve22lts_hre_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(DIST_CPPFLAGS)
dve22lts_hre_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-hre
spinja2lts_hre_SOURCES  = spec2lts-hre.c
spinja2lts_hre_SOURCES += ../spinja-greybox.c ../spinja-greybox.h
spinja2lts_hre_LDADD    = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin-hre.la
spinja2lts_hre_LDADD   += $(SPINJA_LIBS)
spinja2lts_hre_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
spinja2lts_hre_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
