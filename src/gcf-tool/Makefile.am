bin_PROGRAMS = gcf

# gcf
gcf_SOURCES  = gcf-tool.c
gcf_CPPFLAGS = -I ${top_srcdir}/src $(AM_CPPFLAGS)
gcf_LDFLAGS  = $(AM_LDFLAGS)
gcf_LDADD    = ../hre/libhre.la ../libltsmin-hre.la

if HAVE_LIBZIP
gcf_LDADD += -lzip
endif
