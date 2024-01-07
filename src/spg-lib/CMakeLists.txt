cmake_minimum_required(VERSION 3.21)

add_library(spg-lib STATIC)

set(SPGLIB_HDRS
    spg.h
    spg-attr.h
    spg-options.h
    spg-solve.h
    spg-strategy.h
    )


target_sources(spg-lib
  PRIVATE
    spg.c
    spg-attr.c
    spg-options.c
    spg-solve.c
    spg-strategy.c
    ${SPGLIB_HDRS}
)

target_include_directories(spg-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)


