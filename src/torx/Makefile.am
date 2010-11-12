AM_CPPFLAGS = -I${top_srcdir}/src

bin_PROGRAMS = etf2torx

# if HAVE_LIBNIPS
bin_PROGRAMS += nips2torx
# endif

if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2torx
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2torx
endif

if HAVE_DVEC
bin_PROGRAMS += dve2torx
endif

if HAVE_DVEC2
bin_PROGRAMS += dve22torx
endif


# etf2torx
etf2torx_SOURCES  = \
	../etf-greybox.c ../etf-greybox.h \
	spec2torx.c
etf2torx_LDADD    = ../libltsmin.la
etf2torx_CPPFLAGS = -DETF $(TORX_CPPFLAGS) $(AM_CPPFLAGS)

# dve2torx
dve2torx_SOURCES  = \
	../dve-greybox.cpp ../dve-greybox.h \
	spec2torx.c
dve2torx_LDADD    = ../libltsmin.la $(DVEC_LIBS)
dve2torx_CPPFLAGS = -DDIVINE $(TORX_CPPFLAGS) $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2torx_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22torx
dve22torx_SOURCES  = \
	../dve2-greybox.c ../dve2-greybox.h \
	../lts.c ../lts.h \
	spec2torx.c
dve22torx_LDADD    = ../libltsmin.la $(DVEC2_LIBS)
dve22torx_CPPFLAGS = -DDIVINE2 $(TORX_CPPFLAGS) $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22torx_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# lpo2torx
lpo2torx_SOURCES  = \
	../mcrl-greybox.c ../mcrl-greybox.h \
	../at-map.c ../at-map.h \
	spec2torx.c
lpo2torx_LDADD    = ../libltsmin.la $(MCRL_LIBS)
lpo2torx_CPPFLAGS = -DMCRL $(TORX_CPPFLAGS) $(AM_CPPFLAGS) $(MCRL_CPPFLAGS)
lpo2torx_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2torx
lps2torx_SOURCES  = \
	../mcrl2-greybox.cpp ../mcrl2-greybox.h \
	../at-map.c ../at-map.h \
	spec2torx.c
lps2torx_LDADD    = ../libltsmin.la $(MCRL2_LIBS)
lps2torx_CPPFLAGS = -DMCRL2 $(TORX_CPPFLAGS) $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2torx_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)

# nips2torx
nips2torx_SOURCES  = \
	../nips-greybox.c ../nips-greybox.h \
	spec2torx.c
nips2torx_LDADD    = ../libltsmin.la $(NIPS_LIBS)
nips2torx_CPPFLAGS = -DNIPS $(TORX_CPPFLAGS) $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2torx_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# make
.DELETE_ON_ERROR:
