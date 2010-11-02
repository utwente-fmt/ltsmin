bin_PROGRAMS = ltsmin-convert gcf etf-convert ltsmin-tracepp
check_PROGRAMS = test-bitset test-is-balloc test-dfs-stack test-hre ltsmin-parse test-dbs
check_LTLIBRARIES = libutil.la libltsmin.la
pkglib_LTLIBRARIES =

SUBDIRS = dm bignum . ce torx

EXTRA_DIST = ltsmin-lexer.c ltsmin-lexer.h pins_open
BUILT_SOURCES = ltsmin-grammar.c ltsmin-grammar.h ltsmin-lexer.c ltsmin-lexer.h

# pins_open
if HAVE_CADP
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS = pins_open
pkglib_LTLIBRARIES += libnips-open.la libetf-open.la
endif

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi etf2lts-mpi
check_PROGRAMS += mpi-event-test test-hre-mpi
check_LTLIBRARIES += libmpi.la libltsmin-mpi.la
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
#if HAVE_DVEC2
#bin_PROGRAMS += dve22lts-mpi
#endif
endif
if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2lts-grey lpo-reach
if HAVE_CADP
pkglib_LTLIBRARIES += liblpo-open.la
endif
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-grey lps-reach
if HAVE_CADP
pkglib_LTLIBRARIES += liblps-open.la
endif
endif

if HAVE_LIBMCRL
# if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-grey nips-reach etf2lts-grey etf-reach \
  nips2lts-mc etf2lts-mc
# endif
endif

if HAVE_DVEC
bin_PROGRAMS += dve2lts-grey dve-reach dve2lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libdve-open.la
endif
endif

if HAVE_DVEC2
bin_PROGRAMS += dve22lts-grey dve2-reach \
  dve22lts-mc
if HAVE_CADP
pkglib_LTLIBRARIES += libdve2-open.la
endif
endif

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libltsmin.la

# ltsmin-tracepp
ltsmin_tracepp_SOURCES = ltsmin-tracepp.c
ltsmin_tracepp_LDADD = libltsmin.la $(CADP_LIBS)
ltsmin_tracepp_LDFLAGS = $(AM_LDFLAGS) $(CADP_LDFLAGS)

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
ltsmin_parse_LDADD = libltsmin.la

# dve22lts-grey
dve22lts_grey_SOURCES  = spec2lts-grey.c dve2-greybox.c dve2-greybox.h    \
                         lts.c lts.h vset_atermdd.c
dve22lts_grey_LDADD    = libltsmin.la -lATerm $(DVEC2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
dve22lts_grey_CPPFLAGS = -DDIVINE2 $(MCRL_CPPFLAGS) $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_grey_LDFLAGS  = $(DIVINE2_LDFLAGS) $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# dve2lts-grey
dve2lts_grey_SOURCES  = spec2lts-grey.c dve-greybox.cpp dve-greybox.h    \
                        vset_atermdd.c
dve2lts_grey_LDADD    = libltsmin.la -lATerm $(DVEC_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
dve2lts_grey_CPPFLAGS = -DDIVINE $(MCRL_CPPFLAGS) $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_grey_LDFLAGS  = $(DIVINE_LDFLAGS) $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf2lts-grey
etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h    \
                        vset_atermdd.c
etf2lts_grey_LDADD    = libltsmin.la -lATerm $(BUDDY_LIBS) $(CADP_LIBS)
etf2lts_grey_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h    \
                        at-map.c at-map.h vset_atermdd.c
lpo2lts_grey_LDADD    = libltsmin.la $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c mcrl2-greybox.cpp		\
                        mcrl2-greybox.h at-map.c at-map.h vset_atermdd.c
lps2lts_grey_LDADD    = libltsmin.la $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lps2lts_grey_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_grey_LDFLAGS  = $(MCRL2_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h   \
                         vset_atermdd.c
nips2lts_grey_LDADD    = libltsmin.la -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(BUDDY_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# dve2-reach
dve2_reach_SOURCES  = spec-reach.c dve2-greybox.c dve2-greybox.h  \
                     lts.c lts.h vset_atermdd.c
dve2_reach_LDADD    = libltsmin.la -lATerm $(DVEC2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
dve2_reach_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
dve2_reach_LDFLAGS  = $(DIVINE2_LDFLAGS) $(BUDDY_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# dve-reach
dve_reach_SOURCES  = spec-reach.c dve-greybox.cpp dve-greybox.h  \
                     vset_atermdd.c
dve_reach_LDADD    = libltsmin.la -lATerm $(DVEC_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
dve_reach_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
dve_reach_LDFLAGS  = $(DIVINE_LDFLAGS) $(BUDDY_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h  \
                     at-map.c at-map.h vset_atermdd.c
lpo_reach_LDADD    = libltsmin.la $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lpo_reach_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps-reach
lps_reach_SOURCES  = spec-reach.c mcrl2-greybox.cpp mcrl2-greybox.h      \
                     at-map.c at-map.h vset_atermdd.c
lps_reach_LDADD    = libltsmin.la $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lps_reach_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps_reach_LDFLAGS  = $(MCRL2_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips-reach
nips_reach_SOURCES  = spec-reach.c nips-greybox.c nips-greybox.h	\
                      vset_atermdd.c
nips_reach_LDADD    = libltsmin.la -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips_reach_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips_reach_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf-reach
etf_reach_SOURCES  = spec-reach.c etf-greybox.c etf-greybox.h    \
                     vset_atermdd.c
etf_reach_LDADD    = libltsmin.la -lATerm  $(BUDDY_LIBS) $(CADP_LIBS)
etf_reach_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# dve2lts-mpi
dve2lts_mpi_SOURCES  = spec2lts-mpi.c dve-greybox.cpp dve-greybox.h config.h
dve2lts_mpi_LDADD    = libltsmin-mpi.la $(DVEC_LIBS) $(CADP_LIBS)
dve2lts_mpi_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_mpi_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

## dve22lts-mpi
#dve22lts_mpi_SOURCES  = spec2lts-mpi.c dve2-greybox.cpp dve2-greybox.h config.h
#dve22lts_mpi_LDADD    = libltsmin-mpi.la $(DVEC2_LIBS) $(CADP_LIBS)
#dve22lts_mpi_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) \
#                          $(AM_CPPFLAGS)
#dve22lts_mpi_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h \
                       at-map.c at-map.h config.h
lpo2lts_mpi_LDADD    = libltsmin-mpi.la $(MCRL_LIBS) $(CADP_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-mpi
lps2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl2-greybox.cpp mcrl2-greybox.h	\
                       at-map.c at-map.h config.h
lps2lts_mpi_LDADD    = libltsmin-mpi.la $(MCRL2_LIBS) $(CADP_LIBS)
lps2lts_mpi_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_mpi_LDFLAGS  = $(MCRL2_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-mpi
nips2lts_mpi_SOURCES  = spec2lts-mpi.c nips-greybox.c nips-greybox.h config.h
nips2lts_mpi_LDADD    = libltsmin-mpi.la $(NIPS_LIBS) $(CADP_LIBS)
nips2lts_mpi_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mpi_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf2lts-mpi
etf2lts_mpi_SOURCES  = spec2lts-mpi.c etf-greybox.c etf-greybox.h config.h
etf2lts_mpi_LDADD    = libltsmin-mpi.la $(CADP_LIBS)
etf2lts_mpi_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_mpi_LDFLAGS  = $(AM_LDFLAGS) $(MCRL_LDFLAGS) $(CADP_LDFLAGS)


# libdve-open.la
libdve_open_la_SOURCES = spec_open.c dve-greybox.cpp dve-greybox.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libdve_open_la_LIBADD = libltsmin.la

# libdve2-open.la
libdve2_open_la_SOURCES = spec_open.c dve2-greybox.c dve2-greybox.h
libdve2_open_la_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libdve2_open_la_LIBADD = libltsmin.la

# dve2lts-mc
dve2lts_mc_SOURCES  = spec2lts-mc.c dve-greybox.cpp dve-greybox.h config.h
dve2lts_mc_LDADD    = libltsmin.la $(DVEC_LIBS) $(CADP_LIBS)
dve2lts_mc_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(AM_CPPFLAGS)
dve2lts_mc_LDFLAGS  = $(DIVINE_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# dve22lts-mc
dve22lts_mc_SOURCES  = spec2lts-mc.c dve2-greybox.c dve2-greybox.h config.h
dve22lts_mc_LDADD    = libltsmin.la $(DVEC2_LIBS)
dve22lts_mc_CPPFLAGS = -DDIVINE2 $(DIVINE2_CPPFLAGS) $(AM_CPPFLAGS)
dve22lts_mc_LDFLAGS  = $(DIVINE2_LDFLAGS) $(AM_LDFLAGS)

# etf2lts-mc
etf2lts_mc_SOURCES  = spec2lts-mc.c etf-greybox.c etf-greybox.h config.h \
                      lts.c lts.h
etf2lts_mc_LDADD    = libltsmin.la $(BUDDY_LIBS) $(CADP_LIBS)
etf2lts_mc_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_mc_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

## mCRL and mCRL2 (ATerms) doesnt work concurrently
## lpo2lts-mc
#lpo2lts_mc_SOURCES  = spec2lts-mc.c mcrl-greybox.c mcrl-greybox.h    \
#                        at-map.c at-map.h vset_atermdd.c
#lpo2lts_mc_LDADD    = libltsmin.la $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
#lpo2lts_mc_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
#lpo2lts_mc_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

## lps2lts-mc
#lps2lts_mc_SOURCES  = spec2lts-mc.c mcrl2-greybox.cpp		\
#                        mcrl2-greybox.h at-map.c at-map.h vset_atermdd.c
#lps2lts_mc_LDADD    = libltsmin.la $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
#lps2lts_mc_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
#lps2lts_mc_LDFLAGS  = $(MCRL2_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-mc
nips2lts_mc_SOURCES  = spec2lts-mc.c nips-greybox.c nips-greybox.h vset_atermdd.c
nips2lts_mc_LDADD    = libltsmin.la -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips2lts_mc_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mc_LDFLAGS  = $(NIPS_LDFLAGS) $(BUDDY_LDFLAGS) $(MCRL_LDFLAGS) \
                        $(AM_LDFLAGS) $(CADP_LDFLAGS)


# liblpo-open.la
liblpo_open_la_SOURCES = spec_open.c mcrl-greybox.c mcrl-greybox.h \
			 at-map.c at-map.h
liblpo_open_la_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)  $(CADP_CPPFLAGS)
liblpo_open_la_LIBADD = libltsmin.la

# liblps-open.la
liblps_open_la_SOURCES = spec_open.c mcrl2-greybox.cpp mcrl2-greybox.h \
			 at-map.c at-map.h
liblps_open_la_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
liblps_open_la_LIBADD = libltsmin.la

# libetf-open.la
libetf_open_la_SOURCES = spec_open.c etf-greybox.c etf-greybox.h
libetf_open_la_CPPFLAGS = -DETF $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libetf_open_la_LIBADD = libltsmin.la

# libnips-open.la
libnips_open_la_SOURCES = spec_open.c nips-greybox.c nips-greybox.h
libnips_open_la_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libnips_open_la_LIBADD = ${top_builddir}/nips-vm/libnips_vm.la libltsmin.la

# mpi-event-test
mpi_event_test_SOURCES = mpi-event-test.c config.h
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
ltsmin_mpi_LDFLAGS = $(AM_LDFLAGS) $(CADP_LDFLAGS)
ltsmin_mpi_LDADD   = libltsmin-mpi.la $(CADP_LIBS)


# ltsmin-convert
ltsmin_convert_SOURCES = ltsmin-convert.c
ltsmin_convert_LDADD = libltsmin.la $(CADP_LIBS)
ltsmin_convert_LDFLAGS = $(CADP_LDFLAGS) $(AM_LDFLAGS)
ltsmin_convert_CPPFLAGS = $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

# libraries
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
                    config.h greybox.c greybox.h pins-cache.c group.c   \
                    stringindex.c stringindex.h archive_gsf.c           \
                    dir_ops.c dir_ops.h	\
                    packet_stream.h packet_stream.c	\
                    archive_dir.c chunk_support.c chunk_support.h	\
                    dynamic-array.c dynamic-array.h struct_io.c		\
                    struct_io.h etf-util.c etf-util.h lts-type.c	\
                    etf-internal.h etf-objects.c etf-objects.h etf-parser.c \
                    ltsmin-lexer.l ltsmin-syntax.c ltsmin-syntax.h \
                    ltsmin-grammar.lemon ltsmin-parse-env.h \
                    ltsmin-mu.c ltsmin-mu.h \
                    lts-type.h vset_buddy.c vector_set.c vector_set.h	\
                    vset_listdd.c \
                    vdom_object.h lts_io.c lts_io.h lts_io_internal.h \
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

libutil_la_LIBADD = ltsmin-grammar.lo dm/libdm.la

libutil_la_CPPFLAGS = $(BUDDY_CPPFLAGS)
if HAVE_LIBBCG
libutil_la_SOURCES  += bcg_io_ops.c
libutil_la_CPPFLAGS += $(CADP_CPPFLAGS)
endif
libutil_la_CPPFLAGS += $(AM_CPPFLAGS)

libutil_la_SOURCES += hre-context.c hre-context.h \
	hre-feedback.c hre-feedback.h hre-internal.h \
	hre-main.c hre-main.h hre-malloc.c hre-malloc.h \
	hre-popt.c hre-popt.h hre-pthread.c

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
