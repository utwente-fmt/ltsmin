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

option(with_bignum off)

configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_SOURCE_DIR}/ltl2ba/config.h)

####### cmake configuration output ######
#This will output which modules are active when executing the cmake command 
message("cmake configuration:")
message("====================")
#ASCIIDOC
message(BIGNUM = ${with_bignum})
#BUDDY
#CADP
#CC
#CXX
#LIBDDD
#MCRL
#MCRL2
#MPICC
#MPICXX
#OPAAL
#PBES
#SCOOP
#SPINS
#SPOT
#XMLTO
#BOOST
#VIENNACL
#PNML
#PROB
#SDD
#SYLVAN
message("====================")


#TODO add options for examples, tests and documentation
