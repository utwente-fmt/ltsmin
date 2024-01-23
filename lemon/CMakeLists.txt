cmake_minimum_required(VERSION 3.12)


add_executable(lemon lemon.c)

#lemon needs lempar.c to generate .lemons files. lempar.c is a templatefile and should not be compiled, just copied to the binaries
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/lempar.c
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
