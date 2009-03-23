bin_PROGRAMS = ltsmin-convert gcf etf-convert
noinst_PROGRAMS =
noinst_LIBRARIES = libutil.a

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi etf2lts-mpi
noinst_PROGRAMS += mpi-event-test
noinst_LIBRARIES += libmpi.a
if HAVE_LIBMCRL
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
endif

if HAVE_LIBMCRL2
bin_PROGRAMS += lps2lts-grey lps-reach
endif

if HAVE_LIBMCRL
# if HAVE_LIBNIPS
bin_PROGRAMS += nips2lts-grey nips-reach etf2lts-grey etf-reach
# endif
endif

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libutil.a

# gcf
gcf_SOURCES = gcf-rw.c
gcf_LDADD = libutil.a


# etf2lts-grey
etf2lts_grey_SOURCES  = spec2lts-grey.c etf-greybox.c etf-greybox.h    \
                        lts.c lts.h vset_atermdd.c
etf2lts_grey_LDADD    = libutil.a -lATerm $(BUDDY_LIBS) $(CADP_LIBS)
etf2lts_grey_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)


# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h    \
                        at-map.c at-map.h vset_atermdd.c
lpo2lts_grey_LDADD    = libutil.a $(MCRL_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c mcrl2-greybox.cpp		\
                        mcrl2-greybox.h at-map.c at-map.h vset_atermdd.c
lps2lts_grey_LDADD    = libutil.a $(MCRL2_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
lps2lts_grey_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_grey_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h   \
                         vset_atermdd.c
nips2lts_grey_LDADD    = libutil.a -lATerm $(NIPS_LIBS) $(BUDDY_LIBS) $(CADP_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h  \
                     at-map.c at-map.h vset_atermdd.c
lpo_reach_LDADD    = libutil.a $(MCRL_LIBS) $(BUDDY_LIBS)
lpo_reach_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps-reach
lps_reach_SOURCES  = spec-reach.c mcrl2-greybox.cpp mcrl2-greybox.h      \
                     at-map.c at-map.h vset_atermdd.c
lps_reach_LDADD    = libutil.a $(MCRL2_LIBS) $(BUDDY_LIBS)
lps_reach_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps_reach_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)

# nips-reach
nips_reach_SOURCES  = spec-reach.c nips-greybox.c nips-greybox.h	\
                      vset_atermdd.c
nips_reach_LDADD    = libutil.a -lATerm $(NIPS_LIBS) $(BUDDY_LIBS)
nips_reach_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips_reach_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# etf-reach
etf_reach_SOURCES  = spec-reach.c etf-greybox.c etf-greybox.h    \
                        lts.c lts.h vset_atermdd.c
etf_reach_LDADD    = libutil.a -lATerm  $(BUDDY_LIBS)
etf_reach_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)


# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h	\
                       at-map.c at-map.h config.h
lpo2lts_mpi_LDADD    = libmpi.a libutil.a $(MCRL_LIBS) $(CADP_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# lps2lts-mpi
lps2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl2-greybox.cpp mcrl2-greybox.h	\
                       at-map.c at-map.h config.h
lps2lts_mpi_LDADD    = libmpi.a libutil.a $(MCRL2_LIBS) $(CADP_LIBS)
lps2lts_mpi_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_mpi_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# nips2lts-mpi
nips2lts_mpi_SOURCES  = spec2lts-mpi.c nips-greybox.c nips-greybox.h config.h
nips2lts_mpi_LDADD    = libmpi.a libutil.a $(NIPS_LIBS) $(CADP_LIBS)
nips2lts_mpi_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mpi_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS) $(CADP_LDFLAGS)

# etf2lts-mpi
etf2lts_mpi_SOURCES  = spec2lts-mpi.c etf-greybox.c etf-greybox.h config.h lts.c lts.h 
etf2lts_mpi_LDADD    = libmpi.a libutil.a $(CADP_LIBS)
etf2lts_mpi_CPPFLAGS = -DETF $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
etf2lts_mpi_LDFLAGS  = $(AM_LDFLAGS) $(CADP_LDFLAGS)

# mpi-event-test
mpi_event_test_SOURCES = mpi-event-test.c config.h
mpi_event_test_LDADD   = libmpi.a libutil.a

# ltsmin-mpi
ltsmin_mpi_SOURCES = ltsmin-mpi.c set.c lts.c dlts.c set.h lts.h	\
                     dlts.h config.h
ltsmin_mpi_LDADD   = libmpi.a libutil.a

# ltsmin-convert
ltsmin_convert_SOURCES = ltsmin-convert.c
ltsmin_convert_LDADD = libutil.a $(CADP_LIBS)
ltsmin_convert_LDFLAGS = $(CADP_LDFLAGS) $(AM_LDFLAGS)
ltsmin_convert_CPPFLAGS = $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

# libraries
libmpi_a_SOURCES = mpi_io_stream.c mpi_core.c mpi_raf.c mpi_ram_raf.c	\
		   mpi_core.h mpi_io_stream.h mpi_ram_raf.h config.h	\
		   mpi-event-loop.c mpi-event-loop.h mpi-runtime.c \
                   mpi-runtime.h

libutil_a_SOURCES := unix.c runtime.c stream.c archive.c			\
                    stream_buffer.c fast_hash.c generichash4.c		\
                    generichash8.c treedbs.c archive_format.c raf.c	\
                    stream_mem.c ghf.c archive_gcf.c scctimer.c		\
                    gzstream.c stream_diff32.c arch_object.h	\
                    generichash.h unix.h archive.h ghf.h	\
                    stream.h raf.h stream_object.h raf_object.h		\
                    scctimer.h fast_hash.h runtime.h treedbs.h		\
                    config.h greybox.c greybox.h stringindex.c		\
                    stringindex.h archive_gsf.c dir_ops.c dir_ops.h	\
                    packet_stream.h packet_stream.c	\
                    archive_dir.c chunk_support.c chunk_support.h	\
                    dynamic-array.c dynamic-array.h struct_io.c		\
                    struct_io.h etf-util.c etf-util.h lts-type.c	\
                    lts-type.h vset_buddy.c vector_set.c vector_set.h	\
                    vdom_object.h lts_io.c lts_io.h lts_io_internal.h \
                    lts_enum.c lts_enum.h lts_count.c lts_count.h \
                    archive_io_ops.c fifo.c fifo.h

libutil_a_CPPFLAGS := $(BUDDY_CPPFLAGS)


if HAVE_LIBBCG
libutil_a_SOURCES += bcg_io_ops.c
libutil_a_CPPFLAGS  += $(CADP_CPPFLAGS)
endif


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
