add_subdirectory(maxsum)
############REQUIRED##################
#pins2ltsmin-sym


#set(LIBZZZ
#    libpins2lts_sym.la
#    ../pins-lib/libpins.la
#    ../vset-lib/libvset.la
#    maxsum/libmaxsum.la
#    ../spg-lib/libspg.la
#    ../mc-lib/libmc.la
#    $(top_builddir)/lib/libgnu.la)

add_library( libpins2lts-sym pins2lts-sym.c )

target_sources(libpins2lts-sym INTERFACE
            alg/auxiliary.c alg/auxiliary.h
            alg/bfs.c alg/bfs.h
            alg/chain.c alg/chain.h
            alg/pg.c alg/pg.h
            alg/mu.c alg/mu.h
            alg/reach.c alg/reach.h
            alg/sat.c alg/sat.h
            auxiliary/output.c auxiliary/output.h
            auxiliary/prop.c auxiliary/prop.h)
target_link_libraries(libpins2lts-sym PUBLIC
                       pins-lib
                       ltsmin-lib
                       hre
                       vset-lib
                       util-lib
                       lts-io
                       dm
                       maxsum
                       mc-lib 
                       spg-lib
                       )
                       
target_include_directories(libpins2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                            ${CMAKE_CURRENT_SOURCE_DIR}/alg)


################### EXECUTABLES:  pins2lts-sym####################
add_executable(pins2lts-sym
    auxiliary/options.c auxiliary/options.h
    ../pins-lib/modules/dlopen-pins.c ../pins-lib/modules/dlopen-pins.h
)
target_link_libraries(pins2lts-sym PUBLIC
                       libpins2lts-sym
                       hre
                       )
target_include_directories(pins2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                            ${CMAKE_CURRENT_SOURCE_DIR}/alg)
target_compile_options(pins2lts-sym 
    PRIVATE -DPINS_DLL
) 
################### EXECUTABLES:  eft2lts-sym####################
add_executable(eft2lts-sym
    auxiliary/options.c
    ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
)
target_link_libraries(eft2lts-sym PRIVATE
                       libpins2lts-sym
                       )
target_include_directories(eft2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                            ${CMAKE_CURRENT_SOURCE_DIR}/alg)

#################Required NOT included yet###################3

#eft: eft2lts-sym


#################Optional NOT included yet################
#libstep: lpo2lts-sym
#libmcrl2: lps2lts-sym
#scoop: mapa2lts-sym
#libpbes: pbes2lts-sym
#DVE: dve2lts-sym
#SPINS: prom2lts-sym
#PNML: pnml2lts-sym
#PROB: prob2lts-sym

