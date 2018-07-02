noinst_LTLIBRARIES = libhre-io.la

# libhre-io
libhre_io_la_SOURCES  = user.h types.h
libhre_io_la_SOURCES += hre_archive.c archive.h arch_object.h
libhre_io_la_SOURCES += hre_archive_dir.c
libhre_io_la_SOURCES += hre_client_server.c client-server.h
libhre_io_la_SOURCES += hre_fifo.c fifo.h
libhre_io_la_SOURCES += hre_gcf_common.c gcf_common.h
libhre_io_la_SOURCES += hre_gcf_read.c
libhre_io_la_SOURCES += hre_gcf_write.c
libhre_io_la_SOURCES += hre_gzstream.c
libhre_io_la_SOURCES += hre_raf.c raf.h raf_object.h
libhre_io_la_SOURCES += hre_stream.c stream.h stream_object.h
libhre_io_la_SOURCES += hre_stream_buffer.c
libhre_io_la_SOURCES += hre_stream_diff32.c
libhre_io_la_SOURCES += hre_stream_fd.c
libhre_io_la_SOURCES += hre_stream_mem.c
libhre_io_la_SOURCES += hre_stream_rle32.c
libhre_io_la_SOURCES += hre_struct_io.c struct_io.h
libhre_io_la_SOURCES += hre_zip_io.c

libhre_io_la_CFLAGS   = $(ZLIB_CFLAGS) $(AM_CFLAGS)
libhre_io_la_LIBADD   = $(ZLIB_LIBS)

