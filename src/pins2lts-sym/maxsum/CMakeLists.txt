add_library(maxsum STATIC)

target_sources(maxsum
  PRIVATE
    maxsum.c
    maxsum.h
)

target_include_directories(maxsum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_BINARY_DIR}/src)

