
cmake_minimum_required(VERSION 3.21)

add_library(bignum STATIC)

set(HAVE_GMP_H on)

target_sources(bignum
  PRIVATE
    bignum.h
)
option(HAVE_LIBGMP off)
if(HAVE_LIBGMP)
    target_sources(bignum
        PRIVATE
        bignum-gmp.c
        #add lib -lgmp
        )

option(HAVE_LIBTOMMATH off)
elseif(HAVE_LIBTOMMATH)
    target_sources(bignum
        PRIVATE
        bignum-tommath.c
        #add lib -tommath
        )
else()
    target_sources(bignum
        PRIVATE
        bignum-ll.c
        )
endif()

target_include_directories(bignum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

