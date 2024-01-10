
cmake_minimum_required(VERSION 3.10)

add_library(hre-io STATIC)
    
target_sources(hre-io
  PRIVATE
    user.h types.h
    hre_archive.c archive.h arch_object.h
    hre_archive_dir.c
    hre_client_server.c client-server.h
    hre_fifo.c fifo.h
    hre_gcf_common.c gcf_common.h
    hre_gcf_read.c
    hre_gcf_write.c
    hre_gzstream.c
    hre_raf.c raf.h raf_object.h
    hre_stream.c stream.h stream_object.h
    hre_stream_buffer.c
    hre_stream_diff32.c
    hre_stream_fd.c
    hre_stream_mem.c
    hre_stream_rle32.c
    hre_struct_io.c struct_io.h
    hre_zip_io.c
)

# hre uses zlib
find_package(ZLIB REQUIRED)
target_link_libraries(hre-io ZLIB::ZLIB util-lib)

# Include directories
target_include_directories(hre-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

