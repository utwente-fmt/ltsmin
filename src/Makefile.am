bin_PROGRAMS = ltsmin-convert gcf
noinst_LIBRARIES = libutil.a

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi mpi-event-test
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
bin_PROGRAMS += nips2lts-grey nips-reach
# endif
endif

# gcf
gcf_SOURCES = gcf-rw.c
gcf_LDADD = libutil.a

# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h    \
                        at-map.c at-map.h vector_set.c vector_set.h
lpo2lts_grey_LDADD    = libutil.a $(MCRL_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-grey
lps2lts_grey_SOURCES  = spec2lts-grey.c mcrl2-greybox.cpp		\
                        mcrl2-greybox.h at-map.c at-map.h vector_set.c	\
                        vector_set.h
lps2lts_grey_LDADD    = libutil.a $(MCRL2_LIBS)
lps2lts_grey_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_grey_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)

# nips2lts-grey
nips2lts_grey_SOURCES  = spec2lts-grey.c nips-greybox.c nips-greybox.h   \
                         vector_set.c vector_set.h
nips2lts_grey_LDADD    = libutil.a -lATerm $(NIPS_LIBS)
nips2lts_grey_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_grey_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lpo-reach
lpo_reach_SOURCES  = spec-reach.c mcrl-greybox.c mcrl-greybox.h  \
                     at-map.c at-map.h vector_set.c vector_set.h
lpo_reach_LDADD    = libutil.a $(MCRL_LIBS)
lpo_reach_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo_reach_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps-reach
lps_reach_SOURCES  = spec-reach.c mcrl2-greybox.cpp mcrl2-greybox.h      \
                     at-map.c at-map.h vector_set.c vector_set.h
lps_reach_LDADD    = libutil.a $(MCRL2_LIBS)
lps_reach_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps_reach_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)

# nips-reach
nips_reach_SOURCES  = spec-reach.c nips-greybox.c nips-greybox.h	\
                      vector_set.c vector_set.h
nips_reach_LDADD    = libutil.a -lATerm $(NIPS_LIBS)
nips_reach_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips_reach_LDFLAGS  = $(NIPS_LDFLAGS) $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h	\
                       at-map.c at-map.h config.h
lpo2lts_mpi_LDADD    = libmpi.a libutil.a $(MCRL_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lps2lts-mpi
lps2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl2-greybox.cpp mcrl2-greybox.h	\
                       at-map.c at-map.h config.h
lps2lts_mpi_LDADD    = libmpi.a libutil.a $(MCRL2_LIBS)
lps2lts_mpi_CPPFLAGS = -DMCRL2 $(MCRL2_CPPFLAGS) $(AM_CPPFLAGS)
lps2lts_mpi_LDFLAGS  = $(MCRL2_LDFLAGS) $(AM_LDFLAGS)

# nips2lts-mpi
nips2lts_mpi_SOURCES  = spec2lts-mpi.c nips-greybox.c nips-greybox.h config.h
nips2lts_mpi_LDADD    = libmpi.a libutil.a $(NIPS_LIBS)
nips2lts_mpi_CPPFLAGS = -DNIPS $(NIPS_CPPFLAGS) $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
nips2lts_mpi_LDFLAGS  = $(NIPS_LDFLAGS) $(AM_LDFLAGS)

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
		   mpi-event-loop.c mpi-event-loop.h

libutil_a_SOURCES = unix.c runtime.c stream.c archive.c			\
                    stream_buffer.c fast_hash.c generichash4.c		\
                    generichash8.c treedbs.c archive_format.c raf.c	\
                    stream_mem.c ghf.c archive_gcf.c scctimer.c		\
                    gzstream.c stream_diff32.c options.c arch_object.h	\
                    generichash.h unix.h archive.h ghf.h options.h	\
                    stream.h raf.h stream_object.h raf_object.h		\
                    scctimer.h fast_hash.h runtime.h treedbs.h		\
                    config.h greybox.c greybox.h stringindex.c		\
                    stringindex.h archive_gsf.c dirops.c dirops.h	\
                    ltsman.c ltsman.h packet_stream.h packet_stream.c	\
                    archive_dir.c chunk_support.c chunk_support.h	\
                    dynamic-array.c dynamic-array.h


options.c: git_version.h

GIT_DESCRIBE := $(shell git describe --tags --abbrev=6 HEAD 2>/dev/null)

git_version.h.tmp:
	echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > git_version.h.tmp

.PHONY: git_version.h.tmp

git_version.h: git_version.h.tmp
	if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

# make
.DELETE_ON_ERROR:
