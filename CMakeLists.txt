cmake_minimum_required(VERSION 3.21)

project(ltsmin 
    VERSION 3.0.3
    DESCRIPTION "TODO"
    HOMEPAGE_URL "https://github.com/utwente-fmt/ltsmin"
    LANGUAGES C CXX
)

# Add CMake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/")

#Dependencies
#TODO get sylvan(?), ltl2ba, scoop, spins via FetchContent

#TODO add_subdirectory(src)
#TODO LATER add_subdirectory(spins)
#TODO LATER add_subdirectory(scoop)
add_subdirectory(lemon)
#TODO LATER add_subdirectory(ltl2ba)
#TODO add_subdirectory(lib)


#TODO add options for examples, tests and documentation
