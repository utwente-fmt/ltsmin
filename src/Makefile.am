bin_PROGRAMS  = ltsmin-convert
bin_PROGRAMS += etf-convert
bin_PROGRAMS += ltsmin-tracepp
bin_PROGRAMS += gcf

check_PROGRAMS  = test-bitset
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-dfs-stack
check_PROGRAMS += test-hre
check_PROGRAMS += ltsmin-parse
check_PROGRAMS += test-dbs

noinst_LTLIBRARIES  = libutil.la
noinst_LTLIBRARIES += libvset.la
noinst_LTLIBRARIES += libltsmin.la

pkglib_LTLIBRARIES =

SUBDIRS  = dm
SUBDIRS += bignum
SUBDIRS += .
SUBDIRS += ce
SUBDIRS += torx

EXTRA_DIST  = ltsmin-lexer.c ltsmin-lexer.h

BUILT_SOURCES  = ltsmin-grammar.c ltsmin-grammar.h
BUILT_SOURCES += ltsmin-lexer.c ltsmin-lexer.h

bin_PROGRAMS += etf2lts-grey
bin_PROGRAMS += etf2lts-gsea
bin_PROGRAMS += etf-reach

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-grey
bin_PROGRAMS += lpo2lts-gsea
bin_PROGRAMS += lpo-reach
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-grey
bin_PROGRAMS += lps2lts-gsea
bin_PROGRAMS += lps-reach
noinst_LTLIBRARIES += libmcrl2-pins.la
endif

# if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-grey
bin_PROGRAMS += nips2lts-gsea
bin_PROGRAMS += nips-reach
#endif

if HAVE_DVEC
bin_PROGRAMS += dve2lts-grey
bin_PROGRAMS += dve2lts-gsea
bin_PROGRAMS += dve-reach
endif

if HAVE_DVEC2
bin_PROGRAMS += dve22lts-grey
bin_PROGRAMS += dve22lts-gsea
bin_PROGRAMS += dve2-reach
endif

#if HAVE_SPINJA
bin_PROGRAMS += spinja2lts-grey
bin_PROGRAMS += spinja2lts-gsea
bin_PROGRAMS += spinja-reach
#endif


if HAVE_ATOMICS
bin_PROGRAMS += etf2lts-mc
bin_PROGRAMS += nips2lts-mc
bin_PROGRAMS += spinja2lts-mc
if HAVE_DVEC
bin_PROGRAMS += dve2lts-mc
endif
if HAVE_DVEC2
bin_PROGRAMS += dve22lts-mc
endif
endif

if HAVE_MPI
bin_PROGRAMS += etf2lts-mpi
bin_PROGRAMS += nips2lts-mpi
bin_PROGRAMS += spinja2lts-mpi
if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-mpi
endif
if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-mpi
endif
if HAVE_DVEC
bin_PROGRAMS += dve2lts-mpi
endif
if HAVE_DVEC2
bin_PROGRAMS += dve22lts-mpi
endif
bin_PROGRAMS += ltsmin-mpi
check_PROGRAMS += mpi-event-test
check_PROGRAMS += test-hre-mpi
noinst_LTLIBRARIES += libmpi.la
noinst_LTLIBRARIES += libltsmin-mpi.la
endif

if HAVE_CADP
pkglib_LTLIBRARIES += libetf-open.la
pkglib_LTLIBRARIES += libnips-open.la
pkglib_LTLIBRARIES += libspinja-open.la
if HAVE_LIBSTEP_INFO
pkglib_LTLIBRARIES += liblpo-open.la
endif
if HAVE_LIBMCRL2
pkglib_LTLIBRARIES += liblps-open.la
endif
if HAVE_DVEC
pkglib_LTLIBRARIES += libdve-open.la
endif
if HAVE_DVEC2
pkglib_LTLIBRARIES += libdve2-open.la
endif
# pins_open
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS = pins_open
endif


