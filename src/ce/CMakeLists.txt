#TODO. this is not finished 
add_library(ce STATIC)

set(CE_HDRS
    bufs.h
    #Ddlts.h #can't find mpi.h
    #Dtaudlts.h #can't find mpi.h
    #groups.h #can't find mpi.h (Linked with Dtaudlts)
    paint.h
    sortcount.h)
    
target_sources(ce
  PRIVATE
    #bufs.c #can't find mpi.h
    #Ddlts.c #can't find mpi.h
    #ce-mpi.c #can't find mpi.h
    #Dtaudlts.c #can't find mpi.h
    #roups.c #can't find mpi.h (Linked with Dtaudlts)
    #paint.c ##can't find mpi.h (Linked with Dtaudlts)
    sortcount.c
    ${CE_HDRS}
)

target_include_directories(ce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            "${CMAKE_CURRENT_SOURCE_DIR}/..")


