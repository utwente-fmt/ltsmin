add_library(bignum STATIC)

set(HAVE_GMP_H on)

target_sources(bignum
  PRIVATE
    bignum.h
)
option(have_libgmp "does the project have libgmp?" off) #TODO implement actually checking for libgmp
if(${HAVE_LIBGMP})
    target_sources(bignum
        PRIVATE
        bignum-gmp.c
        #add lib -lgmp
        )

option(HAVE_LIBTOMMATH "does the project have tommath?" off)#TODO implement actually checking for tommath 
elseif(${HAVE_LIBTOMMATH})
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
    target_link_libraries(bignum
        general-lib) # maybe this link needs to be outside the if statement
endif()


target_include_directories(bignum PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

