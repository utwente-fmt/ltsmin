
HRE_LIB = ../hre/libhre.la
PINS_LIBS  =
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += $(HRE_LIB)
MC_LIB = ../mc-lib/libmc.la
DM_LIBS = ../dm/libdm.la
DM_DEBUG_LIBS = ../dm/libdm_debug.la
VSET_LIB = ../vset-lib/libvset.la

if HAVE_BOOST
DM_LIBS += ../dm/libdm_boost.la
DM_DEBUG_LIBS += ../dm/libdm_boost.la
endif

if HAVE_VIENNACL
DM_LIBS += ../dm/libdm_viennacl.la
DM_DEBUG_LIBS += ../dm/libdm_viennacl.la
endif

check_PROGRAMS  =
check_PROGRAMS += test-bitset
check_PROGRAMS += test-dfs-stack
#check_PROGRAMS += test-dbs
check_PROGRAMS += test-dm
check_PROGRAMS += test-hre
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-ltsmin-parse
if !BUILD_OS_IS_DARWIN
check_PROGRAMS += test-mucalc-parser
endif
check_PROGRAMS += test-regroup
check_PROGRAMS += test-simplemap
check_PROGRAMS += test-treedbs
check_PROGRAMS += test-vset

# test-bitset
test_bitset_SOURCES 		= test-bitset.c
test_bitset_LDADD		 	= $(PINS_LIBS) $(AM_LDFLAGS)
test_bitset_CPPFLAGS 	    = $(AM_CPPFLAGS)

# test-is-balloc
test_is_balloc_SOURCES 		= test-is-balloc.c
test_is_balloc_LDADD   		= $(MC_LIB) $(PINS_LIBS) $(AM_LDFLAGS)
test_is_balloc_CPPFLAGS 	= $(AM_CPPFLAGS)

# test-dbs
#test_dbs_SOURCES 				= test-dbs.c
#test_dbs_LDADD   				= $(PINS_LIBS) $(MC_LIB) $(DM_LIBS) $(HRE_LIB) $(AM_LDFLAGS)
#test_dbs_CPPFLAGS 				= $(AM_CPPFLAGS)
#nodist_EXTRA_test_dbs_SOURCES	= automake-force-linker.cxx # required by Boost & ViennaCL

# test-dfs-stack
test_dfs_stack_SOURCES 		= test-dfs-stack.c
test_dfs_stack_LDADD   		= $(PINS_LIBS) $(MC_LIB) $(AM_LDFLAGS)
test_dfs_stack_CPPFLAGS 	= $(AM_CPPFLAGS)

# test-dm
test_dm_SOURCES 				= test-dm.c
test_dm_LDADD 					= $(DM_DEBUG_LIBS) $(PINS_LIBS) $(HRE_LIB) $(AM_LDFLAGS)
test_dm_CPPFLAGS 				= -DDMDEBUG $(AM_CPPFLAGS)
nodist_EXTRA_test_dm_SOURCES	= automake-force-linker.cxx # required by Boost & ViennaCL

# test-hre
test_hre_SOURCES 			= test-hre.c
test_hre_CPPFLAGS 			= $(AM_CPPFLAGS)
test_hre_LDADD 				= ../hre/libhre.la ../hre-io/libhre-io.la
test_hre_LDADD 			   += ../util-lib/libutil.la

# test-hre-mpi
if HAVE_MPI
SUBDIRS = mpicc
else
SUBDIRS = gcc
endif

# test-ltsmin-parse
test_ltsmin_parse_SOURCES 				= test-ltsmin-parse.c
test_ltsmin_parse_CPPFLAGS				= -DNXT -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)
test_ltsmin_parse_LDADD					= $(PINS_LIBS) $(DM_LIBS) $(HRE_LIB) $(AM_LDFLAGS)
test_ltsmin_parse_LDADD    			   += $(top_builddir)/ltl2ba/libltl2ba.la
nodist_EXTRA_test_ltsmin_parse_SOURCES	= automake-force-linker.cxx # required by Boost & ViennaCL

# test-mucalc-parser
test_mucalc_parser_SOURCES   = test-mucalc-parser.c
test_mucalc_parser_CPPFLAGS  = $(AM_CPPFLAGS)
test_mucalc_parser_LDADD     = $(HRE_LIB) ../hre-io/libhre-io.la ../ltsmin-lib/libmucalc.la $(AM_LDFLAGS)

# test-regroup
test_regroup_SOURCES 				= test-regroup.c
test_regroup_LDADD 					= $(DM_LIBS) $(HRE_LIB) $(PINS_LIBS) $(AM_LDFLAGS)
test_regroup_CPPFLAGS 				= $(AM_CPPFLAGS)
nodist_EXTRA_test_regroup_SOURCES	= automake-force-linker.cxx # required by Boost & ViennaCL

# test-simplemap
test_simplemap_SOURCES        = test-simplemap.c
test_simplemap_LDADD          = $(HRE_LIB) ../util-lib/libutil.la $(AM_LDFLAGS)
test_simplemap_CPPFLAGS       = $(AM_CPPFLAGS)

# test-treedbs
test_treedbs_SOURCES 		= test-treedbs.c
test_treedbs_LDADD		 	= ../hre/libhre.la ../hre-io/libhre-io.la ../util-lib/libutil.la
test_treedbs_CPPFLAGS 	    = $(AM_CPPFLAGS)

# test-vset
test_vset_SOURCES 			= test-vset.c
test_vset_LDADD   			= $(VSET_LIB) $(PINS_LIBS) $(AM_LDFLAGS)
test_vset_CPPFLAGS 			= $(AM_CPPFLAGS)
nodist_EXTRA_test_vset_SOURCES = automake-force-linker.cxx # req. by vset
