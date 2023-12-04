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
    user.h)
    
target_sources(hre
  PRIVATE
    hre_context.c
    hre_dir_ops.c
    hre_feedback.c
    hre_main.c
    hre_malloc.c
    hre_messaging.c
    hre_popt.c
    hre_pthread.c
    hre_queue.c
    hre_reduce.c
    hre_runtime.c
    hre_table.c
    hre_timer.c
    hre_utils.c
    stringindex.c
    ${HRE_HDRS}
)

