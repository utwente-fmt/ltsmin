
DM_LIB = ../dm/libdm.la
HRE_LIB = ../hre/libhre.la
UTIL_LIB = ../util-lib/libutil.la $(HRE_LIB)
MC_LIB = ../mc-lib/libmc.la $(UTIL_LIB)
LTSMIN_LIB = ../ltsmin-lib/libltsmin.la $(UTIL_LIB)
PINS_LIB = ../pins-lib/libpins.la $(LTSMIN_LIB)
VSET_LIB = ../vset-lib/libvset.la

check_PROGRAMS  = test-bitset
check_PROGRAMS += test-dfs-stack
check_PROGRAMS += test-dbs
check_PROGRAMS += test-dm
check_PROGRAMS += test-is-balloc
check_PROGRAMS += test-ltsmin-parse
check_PROGRAMS += test-regroup
check_PROGRAMS += test-vset

# test-bitset
test_bitset_SOURCES = test-bitset.c
test_bitset_LDADD   = $(LTSMIN_LIB)

# test-ltsmin-parse
test_ltsmin_parse_SOURCES     = test-ltsmin-parse.c
test_ltsmin_parse_CPPFLAGS    = -DNXT -I${top_srcdir}/ltl2ba
test_ltsmin_parse_LDADD       = $(PINS_LIB)

# test-is-balloc
test_is_balloc_SOURCES = test-is-balloc.c
test_is_balloc_LDADD   = $(MC_LIB)

# test-dfs-stack
test_dfs_stack_SOURCES = test-dfs-stack.c
test_dfs_stack_LDADD   = $(MC_LIB)

# test-dbs
test_dbs_SOURCES = test-dbs.c
test_dbs_LDADD   = $(MC_LIB) $(DM_LIB)

# test-vset
test_vset_SOURCES = test-vset.c
test_vset_LDADD   = $(VSET_LIB) $(UTIL_LIB)

# test-regroup
test_regroup_SOURCES = test-regroup.c
test_regroup_LDADD = ../dm/libdm.la
test_regroup_CFLAGS = -I ${top_srcdir}/src

# test-dm
test_dm_SOURCES = test-dm.c
test_dm_LDADD = ../dm/libdm_debug.la
test_dm_CFLAGS = -I ${top_srcdir}/src -DDMDEBUG

