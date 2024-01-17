add_subdirectory(maxsum)
################### LIBRARY:  pins2lts-sym####################
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
                       general-lib
                       )
target_include_directories(pins2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                            ${CMAKE_CURRENT_SOURCE_DIR}/alg)
                            
target_compile_options(pins2lts-sym 
    PRIVATE -DPINS_DLL -I${CMAKE_SOURCE_DIR}/lib -I${CMAKE_BINARY_DIR}/lib #include of lib might not be necessary
    )
install(TARGETS pins2lts-sym)  

################### EXECUTABLES:  eft2lts-sym####################
add_executable(etf2lts-sym
    auxiliary/options.c
    ../pins-lib/modules/etf-pins.c ../pins-lib/modules/etf-pins.h
)
target_link_libraries(etf2lts-sym PRIVATE
                       libpins2lts-sym
                       hre
                       general-lib
                       )
target_include_directories(etf2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..
                            ${CMAKE_SOURCE_DIR}/src
                            ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                            ${CMAKE_CURRENT_SOURCE_DIR}/alg)
target_compile_options(etf2lts-sym
    PRIVATE -DETF -I${CMAKE_SOURCE_DIR}/lib -I${CMAKE_BINARY_DIR}/lib)
install(TARGETS etf2lts-sym) 


################### OPTIONAL EXECUTABLES:  pnml2lts-sym####################
set(HAVE_PNML ON) #TODO remove this when pnml check is implemented
if (HAVE_PNML)
    add_executable(pnml2lts-sym
        auxiliary/options.c
        ../pins-lib/modules/pnml-pins.c ../pins-lib/modules/pnml-pins.h
    )
    find_package(LibXml2 REQUIRED)
    target_link_libraries(pnml2lts-sym PRIVATE
                           libpins2lts-sym
                           hre
                           general-lib
                           andl-lib
                           LibXml2::LibXml2
                           )
    target_include_directories(pnml2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                ${CMAKE_CURRENT_SOURCE_DIR}/..
                                ${CMAKE_CURRENT_BINARY_DIR}/..
                                ${CMAKE_SOURCE_DIR}/src
                                ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                                ${CMAKE_CURRENT_SOURCE_DIR}/alg)
    target_compile_options(pnml2lts-sym
        PRIVATE -DPNML -I${CMAKE_SOURCE_DIR}/lib -I${CMAKE_BINARY_DIR}/lib #include of lib might not be necessary
        ) 
     install(TARGETS pnml2lts-sym)
endif()


################### OPTIONAL EXECUTABLES:  dve2lts-sym####################
set(HAVE_DVE ON) #TODO remove this when pnml check is implemented
if (HAVE_DVE)
    add_executable(dve2lts-sym
        auxiliary/options.c
        ../pins-lib/modules/dve-pins.c ../pins-lib/modules/dve-pins.h
    )
    find_package(LibXml2 REQUIRED)
    target_link_libraries(dve2lts-sym PRIVATE
                           libpins2lts-sym
                           hre
                           general-lib
                           )
    target_include_directories(dve2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                ${CMAKE_CURRENT_SOURCE_DIR}/..
                                ${CMAKE_CURRENT_BINARY_DIR}/..
                                ${CMAKE_SOURCE_DIR}/src
                                ${CMAKE_CURRENT_SOURCE_DIR}/auxiliary
                                ${CMAKE_CURRENT_SOURCE_DIR}/alg)
    target_compile_options(dve2lts-sym
        PRIVATE -DDIVINE -I${CMAKE_SOURCE_DIR}/lib -I${CMAKE_BINARY_DIR}/lib #include of lib might not be necessary
        ) 
    install(TARGETS dve2lts-sym)
endif()

#################Optional executables NOT included yet################
#libstep: lpo2lts-sym
#libmcrl2: lps2lts-sym
#scoop: mapa2lts-sym
#libpbes: pbes2lts-sym
#SPINS: prom2lts-sym

#PROB: prob2lts-sym

