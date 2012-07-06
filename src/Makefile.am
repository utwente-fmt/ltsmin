bin_PROGRAMS = etf-convert

pkglib_LTLIBRARIES =

check_PROGRAMS  = test-bitset
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-dfs-stack
check_PROGRAMS += ltsmin-parse
check_PROGRAMS += test-dbs
check_PROGRAMS += test-vset

noinst_LTLIBRARIES  = libvset.la

if HAVE_LIBSPG
noinst_LTLIBRARIES += libspg.la
endif

noinst_LTLIBRARIES += libltsmin.la
noinst_LTLIBRARIES += libmc.la

SUBDIRS  = dm
SUBDIRS += bignum
SUBDIRS += hre-io
SUBDIRS += hre
if HAVE_MPI
SUBDIRS += hre-mpi
endif
SUBDIRS += hre-test
SUBDIRS += lts-io
SUBDIRS += lts-lib
SUBDIRS += .
SUBDIRS += gcf-tool
SUBDIRS += lts-tracepp
SUBDIRS += ltscmp-one
SUBDIRS += ltstrans
SUBDIRS += pins2lts-hre
SUBDIRS += sigmin-one
if HAVE_MPI
SUBDIRS += sigmin-mpi
endif
SUBDIRS += ce
SUBDIRS += torx
SUBDIRS += spg


EXTRA_DIST  = ltsmin-lexer.c ltsmin-lexer.h

BUILT_SOURCES  = ltsmin-grammar.c ltsmin-grammar.h
BUILT_SOURCES += ltsmin-lexer.c ltsmin-lexer.h

#bin_PROGRAMS += etf2lts-grey
bin_PROGRAMS += etf2lts-gsea
bin_PROGRAMS += etf-reach

if HAVE_LIBSTEP_INFO
#bin_PROGRAMS += lpo2lts-grey
bin_PROGRAMS += lpo2lts-gsea
bin_PROGRAMS += lpo-reach
endif

if HAVE_LIBMCRL2
#bin_PROGRAMS += lps2lts-grey
bin_PROGRAMS += lps2lts-gsea
bin_PROGRAMS += lps-reach
noinst_LTLIBRARIES += libmcrl2-pins.la
endif

if HAVE_LIBPBES
#bin_PROGRAMS += pbes2lts-grey 
bin_PROGRAMS += pbes2lts-gsea
if HAVE_LIBSPG
bin_PROGRAMS += pbes-reach
endif
noinst_LTLIBRARIES += libpbes-pins.la
endif

if HAVE_DVEC
#bin_PROGRAMS += dve2lts-grey
bin_PROGRAMS += dve2lts-gsea
bin_PROGRAMS += dve-reach
endif

if HAVE_DVEC2
#bin_PROGRAMS += dve22lts-grey
bin_PROGRAMS += dve22lts-gsea
bin_PROGRAMS += dve2-reach
endif

#if HAVE_OPAAL
#bin_PROGRAMS += opaal2lts-grey
#bin_PROGRAMS += opaal2lts-gsea
#bin_PROGRAMS += opaal-reach
#endif

#if HAVE_SPINJA
#bin_PROGRAMS += spinja2lts-grey
bin_PROGRAMS += spinja2lts-gsea
bin_PROGRAMS += spinja-reach
#endif


if HAVE_ATOMICS
bin_PROGRAMS += etf2lts-mc
bin_PROGRAMS += spinja2lts-mc
if HAVE_DVEC
bin_PROGRAMS += dve2lts-mc
endif
if HAVE_DVEC2
bin_PROGRAMS += dve22lts-mc
bin_PROGRAMS += dve22lts-hre
endif
if HAVE_OPAAL
bin_PROGRAMS += opaal2lts-mc
endif
endif

if HAVE_CADP
pkglib_LTLIBRARIES += libetf-open.la
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
if HAVE_OPAAL
pkglib_LTLIBRARIES += libopaal-open.la
endif
# pins_open
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS = pins_open
endif


libmcrl2_pins_la_SOURCES  = mcrl2-greybox.cpp mcrl2-greybox.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)

if HAVE_LIBPBES
libpbes_pins_la_SOURCES  = pbes-greybox.cpp pbes-greybox.h
libpbes_pins_la_CPPFLAGS = -DPBES -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libpbes_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)
endif

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libltsmin.la

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD = libltsmin.la

# ltsmin-parse
ltsmin_parse_SOURCES = ltsmin-parse.c
ltsmin_parse_CPPFLAGS = -DNXT -I${top_srcdir}/ltl2ba
ltsmin_parse_LDADD = libltsmin.la


### etf2lts-grey
##etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h
##etf2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la
##etf2lts_grey_CPPFLAGS = -DETF $(AM_CPPFLAGS)

