
add_library(vset-lib STATIC)
add_dependencies(vset-lib util-lib)

set(VSET_HDRS
    vdom_object.h
    vector.h
    vector_set.h
    )

target_sources(vset-lib PRIVATE
    vector.c
    vector_set.c
    vset_listdd.c
    vset_listdd64.c
    ${VSET_HDRS}
)


if(${have_buddy})
    target_sources(vset-lib PRIVATE
                    vset_buddy.c)
endif()


if(${have_sylvan})
    target_sources(vset-lib PRIVATE
                    vset_sylvan.c
                    vset_lddmc.c)
endif()


if(${have_libmcrl})
    target_sources(vset-lib PRIVATE
                    vset_atermdd.c)
endif()


if(${have_sdd})
    target_sources(vset-lib PRIVATE
                    sdd_utils.c     sdd_utils.h
                    vset_sdd.c    vset_sdd.h
                    vset_sdd_utils.c    vset_sdd_utils.h
                    vtree_utils.c   vtree_utils.h)
endif()

if(${have_libddd})
    target_sources(vset-lib PRIVATE
                        vset_ddd.cpp)
endif()

target_link_libraries(vset-lib PUBLIC
    util-lib
    hre-io
    bignum)

target_include_directories(vset-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src/hre)
