bin_PROGRAMS = ltsmin-convert gcf etf-convert ltsmin-tracepp
check_PROGRAMS = test-bitset test-is-balloc test-dfs-stack test-hre ltsmin-parse test-dbs

noinst_LTLIBRARIES  = libutil.la
noinst_LTLIBRARIES += libvset.la
noinst_LTLIBRARIES += libltsmin.la

pkglib_LTLIBRARIES =

SUBDIRS = dm bignum . ce torx

EXTRA_DIST = ltsmin-lexer.c ltsmin-lexer.h pins_open
BUILT_SOURCES = ltsmin-grammar.c ltsmin-grammar.h ltsmin-lexer.c ltsmin-lexer.h

# pins_open
if HAVE_CADP
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS = pins_open
endif

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi etf2lts-mpi
check_PROGRAMS += mpi-event-test test-hre-mpi
noinst_LTLIBRARIES += libmpi.la libltsmin-mpi.la
if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-mpi
endif
if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-mpi
endif
#if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-mpi
#endif
if HAVE_DVEC
bin_PROGRAMS += dve2lts-mpi
endif
if HAVE_DVEC2
bin_PROGRAMS += dve22lts-mpi
endif
endif

if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-grey
bin_PROGRAMS += lpo2lts-gsea
bin_PROGRAMS += lpo-reach
if HAVE_CADP
pkglib_LTLIBRARIES += liblpo-open.la
endif
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-grey
bin_PROGRAMS += lps2lts-gsea
bin_PROGRAMS += lps-reach
noinst_LTLIBRARIES += libmcrl2-pins.la
if HAVE_CADP
pkglib_LTLIBRARIES += liblps-open.la
endif
endif

# if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-grey
bin_PROGRAMS += nips2lts-gsea
bin_PROGRAMS += nips-reach
bin_PROGRAMS += nips2lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libnips-open.la
endif
#endif

bin_PROGRAMS += etf2lts-grey
bin_PROGRAMS += etf2lts-gsea
bin_PROGRAMS += etf-reach
bin_PROGRAMS += etf2lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libetf-open.la
endif

if HAVE_DVEC
bin_PROGRAMS += dve2lts-grey
bin_PROGRAMS += dve2lts-gsea
bin_PROGRAMS += dve-reach
bin_PROGRAMS += dve2lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libdve-open.la
endif
endif

if HAVE_DVEC2
bin_PROGRAMS += dve22lts-grey
bin_PROGRAMS += dve22lts-gsea
bin_PROGRAMS += dve2-reach
bin_PROGRAMS += dve22lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libdve2-open.la
endif
endif

#if HAVE_SPINJA
bin_PROGRAMS += spinja2lts-grey
bin_PROGRAMS += spinja2lts-gsea
bin_PROGRAMS += spinja-reach
bin_PROGRAMS += spinja2lts-mc
#endif

libmcrl2_pins_la_SOURCES  = mcrl2-greybox.cpp mcrl2-greybox.h
libmcrl2_pins_la_SOURCES += at-map.c at-map.h
libmcrl2_pins_la_CPPFLAGS = $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
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

# spinja2lts-grey
spinja2lts_grey_SOURCES  = spec2lts-grey.c spinja-greybox.c spinja-greybox.h
spinja2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja2lts_grey_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_grey_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)

# spinja2lts-gsea
spinja2lts_gsea_SOURCES  = spec2lts-gsea.c spinja-greybox.c spinja-greybox.h
spinja2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja2lts_gsea_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja2lts_gsea_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)


# dve22lts-grey
dve22lts_grey_SOURCES  = spec2lts-grey.c dve2-greybox.c dve2-greybox.h
dve22lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve22lts_grey_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_grey_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# dve22lts-gsea
dve22lts_gsea_SOURCES  = spec2lts-gsea.c dve2-greybox.c dve2-greybox.h
dve22lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve22lts_gsea_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_gsea_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-grey
dve2lts_grey_SOURCES  = spec2lts-grey.c dve-greybox.cpp dve-greybox.h
dve2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve2lts_grey_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_grey_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# dve2lts-gsea
dve2lts_gsea_SOURCES  = spec2lts-gsea.c dve-greybox.cpp dve-greybox.h
dve2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve2lts_gsea_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_gsea_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# etf2lts-grey
etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h
etf2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la
etf2lts_grey_CPPFLAGS = -DETF $(AM_CPPFLAGS)

# etf2lts-gsea
etf2lts_gsea_SOURCES  = spec2lts-gsea.c etf-greybox.c etf-greybox.h
etf2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la
etf2lts_gsea_CPPFLAGS = -DETF $(AM_CPPFLAGS)


# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_grey_SOURCES += at-map.c at-map.h
lpo2lts_grey_LDADD    = libvset-mcrl.la libltsmin.la $(MCRL_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lpo2lts-gsea
lpo2lts_gsea_SOURCES  = spec2lts-gsea.c mcrl-greybox.c mcrl-greybox.h
lpo2lts_gsea_SOURCES += at-map.c at-map.h
lpo2lts_gsea_LDADD    = libvset-mcrl.la libltsmin.la $(MCRL_LIBS)
lpo2lts_gsea_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_gsea_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c
lps2lts_grey_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
lps2lts_grey_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps2lts_grey_SOURCES = automake-force-linker.cxx

# lps2lts-gsea
lps2lts_gsea_SOURCES  = spec2lts-gsea.c
lps2lts_gsea_LDADD    = $(VSET_LIBS) libmcrl2-pins.la libltsmin.la
lps2lts_gsea_CPPFLAGS = -DMCRL2 $(AM_CPPFLAGS)
nodist_EXTRA_lps2lts_gsea_SOURCES = automake-force-linker.cxx

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h
nips2lts_grey_LDADD    = $(VSET_LIBS) libltsmin.la $(NIPS_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# nips2lts-gsea
nips2lts_gsea_SOURCES  = spec2lts-gsea.c nips-greybox.c nips-greybox.h
nips2lts_gsea_LDADD    = $(VSET_LIBS) libltsmin.la $(NIPS_LIBS)
nips2lts_gsea_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_gsea_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

# spinja-reach
spinja_reach_SOURCES  = spec-reach.c spinja-greybox.c spinja-greybox.h
spinja_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(SPINJA_LIBS)
spinja_reach_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(AM_CPPFLAGS)
spinja_reach_LDFLAGS  = $(SPINJA_LDFLAGS) $(AM_LDFLAGS)

# dve2-reach
dve2_reach_SOURCES  = spec-reach.c dve2-greybox.c dve2-greybox.h
dve2_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC2_LIBS)
dve2_reach_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve2_reach_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)


# dve-reach
dve_reach_SOURCES  = spec-reach.c dve-greybox.cpp dve-greybox.h
dve_reach_LDADD    = $(VSET_LIBS) libltsmin.la $(DVEC_LIBS)
dve_reach_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve_reach_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h
lpo_reach_SOURCES += at-map.c at-map.h
lpo_reach_LDADD    = libvset-mcrl.la libltsmin.la $(MCRL_LIBS)
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

# etf-reach
etf_reach_SOURCES  = spec-reach.c etf-greybox.c etf-greybox.h
etf_reach_LDADD    = $(VSET_LIBS) libltsmin.la
etf_reach_CPPFLAGS = -DETF $(AM_CPPFLAGS)


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

# etf2lts-mpi
etf2lts_mpi_SOURCES  = spec2lts-mpi.c etf-greybox.c etf-greybox.h
etf2lts_mpi_LDADD    = libltsmin-mpi.la
etf2lts_mpi_CPPFLAGS = -DETF $(AM_CPPFLAGS)


# libdve-open.la
libdve_open_la_SOURCES = spec_open.c dve-greybox.cpp dve-greybox.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libdve_open_la_LIBADD = libltsmin.la

# libdve2-open.la
libdve2_open_la_SOURCES = spec_open.c dve2-greybox.c dve2-greybox.h
libdve2_open_la_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libdve2_open_la_LIBADD = libltsmin.la

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

# etf2lts-mc
etf2lts_mc_SOURCES  = spec2lts-mc.c etf-greybox.c etf-greybox.h
etf2lts_mc_LDADD    = libltsmin.la
etf2lts_mc_CPPFLAGS = -DETF $(AM_CPPFLAGS)

## mCRL and mCRL2 (ATerms) doesnt work concurrently
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

# libetf-open.la
libetf_open_la_SOURCES = spec_open.c etf-greybox.c etf-greybox.h
libetf_open_la_CPPFLAGS = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD = libltsmin.la

# libnips-open.la
libnips_open_la_SOURCES = spec_open.c nips-greybox.c nips-greybox.h
libnips_open_la_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libnips_open_la_LIBADD = ${top_builddir}/nips-vm/libnips_vm.la libltsmin.la

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
ltsmin_mpi_SOURCES = sigmin-mpi.c sigmin-sig.h config.h seg-lts.c seg-lts.h \
                     sig-array.c sig-array.h sigmin-array.c sigmin-array.h \
                     sigmin-set.c sigmin-set.h set.c set.h sigmin-types.h
ltsmin_mpi_LDADD   = libltsmin-mpi.la


# ltsmin-convert
ltsmin_convert_SOURCES = ltsmin-convert.c
ltsmin_convert_LDADD = libltsmin.la

# libraries
if HAVE_LIBMCRL2
noinst_LTLIBRARIES += libvset-mcrl2.la
VSET_LIBS = libvset-mcrl2.la
else
if HAVE_LIBMCRL
noinst_LTLIBRARIES += libvset-mcrl.la
VSET_LIBS = libvset-mcrl.la
else
VSET_LIBS = libvset.la
endif
endif

