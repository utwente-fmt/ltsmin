

pkglib_LTLIBRARIES =

check_PROGRAMS  = test-bitset
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-dfs-stack
check_PROGRAMS += ltsmin-parse
check_PROGRAMS += test-dbs
check_PROGRAMS += test-vset

noinst_LTLIBRARIES  = libvset.la

if HAVE_LIBSPG
noinst_LTLIBRARIES += libspg.la
endif

SUBDIRS  = dm

SUBDIRS += bignum
SUBDIRS += hre-io
SUBDIRS += hre
if HAVE_MPI
SUBDIRS += hre-mpi
endif
SUBDIRS += hre-test
SUBDIRS += lts-io
SUBDIRS += lts-lib

SUBDIRS += util-lib
SUBDIRS += ltsmin-lib

bin_PROGRAMS = etf-convert

SUBDIRS += gcf-tool
SUBDIRS += ltsmin-printtrace
SUBDIRS += ltsmin-compare
SUBDIRS += ltsmin-convert

SUBDIRS += pins-lib
SUBDIRS += . 
if HAVE_ATOMICS
SUBDIRS += mc-lib
SUBDIRS += pins2lts-mc
SUBDIRS += pins2lts-seq
endif
SUBDIRS += pins2lts-dist
SUBDIRS += pins2lts-sym
SUBDIRS += ltsmin-reduce
if HAVE_MPI
SUBDIRS += ltsmin-reduce-dist
endif
SUBDIRS += ce
SUBDIRS += torx
SUBDIRS += spg

bin_SCRIPTS =

if HAVE_CADP
pkglib_LTLIBRARIES += libetf-open.la
if HAVE_SPINJA
pkglib_LTLIBRARIES += libprom-open.la
endif
if HAVE_LIBSTEP_INFO
pkglib_LTLIBRARIES += liblpo-open.la
endif
if HAVE_LIBMCRL2
pkglib_LTLIBRARIES += liblps-open.la
endif
if HAVE_DVE
pkglib_LTLIBRARIES += libdve-open.la
endif
if HAVE_OPAAL
pkglib_LTLIBRARIES += libopaal-open.la
endif
# pins_open
CADPCC = $(CADP)/src/com/cadp_cc
CADPINCLUDES = -I$(CADP)/incl
bin_SCRIPTS += pins_open
endif

bin_SCRIPTS += spinjal

PINS_LIB = pins-lib/libpins.la
LTSMIN_LIB = ltsmin-lib/libltsmin.la util-lib/libutil.la

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = $(LTSMIN_LIB)

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD = $(LTSMIN_LIB)

# ltsmin-parse
ltsmin_parse_SOURCES = ltsmin-parse.c
ltsmin_parse_CPPFLAGS = -DNXT -I${top_srcdir}/ltl2ba
ltsmin_parse_LDADD = $(LTSMIN_LIB)

# libetf-open.la
libetf_open_la_SOURCES = spec_open.c pins-lib/etf-pins.c pins-lib/etf-pins.h
libetf_open_la_CPPFLAGS = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD = $(PINS_LIB)

# liblpo-open.la
liblpo_open_la_SOURCES  = spec_open.c pins-lib/mcrl-pins.c pins-lib/mcrl-pins.h
liblpo_open_la_SOURCES += pins-lib/at-map.c pins-lib/at-map.h
liblpo_open_la_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblpo_open_la_LIBADD = $(PINS_LIB)

# liblps-open.la
liblps_open_la_SOURCES = spec_open.c
liblps_open_la_CPPFLAGS = -DMCRL2 $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblps_open_la_LIBADD = libmcrl2-pins.la $(PINS_LIB)
nodist_EXTRA_liblps_open_la_SOURCES = automake-force-linker.cxx

# libdve-open.la
libdve_open_la_SOURCES = spec_open.c pins-lib/dve-pins.c pins-lib/dve-pins.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve_open_la_LIBADD = $(PINS_LIB)

# libopaal-open.la
libopaal_open_la_SOURCES = spec_open.c pins-lib/opaal-pins.c pins-lib/opaal-pins.h
libopaal_open_la_CPPFLAGS = -DOPAAL $(OPAAL_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libopaal_open_la_LIBADD = $(PINS_LIB)

# libprom-open.la
libprom_open_la_SOURCES = spec_open.c pins-lib/prom-pins.c pins-lib/prom-pins.h
libprom_open_la_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libprom_open_la_LIBADD = $(PINS_LIB)


# test-is-balloc
test_is_balloc_SOURCES = test-is-balloc.c
test_is_balloc_LDADD   = $(UTIL_LIB)

# test-dfs-stack
test_dfs_stack_SOURCES = test-dfs-stack.c
test_dfs_stack_LDADD   = $(UTIL_LIB)

# test-dbs
test_dbs_SOURCES = test-dbs.c
test_dbs_LDADD   = $(MC_LIB)

# test-vset
test_vset_SOURCES = test-vset.c
test_vset_LDADD   = $(VSET_LIBS) util-lib/libutil.la


# libraries
libvset_la_SOURCES   = vector_set.c vector_set.h vdom_object.h
libvset_la_SOURCES  += vset_listdd.c
libvset_la_SOURCES  += vset_buddy.c
libvset_la_CPPFLAGS  = $(BUDDY_CPPFLAGS)
libvset_la_LDFLAGS   = $(BUDDY_LDFLAGS)
libvset_la_LIBADD    = $(BUDDY_LIBS)

if HAVE_SYLVAN
libvset_la_SOURCES  += vset_sylvan.c
libvset_la_CPPFLAGS += $(SYLVAN_CPPFLAGS) 
libvset_la_LDFLAGS  += $(SYLVAN_LDFLAGS) 
libvset_la_LIBADD   += $(SYLVAN_LIBS)
endif

if HAVE_LIBMCRL
libvset_la_SOURCES  += vset_atermdd.c
libvset_la_CPPFLAGS += $(MCRL_CPPFLAGS)
libvset_la_LDFLAGS  += $(MCRL_LDFLAGS)
libvset_la_LIBADD   += -lATerm
endif

if HAVE_LIBDDD
CCLD = $(CXXLD)
libvset_la_SOURCES  += vset_ddd.cpp
libvset_la_CPPFLAGS += $(DDD_CPPFLAGS)
libvset_la_LDFLAGS  += $(DDD_LDFLAGS)
libvset_la_LIBADD   += $(DDD_LIBS)
endif

libvset_la_CPPFLAGS += $(AM_CPPFLAGS)
libvset_la_LDFLAGS  += $(AM_LDFLAGS)

libspg_la_SOURCES  = spg-solve.c spg-solve.h
libspg_la_CPPFLAGS = -DNDEBUG $(AM_CPPFLAGS)
libspg_la_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
libspg_la_LIBADD   = libvset.la $(UTIL_LIB) bignum/libbignum.la

EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =
BUILT_SOURCES=

CLEANFILES += pins_open
DISTCLEANFILES += subst_pins_open.mk

EXTRA_DIST += pins_open.in
EXTRA_DIST += config.munge.pl subst_pins_open.mk

-include subst_pins_open.mk

pins_open: pins_open.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

subst_pins_open.mk: pins_open.in Makefile config.munge.pl
	perl $(srcdir)/config.munge.pl subst_pins_open < $< > $@

CLEANFILES += spinjal

EXTRA_DIST += spinjal.in

spinjal: spinjal.in Makefile subst_pins_open.mk
	$(subst_pins_open) < $< > $@
	chmod +x $@

EXTRA_DIST += git_version.h
DISTCLEANFILES += git_version.h.tmp
MAINTAINERCLEANFILES += git_version.h

BUILT_SOURCES += git_version.h

if HAVE_VERSION_CONTROL
GIT_DESCRIBE := $(shell cd $(top_srcdir) && git describe --tags --abbrev=6 --dirty 2>/dev/null)

git_version.h: git_version.h.tmp
	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi

git_version.h.tmp:
	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > $@
.PHONY: git_version.h.tmp
endif

# make
.DELETE_ON_ERROR:
