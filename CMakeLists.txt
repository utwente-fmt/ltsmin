cmake_minimum_required(VERSION 3.21)



project(ltsmin 
    VERSION 3.0.3
    DESCRIPTION "TODO"
    HOMEPAGE_URL "https://github.com/utwente-fmt/ltsmin"
    LANGUAGES C CXX
)

Set(CMAKE_CXX_STANDARD 11)
Set(CMAKE_CXX_STANDARD_REQUIRED True)
set(PACKAGE_STRING "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")
set(PACKAGE_BUGREPORT "ltsmin-support@lists.utwente.nl")
include(CTest)
set(CMAKE_MODULE_PATH cmake_modules/)

# update submodules:
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ltl2ba/CMakeLists.txt" OR 
   NOT EXISTS "${PROJECT_SOURCE_DIR}/scoop/Makefile.am" OR
   NOT EXISTS "${PROJECT_SOURCE_DIR}/spins/spins.sh")
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()


#TODO get sylvan(?) via FetchContent? or make it a submodule
add_subdirectory(src)
add_subdirectory(lib)
add_subdirectory(lemon)
add_subdirectory(ltl2ba)




###### CONFIGURATION ######

#check OS https://gitlab.kitware.com/cmake/community/-/wikis/doc/tutorials/How-To-Write-Platform-Checks
    if (WIN32)
        set(_WIN32)
    elseif (UNIX)
        set(__linux__ ON)
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(_APPLE_ ON)
    else ()
        message(FATAL_ERROR "OS not accounted for in this program")
    endif()



configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_SOURCE_DIR}/ltl2ba/config.h)

####### cmake configuration output ######
#This will output which modules are active when executing the cmake command 
message("cmake configuration:")
message("====================")
###ASCIIDOC
    #TODO check if this actually finds asciidoc
    find_program(ASCIIDOC asciidoc)
    string(COMPARE NOTEQUAL ${ASCIIDOC} "ASCIIDOC-NOTFOUND" have_asciidoc)
    if(${have_asciidoc})
        message("ASCIIDOC = ON")
    else()
        message("ASCIIDOC = NOT FOUND. Asciidoc is needed for building documentation.")
    endif()
###BIGNUM
    #TODO: implement some kind of check here
    option(with_bignum "variable to disable bignum" ON)
    message("BIGNUM = ${with_bignum}")
###BUDDY
###CADP
###CC
###CXX
###LIBDDD
###MCRL
###MCRL2
###MPICC
###MPICXX
###OPAAL
###PBES
###SCOOP
###SPINS
###SPOT

     
    option(without-spot "variable to disable spot" ON) #TODO when find_package(SPOT) works. Then change option default value to OFF
    message(${without-spot})
    if(${without-spot})
        set(have_spot OFF)
    else()
        #TODO: Implement a FindSPOT.cmake in CMAKE_MODULE_PATH.
        message("TODO: Implement a FindSPOT.cmake in CMAKE_MODULE_PATH.")
        find_package(SPOT)
        #TODO: have_spot should depend if the package was found or not
        set(have_spot ON)
    endif()
    
    if(${have_spot})
        message("SPOT = ON. (override with -Dwithout-spot)")
    else()
        message("SPOT = OFF. Spot not found, or disabled")
    endif()
###XMLTO
###BOOST
###VIENNACL
###PNML
###PROB
###SDD
###SYLVAN

    option(without-sylvan "variable to disable sylvan" OFF)
    if(${without-sylvan})
        set(SYLVAN_FOUND OFF)
    else()
        find_package(LACE)
        find_package(SYLVAN)
    endif()
    
    if(${SYLVAN_FOUND})
        set(have_sylvan ON)
        message("SYLVAN = ON. (override with -Dwithout-sylvan)")
    else()
        message("SYLVAN = OFF. SYLVAN not found, or disabled")
    endif()
message("====================")


#TODO add options for examples, tests and documentation
