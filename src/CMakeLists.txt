# LIB DEPENDENCIES (transitive depends on relations)
#
# vset-lib  ---->  bignum-lib
#    |
#    |
#    v
#   HRE   <------>   HRE-IO
#    ^
#    |
#    v
# util-lib
#    ^
#    |
#    |
# ltsmin-lib  <--->  lts-io  <----  lts-lib 
#    ^   ^
#    |    \
#    |     \
#    |      \
#    |      mc-lib
#    |            \
#    |             v
# pins-lib  ---->  dm-lib
#    
#

cmake_minimum_required(VERSION 3.21)

Option(HAVE_CE OFF)
if (HAVE_CE)
    add_subdirectory(ce)
endif()
add_subdirectory(hre)
add_subdirectory(pins-lib)
add_subdirectory(ltsmin-lib)
add_subdirectory(pins2lts-sym)
add_subdirectory(vset-lib)
add_subdirectory(util-lib)
add_subdirectory(lts-io)
add_subdirectory(dm)
add_subdirectory(mc-lib)
add_subdirectory(spg-lib)
