noinst_LTLIBRARIES = libhre.la

# export to install folder
pkginclude_HEADERS = feedback.h

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

# for dlopen, etc.
AM_CPPFLAGS += $(LTDLINCL)

# libhre
libhre_la_SOURCES  = user.h internal.h provider.h
libhre_la_SOURCES += hre_context.c context.h
libhre_la_SOURCES += hre_dir_ops.c dir_ops.h
libhre_la_SOURCES += hre_feedback.c feedback.h
libhre_la_SOURCES += hre_main.c
libhre_la_SOURCES += hre_malloc.c
libhre_la_SOURCES += hre_messaging.c
libhre_la_SOURCES += hre_popt.c
libhre_la_SOURCES += hre_pthread.c
libhre_la_SOURCES += hre_queue.c queue.h
libhre_la_SOURCES += hre_table.c table.h
libhre_la_SOURCES += hre_reduce.c
libhre_la_SOURCES += hre_runtime.c runtime.h
libhre_la_SOURCES += hre_timer.c
libhre_la_SOURCES += unix.h
libhre_la_SOURCES += hre_utils.c
libhre_la_SOURCES += stringindex.c stringindex.h

libhre_la_CFLAGS   = $(AM_CFLAGS) $(POPT_CFLAGS) $(PTHREAD_CFLAGS)

##libhre_la_LIBADD   = ../hre-io/libhre-io.la
libhre_la_LIBADD   = ../util-lib/libutil.la
libhre_la_LIBADD  += $(POPT_LIBS)
libhre_la_LIBADD  += $(top_builddir)/lib/libgnu.la
libhre_la_LIBADD  += $(PTHREAD_LIBS)

# for dlopen, etc.
libhre_la_LIBADD  += $(LIBLTDL)

# git_version.h
EXTRA_DIST = git_version.h
DISTCLEANFILES = git_version.h.tmp
MAINTAINERCLEANFILES = git_version.h

BUILT_SOURCES = git_version.h

if HAVE_VERSION_CONTROL
GIT_DESCRIBE := $(shell cd $(top_srcdir) && git describe --tags --abbrev=6 --dirty 2>/dev/null)

git_version.h: git_version.h.tmp
	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

git_version.h.tmp:
	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > $@
.PHONY: git_version.h.tmp
endif
