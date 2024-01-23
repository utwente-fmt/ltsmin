add_library(lts-io STATIC)

set(LTSIO_HDRS
    internal.h
    provider.h
    user.h
    )
    
target_sources(lts-io
  PRIVATE
    aut_io.c
    bcg_io.c
    file-base.c
    filter_write.c
    fsm_write.c
    lts_file_misc.c
    lts-type-io.c
    vector_read.c
    vector_write.c
    ${LTSIO_HDRS}
)

target_link_libraries(lts-io hre-io util-lib hre)

# Include directories
target_include_directories(lts-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)



