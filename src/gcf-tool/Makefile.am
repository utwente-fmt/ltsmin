bin_PROGRAMS = gcf

# gcf
gcf_SOURCES  = gcf-tool.c
gcf_CPPFLAGS = -I ${top_srcdir}/src $(LIBZIP_CPPFLAGS) $(AM_CPPFLAGS)
gcf_LDFLAGS  = $(LIBZIP_LDFLAGS) $(AM_LDFLAGS)
gcf_LDADD    = ../hre/libhre.la ../libltsmin-hre.la $(LIBZIP_LIBS)
