bin_PROGRAMS = ltstrans

# ltstrans
ltstrans_SOURCES   = ltstrans.c
ltstrans_LDADD     = ../lts-lib/liblts-lib.la ../lts-io/liblts-io.la
ltstrans_LDADD    += ../hre/libhre.la ../libltsmin-hre.la
ltstrans_CPPFLAGS  = -I ${top_srcdir}/src $(AM_CPPFLAGS)
ltstrans_LDFLAGS   = $(AM_LDFLAGS)
