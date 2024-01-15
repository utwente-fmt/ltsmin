#test-bitset.c
add_executable(bitset_test test-bitset.c)
target_link_libraries(bitset_test 
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre)
add_test(NAME bitset_test
         COMMAND bitset_test)
         
         
#test-dbs.c
add_executable(dbs_test test-dbs.c)
target_link_libraries(dbs_test 
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME dbs_test
         COMMAND dbs_test)
         
#test-dfs-stack.c
add_executable(dfs-stack_test test-dfs-stack.c)
target_link_libraries(dfs-stack_test 
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME dfs-stack_test
         COMMAND dfs-stack_test)

#test-dm.c
add_executable(dm_test test-dm.c)
target_link_libraries(dm_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME dm_test
         COMMAND dm_test)

#test-hre.c
add_executable(hre_test test-hre.c)
target_link_libraries(hre_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME hre_test
         COMMAND hre_test)

#test-is-balloc.c
add_executable(is-balloc_test test-is-balloc.c)
target_link_libraries(is-balloc_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME is-balloc_test
         COMMAND is-balloc_test)

# test-ltsmin-parse.c
# Disabled because it requires a flag (comment from original automake code)

#test-mucalc-parser.c
add_executable(mucalc-parser_test test-mucalc-parser.c)
target_link_libraries(mucalc-parser_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME mucalc-parser_test
         COMMAND mucalc-parser_test)

#test-regroup.c
# Disabled because it requires a read from stdin (comment from original automake code)

#test-simplemap.c
add_executable(simplemap_test test-simplemap.c)
target_link_libraries(simplemap_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME simplemap_test
         COMMAND simplemap_test)

#test-treedbs.c
add_executable(treedbs_test test-treedbs.c)
target_link_libraries(treedbs_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm)
add_test(NAME treedbs_test
         COMMAND treedbs_test)

#test-vset.c
add_executable(vset_test test-vset.c)
target_link_libraries(vset_test
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre
                mc-lib
                dm
                vset-lib)
add_test(NAME vset_test
         COMMAND vset_test)


