if HAVE_MPI
HRE_LIB = ../hre-mpi/libhre.la
DIST_CPPFLAGS = -DUSEMPI -I ${top_srcdir}/src $(AM_CPPFLAGS)
else
HRE_LIB = ../hre/libhre.la
DIST_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
endif

bin_PROGRAMS = etf2lts-dist

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-dist
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-dist
endif

if HAVE_LIBPBES
bin_PROGRAMS += pbes2lts-dist
endif

if HAVE_DVE
bin_PROGRAMS += dve2lts-dist
endif

if HAVE_SPINJA
bin_PROGRAMS += prom2lts-dist
endif

# etf2lts-dist
etf2lts_dist_SOURCES   = pins2lts-dist.c
etf2lts_dist_SOURCES  += ../etf-greybox.c ../etf-greybox.h
etf2lts_dist_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
etf2lts_dist_CPPFLAGS  = -DETF $(DIST_CPPFLAGS)

# lpo2lts-dist
lpo2lts_dist_SOURCES   = pins2lts-dist.c
lpo2lts_dist_SOURCES  += ../mcrl-greybox.c ../mcrl-greybox.h
lpo2lts_dist_SOURCES  += ../at-map.c ../at-map.h
lpo2lts_dist_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lpo2lts_dist_LDADD    += $(MCRL_LIBS)
lpo2lts_dist_CPPFLAGS  = -DMCRL $(MCRL_CPPFLAGS) $(DIST_CPPFLAGS)
lpo2lts_dist_LDFLAGS   = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-dist
lps2lts_dist_SOURCES   = pins2lts-dist.c
lps2lts_dist_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
lps2lts_dist_LDADD    +=  ../libmcrl2-pins.la
lps2lts_dist_CPPFLAGS  = -DMCRL2 $(DIST_CPPFLAGS)
lps2lts_dist_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_dist_SOURCES = ../automake-force-linker.cxx

# pbes2lts-dist
pbes2lts_dist_SOURCES   = pins2lts-dist.c
pbes2lts_dist_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
pbes2lts_dist_LDADD    +=  ../libpbes-pins.la
pbes2lts_dist_CPPFLAGS  = -DPBES $(DIST_CPPFLAGS)
pbes2lts_dist_LDFLAGS   = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_pbes2lts_dist_SOURCES = ../automake-force-linker.cxx

# dve2lts-dist
dve2lts_dist_SOURCES   = pins2lts-dist.c
dve2lts_dist_SOURCES  += ../dve-greybox.c ../dve-greybox.h
dve2lts_dist_LDADD     = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
dve2lts_dist_LDADD    += $(DVEC_LIBS)
dve2lts_dist_CPPFLAGS  = -DDIVINE $(DIVINE_CPPFLAGS) $(DIST_CPPFLAGS)
dve2lts_dist_LDFLAGS   = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# prom2lts-dist
prom2lts_dist_SOURCES  = pins2lts-dist.c
prom2lts_dist_SOURCES += ../prom-greybox.c ../prom-greybox.h
prom2lts_dist_LDADD    = ../lts-io/liblts-io.la $(HRE_LIB) ../libltsmin.la
prom2lts_dist_LDADD   += $(SPINJA_LIBS)
prom2lts_dist_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(DIST_CPPFLAGS)
prom2lts_dist_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)
