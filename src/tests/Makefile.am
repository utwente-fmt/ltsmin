
HRE_LIB = ../hre/libhre.la

PINS_LIBS  =
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += $(HRE_LIB)

MC_LIB = ../mc-lib/libmc.la

DM_LIBS         = ../dm/libdm.la
DM_DEBUG_LIBS   = ../dm/libdm_debug.la
if HAVE_BOOST
DM_LIBS        += ../dm/libdm_boost.la
DM_DEBUG_LIBS  += ../dm/libdm_boost.la
endif

if HAVE_VIENNACL
DM_LIBS        += ../dm/libdm_viennacl.la
DM_DEBUG_LIBS  += ../dm/libdm_viennacl.la
endif

VSET_LIB = ../vset-lib/libvset.la

# for gnulib
AM_CPPFLAGS += -I$(top_srcdir)/lib -I$(top_builddir)/lib

check_PROGRAMS  =
check_PROGRAMS += test-bitset$(EXEEXT)
check_PROGRAMS += test-dfs-stack$(EXEEXT)
check_PROGRAMS += test-dbs$(EXEEXT)
check_PROGRAMS += test-dm$(EXEEXT)
check_PROGRAMS += test-hre$(EXEEXT)
check_PROGRAMS += test-is-balloc$(EXEEXT)
check_PROGRAMS += test-ltsmin-parse$(EXEEXT)
check_PROGRAMS += test-mucalc-parser$(EXEEXT)
check_PROGRAMS += test-regroup$(EXEEXT)
check_PROGRAMS += test-simplemap$(EXEEXT)
check_PROGRAMS += test-treedbs$(EXEEXT)
check_PROGRAMS += test-vset$(EXEEXT)

TESTS  =
TESTS += test-bitset$(EXEEXT)
TESTS += test-dfs-stack$(EXEEXT)
TESTS += test-dbs$(EXEEXT)
TESTS += test-dm$(EXEEXT)
TESTS += test-hre$(EXEEXT)
TESTS += test-is-balloc$(EXEEXT)

EXTRA_DIST = wine-wrapper.sh.in

# disables because it requires a flag
#TESTS += test-ltsmin-parse$(EXEEXT)

# disabled because it requires a read from stdin
#TESTS += test-regroup$(EXEEXT)

TESTS += test-mucalc-parser$(EXEEXT)
TESTS += test-simplemap$(EXEEXT)
TESTS += test-treedbs$(EXEEXT)
TESTS += test-vset$(EXEEXT)

# test-bitset
test_bitset_SOURCES     = test-bitset.c
test_bitset_LDADD       = $(PINS_LIBS) $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la

# test-is-balloc
test_is_balloc_SOURCES  = test-is-balloc.c
test_is_balloc_LDADD    = $(MC_LIB) $(PINS_LIBS) $(AM_LDFLAGS)
test_is_balloc_LDADD   += $(top_builddir)/lib/libgnu.la

# test-dbs
test_dbs_SOURCES                = test-dbs.c
test_dbs_LDADD                  = $(PINS_LIBS) $(MC_LIB) $(DM_LIBS) $(HRE_LIB)
test_dbs_LDADD                 += $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la
nodist_EXTRA_test_dbs_SOURCES   = automake-force-linker.cxx # required by Boost & ViennaCL

# test-dfs-stack
test_dfs_stack_SOURCES  = test-dfs-stack.c
test_dfs_stack_LDADD    = $(PINS_LIBS) $(MC_LIB) $(AM_LDFLAGS)
test_dfs_stack_LDADD   += $(top_builddir)/lib/libgnu.la

# test-dm
test_dm_SOURCES                 = test-dm.c
test_dm_LDADD                   = $(DM_DEBUG_LIBS) $(PINS_LIBS) $(HRE_LIB)
test_dm_LDADD                  += $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la
test_dm_CPPFLAGS                = -DDMDEBUG $(AM_CPPFLAGS)
nodist_EXTRA_test_dm_SOURCES    = automake-force-linker.cxx # required by Boost & ViennaCL

# test-hre
test_hre_SOURCES    = test-hre.c
test_hre_LDADD      = ../hre/libhre.la ../hre-io/libhre-io.la
test_hre_LDADD     += ../util-lib/libutil.la $(top_builddir)/lib/libgnu.la

# test-hre-mpi
if HAVE_MPI
SUBDIRS = mpicc
else
SUBDIRS = gcc
endif

# test-ltsmin-parse
test_ltsmin_parse_SOURCES               = test-ltsmin-parse.c
test_ltsmin_parse_CPPFLAGS              = -DNXT -I${top_srcdir}/ltl2ba
test_ltsmin_parse_CPPFLAGS             += $(AM_CPPFLAGS)
test_ltsmin_parse_LDADD                 = $(PINS_LIBS) $(DM_LIBS)
test_ltsmin_parse_LDADD                += $(HRE_LIB) $(AM_LDFLAGS)
test_ltsmin_parse_LDADD                += $(top_builddir)/ltl2ba/libltl2ba.la
test_ltsmin_parse_LDADD                += $(top_builddir)/lib/libgnu.la
nodist_EXTRA_test_ltsmin_parse_SOURCES  = automake-force-linker.cxx # required by Boost & ViennaCL

# test-mucalc-parser
test_mucalc_parser_SOURCES      = test-mucalc-parser.c
test_mucalc_parser_LDADD        = $(HRE_LIB) ../hre-io/libhre-io.la
test_mucalc_parser_LDADD       += ../ltsmin-lib/libmucalc.la $(AM_LDFLAGS)
test_mucalc_parser_LDADD       += $(top_builddir)/lib/libgnu.la

# test-regroup
test_regroup_SOURCES               = test-regroup.c
test_regroup_LDADD                 = $(DM_LIBS) $(HRE_LIB) $(PINS_LIBS)
test_regroup_LDADD                += $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la
nodist_EXTRA_test_regroup_SOURCES  = automake-force-linker.cxx # required by Boost & ViennaCL

# test-simplemap
test_simplemap_SOURCES  = test-simplemap.c
test_simplemap_LDADD    = $(HRE_LIB) ../util-lib/libutil.la
test_simplemap_LDADD   += $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la

# test-treedbs
test_treedbs_SOURCES    = test-treedbs.c
test_treedbs_LDADD      = ../hre/libhre.la ../hre-io/libhre-io.la
test_treedbs_LDADD     += ../util-lib/libutil.la $(top_builddir)/lib/libgnu.la

# test-vset
test_vset_SOURCES               = test-vset.c
test_vset_LDADD                 = $(VSET_LIB) $(PINS_LIBS)
test_vset_LDADD                += $(AM_LDFLAGS) $(top_builddir)/lib/libgnu.la
if HAVE_SYLVAN
test_vset_LDADD                += $(SYLVAN_LIBS)
test_vset_CFLAGS                = $(AM_CFLAGS) $(SYLVAN_CFLAGS)
endif
nodist_EXTRA_test_vset_SOURCES  = automake-force-linker.cxx # req. by vset

.PHONY: wine-wrapper

check: wine-wrapper
if WINE_MISSING
	$(error To run tests you need to install Wine, and re-run configure.)
else
endif

DISTCLEANFILES = wine-wrapper.sh

wine-wrapper:
	chmod +x wine-wrapper.sh
if NEEDS_WINE
	export WINEPREFIX=$(top_srcdir)/wine
	echo "Starting persistent wineserver..."
	$(WINESERVER) -p || echo "wineserver already running"
endif

check-hook:
if NEEDS_WINE
	echo "killing wineserver..."
	$(WINESERVER) -k || echo "wineserver not running"
endif

if NEEDS_WINE
	AM_TESTS_ENVIRONMENT = ./wine-wrapper.sh
endif