### lpo2lts-grey
##lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h
##lpo2lts_grey_SOURCES += at-map.c at-map.h
##lpo2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(MCRL_LIBS)
##lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
##lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

### lps2lts-grey
##lps2lts_grey_SOURCES  = spec2lts-grey.c
##lps2lts_grey_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
##lps2lts_grey_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
##lps2lts_grey_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
##nodist_EXTRA_lps2lts_grey_SOURCES = automake-force-linker.cxx

# pbes2lts-grey
##pbes2lts_grey_SOURCES  = spec2lts-grey.c
##pbes2lts_grey_LDADD    = libvset.la libpbes-pins.la libltsmin.la
##pbes2lts_grey_CPPFLAGS = -DPBES $(AM_CPPFLAGS)
##nodist_EXTRA_pbes2lts_grey_SOURCES = automake-force-linker.cxx

### dve2lts-grey
##dve2lts_grey_SOURCES  = spec2lts-grey.c dve-greybox.cpp dve-greybox.h
##dve2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
##dve2lts_grey_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
##dve2lts_grey_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

### dve22lts-grey
##dve22lts_grey_SOURCES  = spec2lts-grey.c dve2-greybox.c dve2-greybox.h
##dve22lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
##dve22lts_grey_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
##dve22lts_grey_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# opaal2lts-grey
#opaal2lts_grey_SOURCES  = spec2lts-grey.c opaal-greybox.c opaal-greybox.h
#opaal2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS) $(MALLOC_LIBS)
#opaal2lts_grey_CPPFLAGS = -DOPAAL $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
#opaal2lts_grey_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

### spinja2lts-grey
##spinja2lts_grey_SOURCES  = spec2lts-grey.c spinja-greybox.c spinja-greybox.h
##spinja2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
##spinja2lts_grey_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
##spinja2lts_grey_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf2lts-gsea
etf2lts_gsea_SOURCES  = spec2lts-gsea.c etf-greybox.c etf-greybox.h
etf2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la
etf2lts_gsea_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# lpo2lts-gsea
lpo2lts_gsea_SOURCES  = spec2lts-gsea.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_gsea_SOURCES += at-map.c at-map.h
lpo2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la $(MCRL_LIBS)
lpo2lts_gsea_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_gsea_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-gsea
lps2lts_gsea_SOURCES  = spec2lts-gsea.c
lps2lts_gsea_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la libmc.la
lps2lts_gsea_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
lps2lts_gsea_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps2lts_gsea_SOURCES = automake-force-linker.cxx

# pbes2lts-gsea
pbes2lts_gsea_SOURCES  = spec2lts-gsea.c
pbes2lts_gsea_LDADD    = libvset.la libpbes-pins.la libltsmin.la libmc.la
pbes2lts_gsea_CPPFLAGS = -DPBES $(AM_CPPFLAGS)
nodist_EXTRA_pbes2lts_gsea_SOURCES = automake-force-linker.cxx

# dve2lts-gsea
dve2lts_gsea_SOURCES  = spec2lts-gsea.c dve-greybox.cpp dve-greybox.h
dve2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la $(DVEC_LIBS)
dve2lts_gsea_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_gsea_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-gsea
dve22lts_gsea_SOURCES  = spec2lts-gsea.c dve2-greybox.c dve2-greybox.h
dve22lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la $(DVEC2_LIBS)
dve22lts_gsea_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_gsea_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# opaal2lts-gsea
#opaal2lts_gsea_SOURCES  = spec2lts-gsea.c opaal-greybox.c opaal-greybox.h
#opaal2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la $(DVEC2_LIBS) $(MALLOC_LIBS)
#opaal2lts_gsea_CPPFLAGS = -DOPAAL $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
#opaal2lts_gsea_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-gsea
spinja2lts_gsea_SOURCES  = spec2lts-gsea.c spinja-greybox.c spinja-greybox.h
spinja2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la libmc.la $(SPINJA_LIBS)
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
lps_reach_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
nodist_EXTRA_lps_reach_SOURCES = automake-force-linker.cxx

# pbes-reach
pbes_reach_SOURCES  = spec-reach.c
pbes_reach_LDADD    = libvset.la libpbes-pins.la libltsmin.la libspg.la
pbes_reach_CPPFLAGS = -DPBES $(AM_CPPFLAGS)
nodist_EXTRA_pbes_reach_SOURCES = automake-force-linker.cxx

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

# opaal-reach
#opaal_reach_SOURCES  = spec-reach.c opaal-greybox.c opaal-greybox.h
#opaal_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS) $(MALLOC_LIBS)
#opaal_reach_CPPFLAGS = -DOPAAL $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
#opaal_reach_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja-reach
spinja_reach_SOURCES  = spec-reach.c spinja-greybox.c spinja-greybox.h
spinja_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja_reach_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja_reach_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# etf2lts-mc
etf2lts_mc_SOURCES  = spec2lts-mc.c etf-greybox.c etf-greybox.h
etf2lts_mc_LDADD    = libltsmin.la libmc.la
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
#lps2lts_mc_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
#nodist_EXTRA_lps2lts_mc_SOURCES = automake-force-linker.cxx

# dve2lts-mc
dve2lts_mc_SOURCES  = spec2lts-mc.c dve-greybox.cpp dve-greybox.h
dve2lts_mc_LDADD    = libltsmin.la libmc.la $(DVEC_LIBS)
dve2lts_mc_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_mc_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-mc
dve22lts_mc_SOURCES  = spec2lts-mc.c dve2-greybox.c dve2-greybox.h
dve22lts_mc_LDADD    = libltsmin.la libmc.la $(DVEC2_LIBS)
dve22lts_mc_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_mc_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-hre
dve22lts_hre_SOURCES  = spec2lts-mc.c dve2-greybox.c dve2-greybox.h
dve22lts_hre_LDADD    = libltsmin.la libmc.la $(DVEC2_LIBS)
dve22lts_hre_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_hre_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# opaal2lts-mc
opaal2lts_mc_SOURCES  = spec2lts-mc.c opaal-greybox.c opaal-greybox.h
opaal2lts_mc_LDADD    = libltsmin.la libmc.la $(DVEC2_LIBS) $(MALLOC_LIBS)
opaal2lts_mc_CPPFLAGS = -DOPAAL $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
opaal2lts_mc_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-mc
spinja2lts_mc_SOURCES  = spec2lts-mc.c spinja-greybox.c spinja-greybox.h
spinja2lts_mc_LDADD    = libltsmin.la libmc.la $(SPINJA_LIBS)
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

# libdve-open.la
libdve_open_la_SOURCES = spec_open.c dve-greybox.cpp dve-greybox.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve_open_la_LIBADD = libltsmin.la

# libdve2-open.la
libdve2_open_la_SOURCES = spec_open.c dve2-greybox.c dve2-greybox.h
libdve2_open_la_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve2_open_la_LIBADD = libltsmin.la

# libopaal-open.la
libopaal_open_la_SOURCES = spec_open.c opaal-greybox.c opaal-greybox.h
libopaal_open_la_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libopaal_open_la_LIBADD = libltsmin.la

# libspinja-open.la
libspinja_open_la_SOURCES = spec_open.c spinja-greybox.c spinja-greybox.h
libspinja_open_la_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libspinja_open_la_LIBADD = libltsmin.la


# test-is-balloc
test_is_balloc_SOURCES = test-is-balloc.c
test_is_balloc_LDADD   = libltsmin.la

# test-dfs-stack
test_dfs_stack_SOURCES = test-dfs-stack.c
test_dfs_stack_LDADD   = libltsmin.la

# test-dbs
test_dbs_SOURCES = test-dbs.c
test_dbs_LDADD   = libltsmin.la

# test-vset
test_vset_SOURCES = test-vset.c
test_vset_LDADD   = $(VSET_LIBS) libltsmin.la


# libraries
VSET_LIBS = libvset.la

libvset_la_SOURCES   = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES  += vset_listdd.c
libvset_la_SOURCES  += vset_buddy.c
libvset_la_CPPFLAGS  = $(BUDDY_CPPFLAGS)
libvset_la_LDFLAGS   = $(BUDDY_LDFLAGS)
libvset_la_LIBADD    = $(BUDDY_LIBS)

if HAVE_SYLVAN
libvset_la_SOURCES  += vset_sylvan.c
libvset_la_CPPFLAGS += $(SYLVAN_CPPFLAGS) 
libvset_la_LDFLAGS  += $(SYLVAN_LDFLAGS) 
libvset_la_LIBADD   += $(SYLVAN_LIBS)
endif

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


libmc_la_SOURCES = bitvector-ll.c bitvector-ll.h
libmc_la_SOURCES += cctables.c cctables.h
libmc_la_SOURCES += clt_table.c clt_table.h
libmc_la_SOURCES += dbs-ll.c dbs-ll.h
libmc_la_SOURCES += dfs-stack.c dfs-stack.h
libmc_la_SOURCES += hre-tools.h
libmc_la_SOURCES += is-balloc.c is-balloc.h
libmc_la_SOURCES += lb2.c lb2.h
libmc_la_SOURCES += lmap.c lmap.h
libmc_la_SOURCES += statistics.c statistics.h
libmc_la_SOURCES += trace.c trace.h
libmc_la_SOURCES += treedbs-ll.c treedbs-ll.h
libmc_la_SOURCES += zobrist.c zobrist.h


