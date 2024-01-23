
add_library(general-lib STATIC write.c)

target_sources(general-lib
    PUBLIC
    verify.h)


find_library(MATH_LIBRARY m REQUIRED)
if(MATH_LIBRARY)
    target_link_libraries(general-lib PUBLIC ${MATH_LIBRARY})
endif()


target_include_directories(general-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_SOURCE_DIR}/src/hre)
add_dependencies(general-lib hre)
