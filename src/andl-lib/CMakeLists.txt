
FIND_PACKAGE(FLEX REQUIRED)

FLEX_TARGET(ANDLLEXER andl-lexer.l ${CMAKE_CURRENT_BINARY_DIR}/andl-lexer.c
           DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/andl-lexer.h) 
           
find_package(BISON REQUIRED)

# Hack around FindBISON's incorrect use of separate_arguments https://stackoverflow.com/questions/71369325/cmake-incorrectly-escapes-bison-target-option
if (CMAKE_VERSION VERSION_LESS 3.24)
  function(patch_flags variable access value ip stack)
    set(invalid "${CMAKE_CURRENT_BINARY_DIR}")
    separate_arguments(invalid)
    string(REPLACE "${invalid}" "${CMAKE_CURRENT_BINARY_DIR}" "${variable}" "${value}")
    set("${variable}" "${${variable}}" PARENT_SCOPE)
  endfunction()
  variable_watch(BISON_TARGET_cmdopt patch_flags)
  
  BISON_TARGET(ANDLPARSER "andl-parser.y" "${CMAKE_CURRENT_BINARY_DIR}/libandl_la-andl-parser.c"
        COMPILE_FLAGS "--defines=${CMAKE_CURRENT_BINARY_DIR}/libandl_la-andl-parser.h")
else()
  BISON_TARGET(ANDLPARSER "andl-parser.y" "${CMAKE_CURRENT_BINARY_DIR}/libandl_la-andl-parser.c"
        DEFINE_FILE "${CMAKE_CURRENT_BINARY_DIR}/libandl_la-andl-parser.h")
endif ()



ADD_FLEX_BISON_DEPENDENCY(ANDLLEXER ANDLPARSER)


add_library(andl-lib STATIC 
    andl-parser.y
    ${FLEX_ANDLLEXER_OUTPUTS}
    ${BISON_ANDLPARSER_OUTPUTS}
    )


target_link_libraries(andl-lib ${FLEX_LIBRARIES} ${BISON_LIBRARIES})

target_include_directories(andl-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_BINARY_DIR}/..                     
                            ${CMAKE_SOURCE_DIR}/ltl2ba
                            ${CMAKE_SOURCE_DIR}/src)



