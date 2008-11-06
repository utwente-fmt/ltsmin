bin_PROGRAMS = ltsmin-convert gsf-extract gsf-build gsf2dir dir2gsf
noinst_LIBRARIES = libutil.a

if HAVE_MPI
bin_PROGRAMS += ltsmin-mpi
noinst_LIBRARIES += libmpi.a
if HAVE_LIBMCRL
bin_PROGRAMS += lpo2lts-mpi
endif
endif
if HAVE_LIBSTEP_INFO
bin_PROGRAMS +=  lpo2lts-grey
endif

if HAVE_LIBBCG
bin_PROGRAMS += bcg2gsf gsf2bcg
endif

# dir2gsf
dir2gsf_SOURCES = ar2gsf.c
dir2gsf_LDADD = libutil.a
dir2gsf_CPPFLAGS = -DINPUTTYPE=dir $(AM_CPPFLAGS)

# gsf-extract
gsf_extract_SOURCES = gsf-extract.c
gsf_extract_LDADD = libutil.a

# gsf-build
gsf_build_SOURCES = gsf-build.c
gsf_build_LDADD = libutil.a

# bcg2gsf
bcg2gsf_SOURCES = bcg2gsf.c
bcg2gsf_LDADD = libutil.a $(CADP_LIBS)
bcg2gsf_LDFLAGS = $(CADP_LDFLAGS) $(AM_LDFLAGS)
bcg2gsf_CPPFLAGS = $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

# gsf2bcg
gsf2bcg_SOURCES = gsf2any.c
gsf2bcg_LDADD = libutil.a $(CADP_LIBS)
gsf2bcg_LDFLAGS = $(CADP_LDFLAGS) $(AM_LDFLAGS)
gsf2bcg_CPPFLAGS = -DOUTPUT_BCG $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

# gsf2dir
gsf2dir_SOURCES = gsf2any.c
gsf2dir_LDADD = libutil.a
gsf2dir_CPPFLAGS = -DOUTPUT_DIR $(AM_CPPFLAGS)

# lpo2lts-grey
lpo2lts_grey_SOURCES  = spec2lts-grey.c mcrl-greybox.c mcrl-greybox.h at-map.c at-map.h 
lpo2lts_grey_LDADD    = libutil.a $(MCRL_LIBS)
lpo2lts_grey_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_grey_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

# lpo2lts-mpi
lpo2lts_mpi_SOURCES  = spec2lts-mpi.c mcrl-greybox.c mcrl-greybox.h at-map.c at-map.h config.h
lpo2lts_mpi_LDADD    = libmpi.a libutil.a $(MCRL_LIBS)
lpo2lts_mpi_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(AM_CPPFLAGS)
lpo2lts_mpi_LDFLAGS  = $(MCRL_LDFLAGS) $(AM_LDFLAGS)

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
	           mpi_core.h mpi_io_stream.h mpi_ram_raf.h config.h

libutil_a_SOURCES = unix.c runtime.c stream.c archive.c	\
                    stream_buffer.c fast_hash.c generichash4.c		\
                    generichash8.c treedbs.c archive_format.c raf.c	\
                    stream_mem.c ghf.c archive_gcf.c scctimer.c		\
                    gzstream.c stream_diff32.c options.c arch_object.h	\
                    generichash.h unix.h archive.h ghf.h options.h	\
                    stream.h raf.h stream_object.h		\
                    raf_object.h scctimer.h fast_hash.h runtime.h	\
                    treedbs.h config.h greybox.h stringindex.c          \
                    stringindex.h archive_gsf.c dirops.c dirops.h       \
                    ltsman.c ltsman.h packet_stream.h packet_stream.c \
                    archive_dir.c chunk-table.h \
                    dynamic-array.c  dynamic-array.h


# make 
.DELETE_ON_ERROR:

