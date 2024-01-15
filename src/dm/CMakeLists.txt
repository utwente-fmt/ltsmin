add_library(dm STATIC)

set(DM_HDRS
    bitvector.h
    dm.h
    sloan_ordering.hpp
    )
    
target_sources(dm
  PRIVATE
    bitvector.c
    dm.c
    ${DM_HDRS}
)

target_link_libraries(dm general-lib)

OPTION(HAVE_BOOST off)
OPTION(HAVE_VIENNACL off)
if (HAVE_BOOST)
    target_sources(dm PRIVATE
        dm_boost.cpp dm_boost.h)
endif()
if (HAVE_VIENNACL)
    target_sources(dm PRIVATE
        dm_viennacl.cpp dm_viennacl.h)
endif()

# Include directories
target_include_directories(dm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)


