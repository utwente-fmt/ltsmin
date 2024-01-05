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

add_executable( pins2lts-sym pins2lts-sym.c )
target_link_libraries(pins2lts-sym PRIVATE
                       pins-lib
                       ltsmin-lib
                       hre
                       vset-lib
                       util-lib
                       )
#mc-lib?
#lts-io
#dm  
                       
target_include_directories(pins2lts-sym PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_SOURCE_DIR}/..
                            ${CMAKE_CURRENT_BINARY_DIR}/..)


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

