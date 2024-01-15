add_library(por-lib STATIC)

set(PORLIB_HDRS
    pins2pins-por.h
    pins2pins-por-check.h
    por-ample.h
    por-beam.h
    por-deletion.h
    por-internal.h
    por-leap.h
    por-lipton.h
    por-tr.h
)

target_sources(por-lib
    PRIVATE
    pins2pins-por.c
    pins2pins-por-check.c
    por-ample.c
    por-beam.c
    por-deletion.c
    por-leap.c
    por-lipton.c
    por-tr.c
    ${PORLIB_HDRS}
)

target_include_directories(por-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_BINARY_DIR}/src)
