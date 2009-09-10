TORX_CPPFLAGS = -I ${top_srcdir}/src

bin_PROGRAMS = 

if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2torx
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2torx
endif

if HAVE_LIBMCRL
# if HAVE_LIBNIPS
bin_PROGRAMS += nips2torx
# endif
bin_PROGRAMS += etf2torx
endif

# etf2torx
etf2torx_SOURCES  = \
	../etf-greybox.c ../etf-greybox.h \
	../lts.c ../lts.h \
	spec2torx.c
etf2torx_LDADD    = ../libltsmin.la -lATerm
etf2torx_CPPFLAGS = -DETF $(TORX_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2torx_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lpo2torx
lpo2torx_SOURCES  = \
	../mcrl-greybox.c ../mcrl-greybox.h \
	../at-map.c ../at-map.h \
	spec2torx.c
lpo2torx_LDADD    = ../libltsmin.la $(MCRL_LIBS)
lpo2torx_CPPFLAGS = -DMCRL $(TORX_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
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
nips2torx_LDADD    = ../libltsmin.la -lATerm $(NIPS_LIBS)
nips2torx_CPPFLAGS = -DNIPS $(TORX_CPPFLAGS) $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2torx_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# make
.DELETE_ON_ERROR:
