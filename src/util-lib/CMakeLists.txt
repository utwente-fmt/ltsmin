add_library(util-lib STATIC)
add_dependencies(util-lib hre)

target_include_directories(util-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

set(UTILLIB_HDRS
    balloc.h
    bitmultiset.h
    bitset.h
    chunk_support.h
    chunk_table_factory.h
    dfs-stack.h
    dynamic-array.h
    fast_hash.h
    fast_set.h
    is-balloc.h
    MurmurHash3.h
    rationals.h
    simplemap.h
    string-map.h
    tables.h
    treedbs.h
    util.h
    zobrist.h
)

target_sources(util-lib
  PRIVATE
    balloc.c
    bitmultiset.c
    bitset.c
    chunk_support.c
    chunk_table_factory.c
    dfs-stack.c
    dynamic-array.c
    fast_hash.c
    fast_set.c
    is-balloc.c
    MurmurHash3.c
    rationals.c
    simplemap.c
    string-map.c
    tables.c
    treedbs.c
    util.c
    zobrist.c
  ${UTILLIB_HDRS}
  )


