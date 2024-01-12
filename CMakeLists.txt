cmake_minimum_required(VERSION 3.21)

project(ltsmin 
    VERSION 3.0.3
    DESCRIPTION "TODO"
    HOMEPAGE_URL "https://github.com/utwente-fmt/ltsmin"
    LANGUAGES C CXX
)

Set(CMAKE_CXX_STANDARD 11)
Set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add CMake modules path | TODO don't think this is necessary
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

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


#Dependencies
#TODO get sylvan(?) via FetchContent? or make it a submodule

add_subdirectory(src)
add_subdirectory(lib)
add_subdirectory(lemon)
add_subdirectory(ltl2ba)

configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_SOURCE_DIR}/ltl2ba/config.h)



#TODO add options for examples, tests and documentation