libmcrl2_pins_la_SOURCES  = mcrl2-greybox.cpp mcrl2-greybox.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 -DNDEBUG $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_LIBS)

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libltsmin.la

# ltsmin-tracepp
ltsmin_tracepp_SOURCES = ltsmin-tracepp.c
ltsmin_tracepp_LDADD = libltsmin.la

# gcf
gcf_SOURCES = gcf-rw.c
gcf_LDADD = libltsmin.la

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD = libltsmin.la

# test-hre
test_hre_SOURCES = test-hre.c
test_hre_LDADD = libltsmin.la

if HAVE_MPI
test_hre_mpi_SOURCES = test-hre.c
test_hre_mpi_LDADD = libltsmin-mpi.la
endif

# ltsmin-parse
ltsmin_parse_SOURCES = ltsmin-parse.c
ltsmin_parse_CPPFLAGS = -DNXT -I${top_srcdir}/ltl2ba
ltsmin_parse_LDADD = libltsmin.la


# etf2lts-grey
etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h
etf2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la
etf2lts_grey_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_grey_SOURCES += at-map.c at-map.h
lpo2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(MCRL_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c
lps2lts_grey_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
lps2lts_grey_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps2lts_grey_SOURCES = automake-force-linker.cxx

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h
nips2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(NIPS_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-grey
dve2lts_grey_SOURCES  = spec2lts-grey.c dve-greybox.cpp dve-greybox.h
dve2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve2lts_grey_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_grey_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-grey
dve22lts_grey_SOURCES  = spec2lts-grey.c dve2-greybox.c dve2-greybox.h
dve22lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve22lts_grey_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_grey_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-grey
spinja2lts_grey_SOURCES  = spec2lts-grey.c spinja-greybox.c spinja-greybox.h
spinja2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja2lts_grey_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_grey_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf2lts-gsea
etf2lts_gsea_SOURCES  = spec2lts-gsea.c etf-greybox.c etf-greybox.h
etf2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la
etf2lts_gsea_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# lpo2lts-gsea
lpo2lts_gsea_SOURCES  = spec2lts-gsea.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_gsea_SOURCES += at-map.c at-map.h
lpo2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(MCRL_LIBS)
lpo2lts_gsea_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_gsea_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-gsea
lps2lts_gsea_SOURCES  = spec2lts-gsea.c
lps2lts_gsea_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
lps2lts_gsea_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps2lts_gsea_SOURCES = automake-force-linker.cxx

# nips2lts-gsea
nips2lts_gsea_SOURCES  = spec2lts-gsea.c nips-greybox.c nips-greybox.h
nips2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(NIPS_LIBS)
nips2lts_gsea_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_gsea_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-gsea
dve2lts_gsea_SOURCES  = spec2lts-gsea.c dve-greybox.cpp dve-greybox.h
dve2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve2lts_gsea_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_gsea_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-gsea
dve22lts_gsea_SOURCES  = spec2lts-gsea.c dve2-greybox.c dve2-greybox.h
dve22lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve22lts_gsea_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_gsea_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-gsea
spinja2lts_gsea_SOURCES  = spec2lts-gsea.c spinja-greybox.c spinja-greybox.h
spinja2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja2lts_gsea_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_gsea_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf-reach
etf_reach_SOURCES  = spec-reach.c etf-greybox.c etf-greybox.h
etf_reach_LDADD    = $(VSET_LIBS) libltsmin.la
etf_reach_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h
lpo_reach_SOURCES += at-map.c at-map.h
lpo_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(MCRL_LIBS)
lpo_reach_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps-reach
lps_reach_SOURCES  = spec-reach.c
lps_reach_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
lps_reach_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps_reach_SOURCES = automake-force-linker.cxx

# nips-reach
nips_reach_SOURCES  = spec-reach.c nips-greybox.c nips-greybox.h
nips_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(NIPS_LIBS)
nips_reach_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips_reach_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve-reach
dve_reach_SOURCES  = spec-reach.c dve-greybox.cpp dve-greybox.h
dve_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve_reach_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve_reach_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve2-reach
dve2_reach_SOURCES  = spec-reach.c dve2-greybox.c dve2-greybox.h
dve2_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve2_reach_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve2_reach_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja-reach
spinja_reach_SOURCES  = spec-reach.c spinja-greybox.c spinja-greybox.h
spinja_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja_reach_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja_reach_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf2lts-mpi
etf2lts_mpi_SOURCES  = spec2lts-mpi.c etf-greybox.c etf-greybox.h
etf2lts_mpi_LDADD    = libltsmin-mpi.la
etf2lts_mpi_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_mpi_SOURCES += at-map.c at-map.h
lpo2lts_mpi_LDADD    = libltsmin-mpi.la $(MCRL_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-mpi
lps2lts_mpi_SOURCES  = spec2lts-mpi.c
lps2lts_mpi_LDADD    = libmcrl2-pins.la libltsmin-mpi.la
lps2lts_mpi_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps2lts_mpi_SOURCES = automake-force-linker.cxx

# nips2lts-mpi
nips2lts_mpi_SOURCES  = spec2lts-mpi.c nips-greybox.c nips-greybox.h
nips2lts_mpi_LDADD    = libltsmin-mpi.la $(NIPS_LIBS)
nips2lts_mpi_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mpi_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-mpi
dve2lts_mpi_SOURCES  = spec2lts-mpi.c dve-greybox.cpp dve-greybox.h
dve2lts_mpi_LDADD    = libltsmin-mpi.la $(DVEC_LIBS)
dve2lts_mpi_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_mpi_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-mpi
dve22lts_mpi_SOURCES  = spec2lts-mpi.c dve2-greybox.c dve2-greybox.h
dve22lts_mpi_LDADD    = libltsmin-mpi.la $(DVEC2_LIBS)
dve22lts_mpi_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_mpi_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-mpi
spinja2lts_mpi_SOURCES  = spec2lts-mpi.c spinja-greybox.c spinja-greybox.h
spinja2lts_mpi_LDADD    = libltsmin-mpi.la $(SPINJA_LIBS)
spinja2lts_mpi_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_mpi_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf2lts-mc
etf2lts_mc_SOURCES  = spec2lts-mc.c etf-greybox.c etf-greybox.h
etf2lts_mc_LDADD    = libltsmin.la
etf2lts_mc_CPPFLAGS = -DETF $(AM_CPPFLAGS)

## mCRL and mCRL2 (ATerms) are not thread-safe
## lpo2lts-mc
#lpo2lts_mc_SOURCES  = spec2lts-mc.c mcrl-greybox.c mcrl-greybox.h
#lpo2lts_mc_SOURCES += at-map.c at-map.h
#lpo2lts_mc_LDADD    = libltsmin.la $(MCRL_LIBS)
#lpo2lts_mc_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
#lpo2lts_mc_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

## lps2lts-mc
#lps2lts_mc_SOURCES  = spec2lts-mc.c
#lps2lts_mc_LDADD    = libmcrl2-pins.la libltsmin.la
#lps2lts_mc_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
#nodist_EXTRA_lps2lts_mc_SOURCES = automake-force-linker.cxx

# nips2lts-mc
nips2lts_mc_SOURCES  = spec2lts-mc.c nips-greybox.c nips-greybox.h
nips2lts_mc_LDADD    = libltsmin.la $(NIPS_LIBS)
nips2lts_mc_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mc_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-mc
dve2lts_mc_SOURCES  = spec2lts-mc.c dve-greybox.cpp dve-greybox.h
dve2lts_mc_LDADD    = libltsmin.la $(DVEC_LIBS)
dve2lts_mc_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_mc_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-mc
dve22lts_mc_SOURCES  = spec2lts-mc.c dve2-greybox.c dve2-greybox.h
dve22lts_mc_LDADD    = libltsmin.la $(DVEC2_LIBS)
dve22lts_mc_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_mc_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-mc
spinja2lts_mc_SOURCES  = spec2lts-mc.c spinja-greybox.c spinja-greybox.h
spinja2lts_mc_LDADD    = libltsmin.la $(SPINJA_LIBS)
spinja2lts_mc_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_mc_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# libetf-open.la
libetf_open_la_SOURCES = spec_open.c etf-greybox.c etf-greybox.h
libetf_open_la_CPPFLAGS = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD = libltsmin.la

# liblpo-open.la
liblpo_open_la_SOURCES  = spec_open.c mcrl-greybox.c mcrl-greybox.h
liblpo_open_la_SOURCES += at-map.c at-map.h
liblpo_open_la_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblpo_open_la_LIBADD = libltsmin.la

# liblps-open.la
liblps_open_la_SOURCES = spec_open.c
liblps_open_la_CPPFLAGS = -DMCRL2 $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblps_open_la_LIBADD = libmcrl2-pins.la libltsmin.la
nodist_EXTRA_liblps_open_la_SOURCES = automake-force-linker.cxx

# libnips-open.la
libnips_open_la_SOURCES = spec_open.c nips-greybox.c nips-greybox.h
libnips_open_la_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libnips_open_la_LIBADD = ${top_builddir}/nips-vm/libnips_vm.la libltsmin.la

# libdve-open.la
libdve_open_la_SOURCES = spec_open.c dve-greybox.cpp dve-greybox.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve_open_la_LIBADD = libltsmin.la

# libdve2-open.la
libdve2_open_la_SOURCES = spec_open.c dve2-greybox.c dve2-greybox.h
libdve2_open_la_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve2_open_la_LIBADD = libltsmin.la

# libspinja-open.la
libspinja_open_la_SOURCES = spec_open.c spinja-greybox.c spinja-greybox.h
libspinja_open_la_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libspinja_open_la_LIBADD = libltsmin.la


# mpi-event-test
mpi_event_test_SOURCES = mpi-event-test.c
mpi_event_test_LDADD   = libltsmin-mpi.la

# test-is-balloc
test_is_balloc_SOURCES = test-is-balloc.c
test_is_balloc_LDADD   = libltsmin.la

# test-dfs-stack
test_dfs_stack_SOURCES = test-dfs-stack.c
test_dfs_stack_LDADD   = libltsmin.la

# test-dbs
test_dbs_SOURCES = test-dbs.c
test_dbs_LDADD   = libltsmin.la

# ltsmin-mpi
ltsmin_mpi_SOURCES  = sigmin-mpi.c sigmin-sig.h
ltsmin_mpi_SOURCES += seg-lts.c seg-lts.h
ltsmin_mpi_SOURCES += sig-array.c sig-array.h
ltsmin_mpi_SOURCES += sigmin-array.c sigmin-array.h
ltsmin_mpi_SOURCES += sigmin-set.c sigmin-set.h
ltsmin_mpi_SOURCES += set.c set.h
ltsmin_mpi_SOURCES += sigmin-types.h
ltsmin_mpi_LDADD = libltsmin-mpi.la


# ltsmin-convert
ltsmin_convert_SOURCES = ltsmin-convert.c
ltsmin_convert_LDADD = libltsmin.la


# libraries
VSET_LIBS = libvset.la

libvset_la_SOURCES   = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES  += vset_listdd.c
libvset_la_SOURCES  += vset_buddy.c
libvset_la_CPPFLAGS  = $(BUDDY_CPPFLAGS)
libvset_la_LDFLAGS   = $(BUDDY_LDFLAGS)
libvset_la_LIBADD    = $(BUDDY_LIBS)

if HAVE_LIBMCRL
libvset_la_SOURCES  += vset_atermdd.c
libvset_la_CPPFLAGS += $(MCRL_CPPFLAGS)
libvset_la_LDFLAGS  += $(MCRL_LDFLAGS)
libvset_la_LIBADD   += -lATerm
endif

if HAVE_LIBDDD
CCLD = $(CXXLD)
libvset_la_SOURCES  += vset_ddd.cpp
libvset_la_CPPFLAGS += $(DDD_CPPFLAGS)
libvset_la_LDFLAGS  += $(DDD_LDFLAGS)
libvset_la_LIBADD   += $(DDD_LIBS)
endif

libvset_la_CPPFLAGS += $(AM_CPPFLAGS)
libvset_la_LDFLAGS  += $(AM_LDFLAGS)


libmpi_la_SOURCES  = mpi_io_stream.c
libmpi_la_SOURCES += mpi_raf.c
libmpi_la_SOURCES += mpi_io_stream.h
libmpi_la_SOURCES += mpi-event-loop.c mpi-event-loop.h
libmpi_la_SOURCES += mpi-runtime.c mpi-runtime.h
libmpi_la_SOURCES += task-queue.c task-queue.h


libutil_la_SOURCES  = unix.c unix.h
libutil_la_SOURCES += arch_object.h
libutil_la_SOURCES += archive.c archive.h
libutil_la_SOURCES += archive_dir.c
libutil_la_SOURCES += archive_format.c
libutil_la_SOURCES += archive_gcf.c
libutil_la_SOURCES += archive_gsf.c
libutil_la_SOURCES += archive_io_ops.c
libutil_la_SOURCES += balloc.c balloc.h
libutil_la_SOURCES += bitset.c bitset.h
libutil_la_SOURCES += cctables.c cctables.h
libutil_la_SOURCES += chunk_support.c chunk_support.h
libutil_la_SOURCES += chunk_table_factory.c
libutil_la_SOURCES += config.h
libutil_la_SOURCES += dbs-ll.c dbs-ll.h
libutil_la_SOURCES += dbs.c dbs.h
libutil_la_SOURCES += dfs-stack.c dfs-stack.h
libutil_la_SOURCES += dir_ops.c dir_ops.h
libutil_la_SOURCES += dynamic-array.c dynamic-array.h
libutil_la_SOURCES += etf-internal.h
libutil_la_SOURCES += etf-objects.c etf-objects.h
libutil_la_SOURCES += etf-parser.c
libutil_la_SOURCES += etf-util.c etf-util.h
libutil_la_SOURCES += fast_hash.c
libutil_la_SOURCES += fast_hash.h
libutil_la_SOURCES += fifo.c fifo.h
libutil_la_SOURCES += generichash4.c generichash8.c generichash.h
libutil_la_SOURCES += ghf.c ghf.h
libutil_la_SOURCES += greybox.c greybox.h
libutil_la_SOURCES += group.c
libutil_la_SOURCES += gzstream.c
libutil_la_SOURCES += is-balloc.c is-balloc.h
libutil_la_SOURCES += lb.c lb.h
libutil_la_SOURCES += lts-type.c lts-type.h
libutil_la_SOURCES += lts_count.c lts_count.h
libutil_la_SOURCES += lts_enum.c lts_enum.h
libutil_la_SOURCES += lts_io.c lts_io.h lts_io_internal.h
libutil_la_SOURCES += ltsmin-grammar.lemon
libutil_la_SOURCES += ltsmin-lexer.l
libutil_la_SOURCES += ltsmin-parse-env.h
libutil_la_SOURCES += ltsmin-syntax.c ltsmin-syntax.h
libutil_la_SOURCES += ltsmin-tl.c ltsmin-tl.h
libutil_la_SOURCES += packet_stream.c packet_stream.h
libutil_la_SOURCES += pins-cache.c
libutil_la_SOURCES += qsortex.c
libutil_la_SOURCES += raf.c raf.h
libutil_la_SOURCES += raf_object.h
libutil_la_SOURCES += runtime.c runtime.h
libutil_la_SOURCES += scctimer.c scctimer.h
libutil_la_SOURCES += spec-greybox.h
libutil_la_SOURCES += stream.c stream.h
libutil_la_SOURCES += stream_buffer.c
libutil_la_SOURCES += stream_diff32.c
libutil_la_SOURCES += stream_fd.c
libutil_la_SOURCES += stream_mem.c
libutil_la_SOURCES += stream_object.h
libutil_la_SOURCES += string-map.c string-map.h
libutil_la_SOURCES += stringindex.c stringindex.h
libutil_la_SOURCES += struct_io.c struct_io.h
libutil_la_SOURCES += tables.c tables.h
libutil_la_SOURCES += tls.c tls.h
libutil_la_SOURCES += trace.c trace.h
libutil_la_SOURCES += treedbs-ll.c treedbs-ll.h
libutil_la_SOURCES += treedbs.c treedbs.h
libutil_la_SOURCES += zobrist.c zobrist.h


libutil_la_CPPFLAGS =
libutil_la_LIBADD   = ltsmin-grammar.lo dm/libdm.la
libutil_la_LDFLAGS  =

if HAVE_LIBBCG
libutil_la_SOURCES  += bcg_io_ops.c
libutil_la_CPPFLAGS += $(CADP_CPPFLAGS)
libutil_la_LDFLAGS  += $(CADP_LDFLAGS)
libutil_la_LIBADD   += $(CADP_LIBS)
endif

libutil_la_SOURCES += ltl2ba-lex.c
libutil_la_SOURCES += ltsmin-buchi.h
libutil_la_CPPFLAGS += -DNXT -I${top_srcdir}/ltl2ba
libutil_la_LIBADD += $(top_builddir)/ltl2ba/libltl2ba.la

libutil_la_SOURCES += pins-ltl.c

libutil_la_SOURCES += pins-por.c

libutil_la_SOURCES += hre-context.c hre-context.h
libutil_la_SOURCES += hre-feedback.c hre-feedback.h hre-internal.h
libutil_la_SOURCES += hre-main.c hre-main.h
libutil_la_SOURCES += hre-malloc.c hre-malloc.h
libutil_la_SOURCES += hre-popt.c hre-popt.h
libutil_la_SOURCES += hre-pthread.c

# must be added last to CPPFLAGS
libutil_la_CPPFLAGS += $(AM_CPPFLAGS)



libltsmin_la_SOURCES = hre-nompi.c
libltsmin_la_LIBADD = libutil.la bignum/libbignum.la

libltsmin_mpi_la_SOURCES = hre-mpi.c
libltsmin_mpi_la_LIBADD = libutil.la libmpi.la



DISTCLEANFILES = git_version.h git_version.h.tmp

CLEANFILES  = ltsmin-grammar.c ltsmin-grammar.h
CLEANFILES += ltsmin-grammar.out
CLEANFILES += pins_open

MAINTAINERCLEANFILES = ltsmin-tokens.c ltsmin-tokens.h
MAINTAINERCLEANFILES += subst_pins_open.mk


EXTRA_DIST += config.munge.pl subst_pins_open.mk

-include subst_pins_open.mk

pins_open: pins_open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

subst_pins_open.mk: pins_open.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@


LEMON_FLAGS = -s

%.c %.h: %.lemon lempar.c
	cd $(top_builddir)/lemon && $(MAKE) $(AM_MAKEFLAGS) lemon$(EXEEXT)
	$(top_builddir)/lemon/lemon$(EXEEXT) $(LEMON_FLAGS) $<
	@if test -f $@; then touch $@; fi

%.c %.h: %.l
	$(am__skiplex) $(LEX) $<

runtime.c: git_version.h
hre-popt.c: git_version.h

GIT_DESCRIBE := $(shell cd $(top_srcdir) ; git describe --tags --abbrev=6 HEAD 2>/dev/null)

git_version.h: git_version.h.tmp
	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

git_version.h.tmp:
	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > git_version.h.tmp
.PHONY: git_version.h.tmp

# make
.DELETE_ON_ERROR:
