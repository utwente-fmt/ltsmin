bin_PROGRAMS = ltsmin-convert gcf etf-convert
noinst_PROGRAMS = test-bitset
noinst_LTLIBRARIES = libutil.la
pkglib_LTLIBRARIES =

SUBDIRS = dm . ce

# pins_open
if HAVE_CADP
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS = pins_open
endif

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi
bin_PROGRAMS += etf2lts-mpi
noinst_PROGRAMS += mpi-event-test
noinst_LTLIBRARIES += libmpi.la
if HAVE_LIBSTEP_INFO
bin_PROGRAMS += lpo2lts-mpi
endif
if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-mpi
endif
#if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-mpi
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
bin_PROGRAMS += nips2lts-grey nips-reach etf2lts-grey etf-reach
if HAVE_CADP
pkglib_LTLIBRARIES += libnips-open.la libetf-open.la
endif
# endif
endif

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libutil.la

# gcf
gcf_SOURCES = gcf-rw.c
gcf_LDADD = libutil.la

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD = libutil.la

# etf2lts-grey
etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h    \
                        lts.c lts.h vset_atermdd.c
etf2lts_grey_LDADD    = libutil.la -lATerm $(BUDDY_LIBS) $(CADP_LIBS)
etf2lts_grey_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h    \
                        at-map.c at-map.h vset_atermdd.c
lpo2lts_grey_LDADD    = libutil.la $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c mcrl2-greybox.cpp		\
                        mcrl2-greybox.h at-map.c at-map.h vset_atermdd.c
lps2lts_grey_LDADD    = libutil.la $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lps2lts_grey_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_grey_LDFLAGS  = $(MCRL2_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h   \
                         vset_atermdd.c
nips2lts_grey_LDADD    = libutil.la -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(BUDDY_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h  \
                     at-map.c at-map.h vset_atermdd.c
lpo_reach_LDADD    = libutil.la $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lpo_reach_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps-reach
lps_reach_SOURCES  = spec-reach.c mcrl2-greybox.cpp mcrl2-greybox.h      \
                     at-map.c at-map.h vset_atermdd.c
lps_reach_LDADD    = libutil.la $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lps_reach_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps_reach_LDFLAGS  = $(MCRL2_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips-reach
nips_reach_SOURCES  = spec-reach.c nips-greybox.c nips-greybox.h	\
                      vset_atermdd.c
nips_reach_LDADD    = libutil.la -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips_reach_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips_reach_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf-reach
etf_reach_SOURCES  = spec-reach.c etf-greybox.c etf-greybox.h    \
                        lts.c lts.h vset_atermdd.c
etf_reach_LDADD    = libutil.la -lATerm  $(BUDDY_LIBS) $(CADP_LIBS)
etf_reach_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(BUDDY_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h \
                       at-map.c at-map.h config.h
lpo2lts_mpi_LDADD    = libmpi.la libutil.la $(MCRL_LIBS) $(CADP_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-mpi
lps2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl2-greybox.cpp mcrl2-greybox.h	\
                       at-map.c at-map.h config.h
lps2lts_mpi_LDADD    = libmpi.la libutil.la $(MCRL2_LIBS) $(CADP_LIBS)
lps2lts_mpi_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_mpi_LDFLAGS  = $(MCRL2_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-mpi
nips2lts_mpi_SOURCES  = spec2lts-mpi.c nips-greybox.c nips-greybox.h config.h
nips2lts_mpi_LDADD    = libmpi.la libutil.la $(NIPS_LIBS) $(CADP_LIBS)
nips2lts_mpi_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mpi_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf2lts-mpi
etf2lts_mpi_SOURCES  = spec2lts-mpi.c etf-greybox.c etf-greybox.h config.h lts.c lts.h 
etf2lts_mpi_LDADD    = libmpi.la libutil.la $(CADP_LIBS)
etf2lts_mpi_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_mpi_LDFLAGS  = $(AM_LDFLAGS) $(MCRL_LDFLAGS) $(CADP_LDFLAGS)


# liblpo-open.la
liblpo_open_la_SOURCES = spec_open.c mcrl-greybox.c mcrl-greybox.h \
			 at-map.c at-map.h vset_atermdd.c
liblpo_open_la_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)  $(CADP_CPPFLAGS)
liblpo_open_la_LIBADD = libutil.la

# liblps-open.la
liblps_open_la_SOURCES = spec_open.c mcrl2-greybox.cpp mcrl2-greybox.h \
			 at-map.c at-map.h vset_atermdd.c
liblps_open_la_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
liblps_open_la_LIBADD = libutil.la

# libetf-open.la
libetf_open_la_SOURCES = spec_open.c etf-greybox.c etf-greybox.h    \
                         lts.c lts.h vset_atermdd.c
libetf_open_la_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libetf_open_la_LIBADD = libutil.la

# libnips-open.la
libnips_open_la_SOURCES = spec_open.c nips-greybox.c nips-greybox.h   \
                           vset_atermdd.c
libnips_open_la_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS) $(CADP_CPPFLAGS)
libnips_open_la_LIBADD = ${top_builddir}/nips-vm/libnips_vm.la libutil.la

# mpi-event-test
mpi_event_test_SOURCES = mpi-event-test.c config.h
mpi_event_test_LDADD   = libmpi.la libutil.la

# ltsmin-mpi
ltsmin_mpi_SOURCES = ltsmin-mpi.c set.c lts.c dlts.c set.h lts.h	\
                     dlts.h config.h
ltsmin_mpi_LDADD   = libmpi.la libutil.la

# ltsmin-convert
ltsmin_convert_SOURCES = ltsmin-convert.c
ltsmin_convert_LDADD = libutil.la $(CADP_LIBS)
ltsmin_convert_LDFLAGS = $(CADP_LDFLAGS) $(AM_LDFLAGS)
ltsmin_convert_CPPFLAGS = $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

# libraries
libmpi_la_SOURCES = mpi_io_stream.c mpi_core.c mpi_raf.c mpi_ram_raf.c	\
		   mpi_core.h mpi_io_stream.h mpi_ram_raf.h config.h	\
		   mpi-event-loop.c mpi-event-loop.h mpi-runtime.c \
		   mpi-runtime.h task-queue.c task-queue.h

libutil_la_SOURCES := unix.c runtime.c stream.c archive.c			\
                    stream_buffer.c fast_hash.c generichash4.c		\
                    generichash8.c treedbs.c archive_format.c raf.c	\
                    stream_mem.c ghf.c archive_gcf.c scctimer.c		\
                    gzstream.c stream_diff32.c arch_object.h	\
                    generichash.h unix.h archive.h ghf.h	\
                    stream.h raf.h stream_object.h raf_object.h		\
                    scctimer.h fast_hash.h runtime.h treedbs.h		\
                    config.h greybox.c greybox.h pins-cache.c group.c   \
                    stringindex.c stringindex.h archive_gsf.c           \
                    dir_ops.c dir_ops.h	\
                    packet_stream.h packet_stream.c	\
                    archive_dir.c chunk_support.c chunk_support.h	\
                    dynamic-array.c dynamic-array.h struct_io.c		\
                    struct_io.h etf-util.c etf-util.h lts-type.c	\
                    lts-type.h vset_buddy.c vector_set.c vector_set.h	\
                    vdom_object.h lts_io.c lts_io.h lts_io_internal.h \
                    lts_enum.c lts_enum.h lts_count.c lts_count.h \
                    archive_io_ops.c fifo.c fifo.h \
                    balloc.c balloc.h bitset.c bitset.h

libutil_la_LIBADD = dm/libdm.la

libutil_la_CPPFLAGS = $(BUDDY_CPPFLAGS)
if HAVE_LIBBCG
libutil_la_SOURCES  += bcg_io_ops.c
libutil_la_CPPFLAGS += $(CADP_CPPFLAGS)
endif
libutil_la_CPPFLAGS += $(AM_CPPFLAGS)

LEMON_FLAGS = -s

%.c %.h: %.lemon lempar.c
	cd $(top_builddir)/lemon && $(MAKE) $(AM_MAKEFLAGS) lemon$(EXEEXT)
	$(top_builddir)/lemon/lemon$(EXEEXT) $(LEMON_FLAGS) $<

%.c: %.l
	$(am__skiplex) $(LEX) $<

runtime.c: git_version.h

GIT_DESCRIBE := $(shell git describe --tags --abbrev=6 HEAD 2>/dev/null)

git_version.h: git_version.h.tmp
	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

git_version.h.tmp:
	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > git_version.h.tmp
.PHONY: git_version.h.tmp

DISTCLEANFILES = git_version.h git_version.h.tmp

# make
.DELETE_ON_ERROR:
