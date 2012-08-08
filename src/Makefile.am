bin_PROGRAMS = etf-convert

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

noinst_LTLIBRARIES += libltsmin.la

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
SUBDIRS += .
SUBDIRS += gcf-tool
SUBDIRS += ltsmin-printtrace
SUBDIRS += ltsmin-compare
SUBDIRS += ltsmin-convert
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


EXTRA_DIST  = ltsmin-lexer.c ltsmin-lexer.h

BUILT_SOURCES  = ltsmin-grammar.c ltsmin-grammar.h
BUILT_SOURCES += ltsmin-lexer.c ltsmin-lexer.h


if HAVE_LIBMCRL2
noinst_LTLIBRARIES += libmcrl2-pins.la
endif

if HAVE_LIBPBES
noinst_LTLIBRARIES += libpbes-pins.la
endif

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

libmcrl2_pins_la_SOURCES  = mcrl2-greybox.cpp mcrl2-greybox.h
libmcrl2_pins_la_CPPFLAGS = -DMCRL2 -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libmcrl2_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libmcrl2_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)

if HAVE_LIBPBES
libpbes_pins_la_SOURCES  = pbes-greybox.cpp pbes-greybox.h
libpbes_pins_la_CPPFLAGS = -DPBES -DNDEBUG $(MCRL2_PINS_CPPFLAGS) $(AM_CPPFLAGS)
libpbes_pins_la_LDFLAGS  = $(MCRL2_PINS_LDFLAGS) $(MCRL2_LDFLAGS) $(AM_LDFLAGS)
libpbes_pins_la_LIBADD   = $(MCRL2_PINS_LIBS)
endif

# etf-convert
etf_convert_SOURCES = etf-convert.c
etf_convert_LDADD = libltsmin.la

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD = libltsmin.la

# ltsmin-parse
ltsmin_parse_SOURCES = ltsmin-parse.c
ltsmin_parse_CPPFLAGS = -DNXT -I${top_srcdir}/ltl2ba
ltsmin_parse_LDADD = libltsmin.la

# libetf-open.la
libetf_open_la_SOURCES = spec_open.c etf-greybox.c etf-greybox.h
libetf_open_la_CPPFLAGS = -DETF $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libetf_open_la_LIBADD = libltsmin.la

# liblpo-open.la
liblpo_open_la_SOURCES  = spec_open.c mcrl-greybox.c mcrl-greybox.h
liblpo_open_la_SOURCES += at-map.c at-map.h
liblpo_open_la_CPPFLAGS = -DMCRL $(MCRL_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblpo_open_la_LIBADD = libltsmin.la

# liblps-open.la
liblps_open_la_SOURCES = spec_open.c
liblps_open_la_CPPFLAGS = -DMCRL2 $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
liblps_open_la_LIBADD = libmcrl2-pins.la libltsmin.la
nodist_EXTRA_liblps_open_la_SOURCES = automake-force-linker.cxx

# libdve-open.la
libdve_open_la_SOURCES = spec_open.c dve-greybox.c dve-greybox.h
libdve_open_la_CPPFLAGS = -DDIVINE $(DIVINE_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libdve_open_la_LIBADD = libltsmin.la

# libopaal-open.la
libopaal_open_la_SOURCES = spec_open.c opaal-greybox.c opaal-greybox.h
libopaal_open_la_CPPFLAGS = -DOPAAL $(OPAAL_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libopaal_open_la_LIBADD = libltsmin.la

# libprom-open.la
libprom_open_la_SOURCES = spec_open.c prom-greybox.c prom-greybox.h
libprom_open_la_CPPFLAGS = -DSPINJA $(SPINJA_CPPFLAGS) $(CADP_CPPFLAGS) $(AM_CPPFLAGS)
libprom_open_la_LIBADD = libltsmin.la


# test-is-balloc
test_is_balloc_SOURCES = test-is-balloc.c
test_is_balloc_LDADD   = libltsmin.la

# test-dfs-stack
test_dfs_stack_SOURCES = test-dfs-stack.c
test_dfs_stack_LDADD   = libltsmin.la

# test-dbs
test_dbs_SOURCES = test-dbs.c
test_dbs_LDADD   = libltsmin.la

# test-vset
test_vset_SOURCES = test-vset.c
test_vset_LDADD   = $(VSET_LIBS) libltsmin.la


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


# ltsmin-hre settings
libltsmin_la_SOURCES  = balloc.c balloc.h
libltsmin_la_SOURCES += bitset.c bitset.h
libltsmin_la_SOURCES += chunk_support.c chunk_support.h
libltsmin_la_SOURCES += chunk_table_factory.c
libltsmin_la_SOURCES += config.h
libltsmin_la_SOURCES += dynamic-array.c dynamic-array.h
libltsmin_la_SOURCES += etf-internal.h
libltsmin_la_SOURCES += etf-objects.c etf-objects.h
libltsmin_la_SOURCES += etf-parser.c
libltsmin_la_SOURCES += etf-util.c etf-util.h
libltsmin_la_SOURCES += fast_hash.c fast_hash.h
libltsmin_la_SOURCES += greybox.c greybox.h
libltsmin_la_SOURCES += group.c
libltsmin_la_SOURCES += qsortex.c
libltsmin_la_SOURCES += ltl2ba-lex.c
libltsmin_la_SOURCES += lts-type.c lts-type.h
libltsmin_la_SOURCES += ltsmin-buchi.h
libltsmin_la_SOURCES += ltsmin-grammar.lemon
libltsmin_la_SOURCES += ltsmin-lexer.l
libltsmin_la_SOURCES += ltsmin-parse-env.h
libltsmin_la_SOURCES += ltsmin-syntax.c ltsmin-syntax.h
libltsmin_la_SOURCES += ltsmin-tl.c ltsmin-tl.h
libltsmin_la_SOURCES += pins-cache.c
libltsmin_la_SOURCES += pins-ltl.c
libltsmin_la_SOURCES += pins-por.c
libltsmin_la_SOURCES += ltsmin-grammar.c ltsmin-grammar.h
libltsmin_la_SOURCES += ltsmin-lexer.c ltsmin-lexer.h
libltsmin_la_SOURCES += string-map.c string-map.h
libltsmin_la_SOURCES += stringindex.c stringindex.h
libltsmin_la_SOURCES += tables.c tables.h
libltsmin_la_SOURCES += treedbs.c treedbs.h
libltsmin_la_SOURCES += unix.h

libltsmin_la_LIBADD   = hre/libhre.la
libltsmin_la_LIBADD  += hre-io/libhre-io.la 
libltsmin_la_LIBADD  += lts-io/liblts-io.la
libltsmin_la_LIBADD  += dm/libdm.la
libltsmin_la_LIBADD  += bignum/libbignum.la
libltsmin_la_LIBADD  += $(top_builddir)/ltl2ba/libltl2ba.la

libltsmin_la_CPPFLAGS  = -DNXT -DLTSMIN_HRE
libltsmin_la_CPPFLAGS += -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)


libspg_la_SOURCES  = spg-solve.c spg-solve.h
libspg_la_CPPFLAGS = -DNDEBUG $(AM_CPPFLAGS)
libspg_la_LDFLAGS  = $(AM_LDFLAGS) $(PROFILER_LDFLAGS)
libspg_la_LIBADD   = libvset.la libltsmin.la bignum/libbignum.la

CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =


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

CLEANFILES += ltsmin-grammar.c ltsmin-grammar.h
CLEANFILES += ltsmin-grammar.out
MAINTAINERCLEANFILES += ltsmin-tokens.c ltsmin-tokens.h

LEMON_FLAGS = -s

%.c %.h: %.lemon lempar.c
	cd $(top_builddir)/lemon && $(MAKE) $(AM_MAKEFLAGS) lemon$(EXEEXT)
	$(top_builddir)/lemon/lemon$(EXEEXT) $(LEMON_FLAGS) $<
	@if test -f $@; then touch $@; fi

%.c %.h: %.l
	$(am__skiplex) $(LEX) $<


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
