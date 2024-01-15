add_library(mc-lib STATIC)

set(MCLIB_HDRS
    atomics.h
    bitvector-ll.h
    cctables.h
    clt_table.h
    dbs-ll.h
    dlopen_extra.h
    hashtable.h
    lb.h
    lmap.h
    renault-unionfind.h
    set-ll.h
    statistics.h
    stats.h
    trace.h
    treedbs-ll.h
    unionfind.h
    )


target_sources(mc-lib
  PRIVATE
    bitvector-ll.c
    cctables.c
    clt_table.c
    dbs-ll.c
    dlopen_extra.c
    hashtable.c
    lb.c
    lmap.c
    renault-unionfind.c
    set-ll.c
    statistics.c
    trace.c
    treedbs-ll.c
    unionfind.c
    ${MCLIB_HDRS}
)

target_include_directories(mc-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

