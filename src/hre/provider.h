// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef HRE_PROVIDER_H
#define HRE_PROVIDER_H

#include <hre/user.h>

/**
\file provider.h

API for providing runtime services.
*/

typedef struct hre_runtime_s {
    struct poptOption *options;
    int selected;
    void (*start)(int *argc,char **argv[],int run_threads);
    void (*start_thread)();
} *hre_runtime_t;

/**
\brief Register a runtime implementation.
*/
extern void HREregisterRuntime(hre_runtime_t runtime);

/// Set the global context of the current thread.
extern void HREglobalSet(hre_context_t context);

/// Test if the current thread is the main thread.
extern int HREmainThread();

/// Set the main thread context.
extern void HREmainSet(hre_context_t context);

extern void *HREgetArea(hre_region_t region);

/**
\brief Get the main thread context.

The members of this context are all the main threads of the application.
*/
extern hre_context_t HREmainGet();

/// Set the process context.
extern void HREprocessSet(hre_context_t context);

/// Parse registered options.
extern int HREdoOptions(int argc,char *argv[],int min_args,int max_args,char*args[],const char* arg_help);

/// Get the name of the application.
extern const char* HREappName();

/// Get the path and name of the application.
extern const char* HREpathName();

/**
 * \brief Generic method to allocate Posix SHM.
 * This function guarantees to allocate shared memory of the
 * specified size and the address is the same for all workers.
 * Only call this function during synchronized phases of initialization and
 * execution (e.g. the loading of the model). This is to make sure that ALL
 * workers execute this in the SAME order (and thus avoiding crossed HREreduce
 * calls).
 * If this call fails, the execution is aborted.
 * This function works on Apple for multiple processes,
 * even though Posix locks on Apple are single process only.
 * @param context The HRE context in which Posix SHM is to be allocated.
 * @param size The size of the Posix SHM that is to be allocated.
 * @return The address of the allocated Posix SHM.
*/
extern void* hre_posix_shm_get(hre_context_t context, size_t size);

/**
 * \brief Generic method to allocate private memory at the same address.
 * This function guarantees to allocate private anonymous memory of the
 * specified size and the address is the same for all workers.
 * Only call this function during synchronized phases of initialization and
 * execution (e.g. the loading of the model). This is to make sure that ALL
 * workers execute this in the SAME order (and thus avoiding crossed HREreduce
 * calls).
 * If this call fails, the execution is aborted.
 * @param context The HRE context in which Posix SHM is to be allocated.
 * @param size The size of the Posix SHM that is to be allocated.
 * @return The address of the allocated Posix SHM.
*/
extern void* hre_privatefixedmem_get(hre_context_t context, size_t size);

/**
\brief Exit from the current thread.
*/
void hre_thread_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));

#endif

