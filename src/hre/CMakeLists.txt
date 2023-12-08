
cmake_minimum_required(VERSION 3.10)

add_library(hre STATIC)

set(HRE_HDRS
    context.h
    dir_ops.h
    feedback.h
    internal.h
    provider.h
    queue.h
    runtime.h
    stringindex.h
    table.h
    unix.h
    user.h
    git_version.h #WHERE DOES THIS COME FROM? generated from configure.ac: Comes from some git_version.h.tmp. Is generated with SEE BELOW:
#if HAVE_VERSION_CONTROL
#GIT_DESCRIBE := $(shell cd $(top_srcdir) && git describe --tags --abbrev=6 --dirty 2>/dev/null)
#
#git_version.h: git_version.h.tmp
#	@if cmp $< $@ 2>/dev/null; then $(RM) $<; else mv $< $@; fi
#
#git_version.h.tmp:
#	@echo "#define GIT_VERSION \"$(GIT_DESCRIBE)\"" > $@
#.PHONY: git_version.h.tmp
#endif
    )
    
target_sources(hre
  PRIVATE
    hre_context.c
    hre_dir_ops.c
    hre_feedback.c
    hre_main.c
    #hre_malloc.c #Might not be necessary, automake makes without this file ALSO COMPILEERROR
    hre_messaging.c
    hre_popt.c
    hre_pthread.c
    hre_queue.c
    hre_reduce.c
    #hre_runtime.c #See root/Conversion-to-cmake.txt for more info. (under src/hre)
    hre_table.c
    hre_timer.c
    hre_utils.c
    stringindex.c
    ${HRE_HDRS}
)

configure_file(${PROJECT_SOURCE_DIR}/src/hre/config.h.in ${PROJECT_SOURCE_DIR}/src/hre/config.h)


# Include directories
target_include_directories(hre PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                            "${CMAKE_CURRENT_SOURCE_DIR}/.."
                            ${CMAKE_CURRENT_BINARY_DIR}/..)

