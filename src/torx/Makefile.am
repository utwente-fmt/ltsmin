AM_CPPFLAGS = -I${top_srcdir}/src

bin_PROGRAMS =

# etf2torx
bin_PROGRAMS += etf2torx
etf2torx_SOURCES  = spec2torx.c
etf2torx_SOURCES += ../etf-greybox.c
etf2torx_SOURCES += ../etf-greybox.h

etf2torx_LDADD    = ../libltsmin.la
etf2torx_CPPFLAGS = -DETF $(TORX_CPPFLAGS) $(AM_CPPFLAGS)


# dve2torx
if HAVE_DVE
bin_PROGRAMS += dve2torx
endif
dve2torx_SOURCES  = spec2torx.c
dve2torx_SOURCES += ../dve-greybox.c
dve2torx_SOURCES += ../dve-greybox.h

dve2torx_LDADD    = ../libltsmin.la $(DVEC_LIBS)
dve2torx_CPPFLAGS = -DDIVINE $(TORX_CPPFLAGS) $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2torx_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)


# lpo2torx
if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2torx
endif
lpo2torx_SOURCES  = spec2torx.c
lpo2torx_SOURCES += ../mcrl-greybox.c
lpo2torx_SOURCES += ../mcrl-greybox.h
lpo2torx_SOURCES += ../at-map.c
lpo2torx_SOURCES += ../at-map.h

lpo2torx_LDADD    = ../libltsmin.la $(MCRL_LIBS)
lpo2torx_CPPFLAGS = -DMCRL $(TORX_CPPFLAGS) $(AM_CPPFLAGS) $(MCRL_CPPFLAGS)
lpo2torx_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)


# lps2torx
if HAVE_LIBMCRL2
bin_PROGRAMS += lps2torx
endif
lps2torx_SOURCES  = spec2torx.c
lps2torx_LDADD    = ../libmcrl2-pins.la ../libltsmin.la
lps2torx_CPPFLAGS = -DMCRL2 $(TORX_CPPFLAGS) $(AM_CPPFLAGS)
nodist_EXTRA_lps2torx_SOURCES = ../automake-force-linker.cxx


# prom2torx
if HAVE_SPINJA
bin_PROGRAMS += prom2torx
endif
prom2torx_SOURCES  = spec2torx.c
prom2torx_SOURCES += ../prom-greybox.c
prom2torx_SOURCES += ../prom-greybox.h

prom2torx_LDADD    = ../libltsmin.la $(SPINJA_LIBS)
prom2torx_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(TORX_CPPFLAGS) $(AM_CPPFLAGS)
prom2torx_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# make
.DELETE_ON_ERROR:
