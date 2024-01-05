
cmake_minimum_required(VERSION 3.21)

add_library(pins-lib STATIC)

set(PINSLIB_HDRS
    dlopen-api.h
    pg-types.h
    pins.h
    pins2pins-cache.h
    pins2pins-check.h
    pins2pins-fork.h
    pins2pins-group.h
    pins2pins-guards.h
    pins2pins-ltl.h
    pins2pins-mucalc.h
    pins-impl.h
    pins-util.h
    property-semantics.h
    )


target_sources(pins-lib
  PRIVATE
    pins.c
    pins2pins-cache.c
    pins2pins-check.c
    pins2pins-fork.c
    pins2pins-group.c
    pins2pins-guards.c
    pins2pins-ltl.c
    pins2pins-mucalc.c
    pins-util.c
    property-semantics.c
    ${PINSLIB_HDRS}
)

target_include_directories(pins-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

add_dependencies(pins-lib 
                    ltsmin-lib 
                    hre)

#add_subdirectory(modules)
#add_subdirectory(por)

