add_library(ce STATIC)

set(CE_HDRS
    bufs.h
    Ddlts.h
    Dtaudlts.h
    groups.h
    paint.h
    sortcount.h)
    
target_sources(ce
  PRIVATE
    bufs.c
    Ddlts.c
    ce-mpi.c
    Dtaudlts.c
    groups.c
    paint.c
    sortcount.c
    ${CE_HDRS}
)

target_include_directories(ce PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hre)
