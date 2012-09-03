
HRE_LIB = ../hre/libhre.la
PINS_LIBS  =
PINS_LIBS += ../pins-lib/libpins.la
PINS_LIBS += ../ltsmin-lib/libltsmin.la
PINS_LIBS += ../hre-io/libhre-io.la
PINS_LIBS += ../lts-io/liblts-io.la
PINS_LIBS += $(HRE_LIB)
MC_LIB = ../mc-lib/libmc.la
DM_LIB = ../dm/libdm.la
VSET_LIB = ../vset-lib/libvset.la

check_PROGRAMS  =
check_PROGRAMS += test-bitset
check_PROGRAMS += test-dfs-stack
check_PROGRAMS += test-dbs
check_PROGRAMS += test-dm
check_PROGRAMS += test-hre
if HAVE_MPI
check_PROGRAMS += test-mpi-hre
endif
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-ltsmin-parse
check_PROGRAMS += test-regroup
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
test_dbs_SOURCES 			= test-dbs.c
test_dbs_LDADD   			= $(PINS_LIBS) $(MC_LIB) $(DM_LIB) $(AM_LDFLAGS)
test_dbs_CPPFLAGS 			= $(AM_CPPFLAGS)

# test-dfs-stack
test_dfs_stack_SOURCES 		= test-dfs-stack.c
test_dfs_stack_LDADD   		= $(PINS_LIBS) $(MC_LIB) $(AM_LDFLAGS)
test_dfs_stack_CPPFLAGS 	= $(AM_CPPFLAGS)

# test-dm
test_dm_SOURCES 			= test-dm.c
test_dm_LDADD 				= ../dm/libdm_debug.la $(AM_LDFLAGS)
test_dm_CPPFLAGS 			= -DDMDEBUG $(AM_CPPFLAGS)

# test-hre
test_hre_SOURCES 			= test-hre.c
test_hre_CPPFLAGS 			= $(AM_CPPFLAGS)
test_hre_LDADD 				= ../hre/libhre.la ../hre-io/libhre-io.la
test_hre_LDADD 			   += ../util-lib/libutil.la

# test-hre-mpi
test_mpi_hre_SOURCES 		= test-hre.c
test_mpi_hre_CPPFLAGS 		= -DTESTMPI $(AM_CPPFLAGS)
test_mpi_hre_LDADD 		    =
test_mpi_hre_LDADD         += ../hre-io/libhre-io.la
test_mpi_hre_LDADD  	   += ../hre-mpi/libhre.la
test_mpi_hre_LDADD 		   += ../util-lib/libutil.la

# test-ltsmin-parse
test_ltsmin_parse_SOURCES 	= test-ltsmin-parse.c
test_ltsmin_parse_CPPFLAGS  = -DNXT -I${top_srcdir}/ltl2ba $(AM_CPPFLAGS)
test_ltsmin_parse_LDADD		= $(PINS_LIBS) $(DM_LIB) $(AM_LDFLAGS)
test_ltsmin_parse_LDADD    += $(top_builddir)/ltl2ba/libltl2ba.la

# test-regroup
test_regroup_SOURCES 		= test-regroup.c
test_regroup_LDADD 			= ../dm/libdm.la $(AM_LDFLAGS)
test_regroup_CPPFLAGS 		= $(AM_CPPFLAGS)

# test-vset
test_vset_SOURCES 			= test-vset.c
test_vset_LDADD   			= $(VSET_LIB) $(PINS_LIBS) $(AM_LDFLAGS)
test_vset_CPPFLAGS 			= $(AM_CPPFLAGS)
nodist_EXTRA_test_vset_SOURCES = automake-force-linker.cxx # req. by vset
