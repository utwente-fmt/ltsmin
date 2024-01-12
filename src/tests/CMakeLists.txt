

#add_test(NAME TestName COMMAND ExecutableToRun arg1 arg2 ...)


#add_executable(TestInstantiator TestInstantiator.cxx)
#target_link_libraries(TestInstantiator vtkCommon)
#add_test(NAME TestInstantiator
#         COMMAND TestInstantiator)

#add_test(test_bitset COMMAND test-bitset${EXEEXT})

add_executable(TestInstantiator test-bitset.c)
target_link_libraries(TestInstantiator 
                pins-lib
                ltsmin-lib
                hre-io
                lts-io
                hre)
add_test(NAME TestInstantiator
         COMMAND TestInstantiator)
