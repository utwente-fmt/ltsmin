noinst_LTLIBRARIES    = liblts-io.la

# liblts-io
liblts_io_la_SOURCES  = user.h internal.h provider.h
liblts_io_la_SOURCES += aut_io.c
liblts_io_la_SOURCES += bcg_io.c
liblts_io_la_SOURCES += file-base.c
liblts_io_la_SOURCES += filter_write.c
liblts_io_la_SOURCES += fsm_write.c
liblts_io_la_SOURCES += lts_file_misc.c
liblts_io_la_SOURCES += lts-type-io.c
liblts_io_la_SOURCES += vector_read.c
liblts_io_la_SOURCES += vector_write.c
liblts_io_la_LIBADD   = $(CADP_LIBS)
liblts_io_la_LDFLAGS  = $(CADP_LDFLAGS)
liblts_io_la_CPPFLAGS = $(CADP_CPPFLAGS) $(AM_CPPFLAGS)