# ltsmin-hre settings
libltsmin_la_SOURCES  = balloc.c balloc.h
libltsmin_la_SOURCES += bitset.c bitset.h
libltsmin_la_SOURCES += chunk_support.c chunk_support.h
libltsmin_la_SOURCES += chunk_table_factory.c
libltsmin_la_SOURCES += config.h
libltsmin_la_SOURCES += dynamic-array.c dynamic-array.h
libltsmin_la_SOURCES += etf-internal.h
libltsmin_la_SOURCES += etf-objects.c etf-objects.h
libltsmin_la_SOURCES += etf-parser.c
libltsmin_la_SOURCES += etf-util.c etf-util.h
libltsmin_la_SOURCES += fast_hash.c fast_hash.h
libltsmin_la_SOURCES += greybox.c greybox.h
libltsmin_la_SOURCES += group.c
libltsmin_la_SOURCES += qsortex.c
libltsmin_la_SOURCES += ltl2ba-lex.c
libltsmin_la_SOURCES += lts-type.c lts-type.h
libltsmin_la_SOURCES += ltsmin-buchi.h
libltsmin_la_SOURCES += ltsmin-grammar.lemon
libltsmin_la_SOURCES += ltsmin-lexer.l
libltsmin_la_SOURCES += ltsmin-parse-env.h
libltsmin_la_SOURCES += ltsmin-syntax.c ltsmin-syntax.h
libltsmin_la_SOURCES += ltsmin-tl.c ltsmin-tl.h
libltsmin_la_SOURCES += pins-cache.c
libltsmin_la_SOURCES += pins-ltl.c
libltsmin_la_SOURCES += pins-por.c
libltsmin_la_SOURCES += ltsmin-grammar.c ltsmin-grammar.h
libltsmin_la_SOURCES += ltsmin-lexer.c ltsmin-lexer.h
libltsmin_la_SOURCES += string-map.c string-map.h
libltsmin_la_SOURCES += stringindex.c stringindex.h
libltsmin_la_SOURCES += tables.c tables.h
libltsmin_la_SOURCES += treedbs.c treedbs.h
libltsmin_la_SOURCES += unix.h

libltsmin_la_LIBADD   = hre/libhre.la
libltsmin_la_LIBADD  += hre-io/libhre-io.la 
libltsmin_la_LIBADD  += lts-io/liblts-io.la
libltsmin_la_LIBADD  += dm/libdm.la
libltsmin_la_LIBADD  += bignum/libbignum.la
libltsmin_la_LIBADD  += $(top_builddir)/ltl2ba/libltl2ba.la

libltsmin_la_CPPFLAGS  = -DNXT -DLTSMIN_HRE
libltsmin_la_CPPFLAGS += -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)


libspg_la_SOURCES  = spg-solve.c spg-solve.h
libspg_la_CPPFLAGS = -DNDEBUG $(AM_CPPFLAGS)
libspg_la_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
libspg_la_LIBADD   = libvset.la libltsmin.la bignum/libbignum.la
if HAVE_PROFILER
endif 

CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =


CLEANFILES += pins_open
DISTCLEANFILES += subst_pins_open.mk

EXTRA_DIST += pins_open.in
EXTRA_DIST += config.munge.pl subst_pins_open.mk

-include subst_pins_open.mk

pins_open: pins_open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

subst_pins_open.mk: pins_open.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@


CLEANFILES += ltsmin-grammar.c ltsmin-grammar.h
CLEANFILES += ltsmin-grammar.out
MAINTAINERCLEANFILES += ltsmin-tokens.c ltsmin-tokens.h

LEMON_FLAGS = -s

%.c %.h: %.lemon lempar.c
	cd $(top_builddir)/lemon && $(MAKE) $(AM_MAKEFLAGS) lemon$(EXEEXT)
	$(top_builddir)/lemon/lemon$(EXEEXT) $(LEMON_FLAGS) $<
	@if test -f $@; then touch $@; fi

%.c %.h: %.l
	$(am__skiplex) $(LEX) $<


EXTRA_DIST += git_version.h
DISTCLEANFILES += git_version.h.tmp
MAINTAINERCLEANFILES += git_version.h

BUILT_SOURCES += git_version.h

if HAVE_VERSION_CONTROL
GIT_DESCRIBE := $(shell cd $(top_srcdir) && git describe --tags --abbrev=6 --dirty 2>/dev/null)

git_version.h: git_version.h.tmp
	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

git_version.h.tmp:
	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > $@
.PHONY: git_version.h.tmp
endif

# make
.DELETE_ON_ERROR:
