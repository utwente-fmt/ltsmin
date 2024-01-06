


#Generate files with lemon
set(LEMONFILES ltsmin-grammar mucalc-grammar)
foreach(LEMONFILE ${LEMONFILES})
    add_custom_command(
        OUTPUT ${LEMONFILE}.h ${LEMONFILE}.c 
        COMMAND ${CMAKE_BINARY_DIR}/lemon/lemon${BUILD_EXEEXT} ${LEMON_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/${LEMONFILE}.lemon
        COMMENT "Generating ltsmin-grammer files"
        #WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS lemon
    )    
endforeach()

#Generate files with flex
    #OLD AUTOMAKE CODE FOR generating with lex:
    #%.c %.h: %.l
    #	$(am__skiplex) $(LEX) $<
#TODO it looks like it is optional in the old automake code. Look into this

set(FLEXFILES ltsmin-lexer mucalc-lexer) 
FIND_PACKAGE(FLEX REQUIRED)
foreach(FLEXFILE ${FLEXFILES})
    FLEX_TARGET(LTSMINLEXER ${FLEXFILE}.l ${CMAKE_CURRENT_BINARY_DIR}/${FLEXFILE}.c
                 DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/${FLEXFILE}.h)
endforeach()



add_library(ltsmin-lib STATIC )
set(LTSMINLIB_HDRS
    etf-internal.h
    etf-objects.h
    etf-util.h
    ltl2ba-lex.h
    ltl2ba-lex-helper.h
    ltl2spot.h
    ltsmin-buchi.h
    ltsmin-parse-env.h
    ltsmin-grammar.h
    ltsmin-standard.h
    ltsmin-syntax.h
    ltsmin-tl.h
    ltsmin-type-system.h
    lts-type.h
    mucalc-grammar.h
    mucalc-parse-env.h
    mucalc-syntax.h

)
    
target_sources(ltsmin-lib
    PRIVATE
    etf-objects.c
    etf-parser.c
    etf-util.c
    ltl2ba-lex.c
    ltl2ba-lex-helper.c
    ltsmin-grammar.c
    ltsmin-syntax.c
    ltsmin-tl.c
    ltsmin-type-system.c
    lts-type.c
    mucalc-grammar.c
    mucalc-syntax.c
    #ltl2spot.cpp
    ${LTSMINLIB_HDRS}
    ${FLEX_LTSMINLEXER_OUTPUTS}
)

target_link_libraries(ltsmin-lib ${FLEX_LIBRARIES})




target_include_directories(ltsmin-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            ${CMAKE_CURRENT_BINARY_DIR}/..                       
                            ${CMAKE_SOURCE_DIR}/ltl2ba
                            ${CMAKE_SOURCE_DIR}/src)