libvset_la_SOURCES  = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES += vset_listdd.c
libvset_la_SOURCES += vset_buddy.c
libvset_la_CPPFLAGS = $(BUDDY_CPPFLAGS) $(AM_CPPFLAGS)
libvset_la_LDFLAGS  = $(BUDDY_LDFLAGS) $(AM_LDFLAGS)
libvset_la_LIBADD   = $(BUDDY_LIBS)

libvset_mcrl_la_SOURCES  = vset_atermdd.c
libvset_mcrl_la_CPPFLAGS = $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
libvset_mcrl_la_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)
libvset_mcrl_la_LIBADD   = libvset.la -lATerm

libvset_mcrl2_la_SOURCES  = vset_atermdd.c
libvset_mcrl2_la_CPPFLAGS = $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
libvset_mcrl2_la_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libvset_mcrl2_la_LIBADD   = libvset.la -laterm


libmpi_la_SOURCES = mpi_io_stream.c mpi_raf.c mpi_io_stream.h \
		   mpi-event-loop.c mpi-event-loop.h mpi-runtime.c \
		   mpi-runtime.h task-queue.c task-queue.h

libutil_la_SOURCES := unix.c runtime.c stream.c archive.c			\
                    stream_buffer.c fast_hash.c generichash4.c		\
                    generichash8.c treedbs.c archive_format.c raf.c	\
                    stream_mem.c ghf.c archive_gcf.c scctimer.c		\
                    gzstream.c stream_diff32.c arch_object.h	\
                    generichash.h unix.h archive.h ghf.h	\
                    stream.h raf.h stream_object.h raf_object.h	stream_fd.c \
                    string-map.c string-map.h \
                    tables.c tables.h chunk_table_factory.c \
                    scctimer.h fast_hash.h runtime.h treedbs.h		\
                    config.h greybox.c greybox.h spec-greybox.h \
                    pins-cache.c group.c   \
                    stringindex.c stringindex.h archive_gsf.c           \
                    dir_ops.c dir_ops.h	\
                    packet_stream.h packet_stream.c	\
                    archive_dir.c chunk_support.c chunk_support.h	\
                    dynamic-array.c dynamic-array.h struct_io.c		\
                    struct_io.h etf-util.c etf-util.h lts-type.c	\
                    etf-internal.h etf-objects.c etf-objects.h etf-parser.c \
                    ltsmin-lexer.l ltsmin-syntax.c ltsmin-syntax.h \
                    ltsmin-grammar.lemon ltsmin-parse-env.h \
                    ltsmin-tl.c ltsmin-tl.h \
                    lts-type.h \
                    lts_io.c lts_io.h lts_io_internal.h \
                    lts_enum.c lts_enum.h lts_count.c lts_count.h \
                    archive_io_ops.c fifo.c fifo.h \
                    trace.c trace.h \
                    is-balloc.c is-balloc.h \
                    dfs-stack.c dfs-stack.h \
                    tls.c tls.h \
                    dbs.c dbs.h \
                    dbs-ll.c dbs-ll.h \
                    treedbs-ll.c treedbs-ll.h \
                    cctables.c cctables.h \
                    zobrist.c zobrist.h \
                    lb.c lb.h \
                    balloc.c balloc.h bitset.c bitset.h

if HAVE_LIBBCG
libutil_la_SOURCES  += bcg_io_ops.c
endif

libutil_la_CPPFLAGS = $(BUDDY_CPPFLAGS) $(CADP_CPPFLAGS)
libutil_la_LDFLAGS  = $(BUDDY_LDFLAGS) $(CADP_LDFLAGS)
libutil_la_LIBADD   = ltsmin-grammar.lo dm/libdm.la $(BUDDY_LIBS) $(CADP_LIBS)

libutil_la_SOURCES += ltl2ba-lex.c
libutil_la_SOURCES += ltsmin-buchi.h
libutil_la_CPPFLAGS += -DNXT -I${top_srcdir}/ltl2ba
libutil_la_LIBADD += $(top_builddir)/ltl2ba/libltl2ba.la

libutil_la_SOURCES += pins-ltl.c


libutil_la_SOURCES += hre-context.c hre-context.h \
	hre-feedback.c hre-feedback.h hre-internal.h \
	hre-main.c hre-main.h hre-malloc.c hre-malloc.h \
	hre-popt.c hre-popt.h hre-pthread.c


libutil_la_CPPFLAGS += $(AM_CPPFLAGS)


libltsmin_la_SOURCES = hre-nompi.c
libltsmin_la_LIBADD = libutil.la bignum/libbignum.la

if HAVE_MPI
libltsmin_mpi_la_SOURCES = hre-mpi.c
libltsmin_mpi_la_LIBADD = libutil.la libmpi.la
endif

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

DISTCLEANFILES = git_version.h git_version.h.tmp
CLEANFILES = ltsmin-grammar.c ltsmin-grammar.h ltsmin-grammar.out pins_open
MAINTAINERCLEANFILES = ltsmin-tokens.c ltsmin-tokens.h

# make
.DELETE_ON_ERROR:
