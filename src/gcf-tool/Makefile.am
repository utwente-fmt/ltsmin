bin_PROGRAMS = gcf

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# gcf
gcf_SOURCES  = gcf-tool.c
gcf_LDADD    = ../hre/libhre.la ../hre-io/libhre-io.la ../util-lib/libutil.la
gcf_LDADD   += $(top_builddir)/lib/libgnu.la

