// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef HRE_USER_H
#define HRE_USER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <popt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef RUNTIME_H
#error "runtime defined: inconsistent include sequence!"
#else
#define RUNTIME_H
#endif

#define EXPECT_FALSE(e) __builtin_expect(e, 0)
#define EXPECT_TRUE(e) __builtin_expect(e, 1)

/**
Opaque type memory region.
*/
typedef struct hre_region_s *hre_region_t;

/**
Opaque type for runtime context.
*/
typedef struct hre_context_s *hre_context_t;

#include <hre/feedback.h>
#include <hre/context.h>
#include <hre/queue.h>
#include <hre/table.h>
#include <hre/runtime.h>

/**
\file user.h
\brief Hybrid Runtime Environment (HRE)

The hybrid runtime environment provides support for
running parallel applications on top of
a variety of shared memory and message passing architectures.
These architectures include
- Posix threads
- MPI
*/


/**
\brief Begin of the HRE initialization sequence.
*/
extern void HREinitBegin(const char*app_name);

/**
\brief Enable Posix threads.

The argument sets the default number of threads used.

Good choices for a default number of threads are 1 and RTnumCPUs().

Thread support can be disabled by calling this function with 0 as argument.
*/
extern void HREenableThreads(int threads, bool selected);

/**
\brief Enable the multi process runtime.

The argument sets the default number of processes used when this runtime is selected.
*/
extern void HREenableFork(int procs, bool selected);

/**
\brief Disable the single process runtime.

Some application may require a specific runtime. For example,
MPI applications would crash if the single process single thread
runtime is used.
*/
extern void HREdisableSingle();

/**
\brief Enable all of the standard run times.
*/
extern void HREenableStandard();

/**
\brief Macro to enable all available runtimes.
*/
#ifndef HREenableAll
#define HREenableAll HREenableStandard
#endif

/**
\brief End of the HRE initialization sequence.

This call will start the selected runtime. During the startup,
the registered options will be parsed. It is up to the selected
runtime exactly when options are parsed.
*/
extern void HREinitStart(int *argc,char **argv[],int min_args,int max_args,char*args[],const char* arg_help);

/**
\brief Start the HRE environment.

This function start all processes and all threads, specified
on the command line.
*/
extern void HREinit(int *argc,char **argv[]);

/**
\brief Assertion check, with or without print arguments
*/
#ifdef NDEBUG
#define HREassert(check,...)    ((void) (check));
#else
#ifdef LTSMIN_DEBUG
#define PRINT_STACK HREprintStack();
#else
#define PRINT_STACK ((void)0);
#endif
#define HREassert(e,...) \
    if (EXPECT_FALSE(!(e))) {\
        char buf[4096];\
        if (#__VA_ARGS__[0])\
            snprintf(buf, 4096, ": " __VA_ARGS__);\
        else\
            buf[0] = '\0';\
        Print(assertion, "assertion \"%s\" failed%s", #e, buf);\
        PRINT_STACK\
        HREabort(HRE_EXIT_FAILURE);\
    }
#endif

/**
\brief Assertion check only for debugging.
*/
#ifdef LTSMIN_DEBUG
#define HRE_ASSERT              HREassert
#else
#define HRE_ASSERT(check,...)   ((void)0);
#endif

/**
\brief Emergency shutdown (async).
*/
extern void HREabort(int code) __attribute__ ((noreturn));

/**
\brief Perform a graceful shutdown (collaborative).
*/
extern void HREexit(int code) __attribute__ ((noreturn));

/**
\brief Print usage info and perform a graceful shutdown (collaborative).
*/
extern void HREexitUsage(int code);

/**
Set the label of this thread.
*/
extern void set_label(const char* fmt,...)
                      __attribute__ ((format (printf, 1, 2)));

/**
Get the label of this thread.
*/
extern char* get_label();

/**
\brief Get the application path (argv[0])
*/
extern char* HREgetApplication();

/**
\brief Require the use of MPI in this program.
*/
extern void HRErequireMPI();

/**
\brief Initialize without process management.
*/
extern void HREinitBare(int *argc,char **argv[]);

/**
\brief Return the bottom of the stack.

The current implementation returns the bottom of the stack of the main thread,
regardsless of which thread calls this function.
 */
extern void* HREstackBottom();


/** \defgroup hre_popt Option parsing for HRE

The sequence of events when using popt options is:
 -# Use the HREaddOptions function to add the options for every subsytem.
 -# Call HREparseOptions to parse the options. If there is a maximum number of argument then option parsing is finished.
 -# If the number of arguments is unlimited then the function
   HREnextOption() has to be called te retrieve those options.
   When there are no option left this function cleans up and returns NULL
*/
/*@{*/

/**
\brief Register options to be parsed.
*/
extern void HREaddOptions(const struct poptOption *options,const char* header);

/**
\brief Initialize the runtime library using popt.
\param argc Number of arguments to be parsed.
\param argv Arguments to be parsed.
\param min_args The minimum number of arguments allowed.
\param max_args The maximum number of arguments allowed, where -1 denotes infinite.
\param args Array of length min(min_args,max_args) in which arguments are returned.
*/
extern void HREparseOptions(
    int argc,char*argv[],
    int min_args,int max_args,char*args[],
    const char* arg_help
);

/**
\brief Return the next argument.

If an unlimited amount of arguments is allowed then the args
array cannot hold them all and this functions serves as an iterator.
*/
extern char* HREnextArg();

/**
\brief Print usage during argument fase of option parsing.
*/
extern void HREprintUsage();

/**
\brief Print help during argument fase of option parsing.
*/
extern void HREprintHelp();

/*}@*/

/** \defgroup hre_malloc Process architecture aware memory allocation.
*/
/*@{*/


/**
Allocater for the process heap.
*/
extern hre_region_t hre_heap;

/**
Allocate memory in a region.
*/
extern void* HREmalloc(hre_region_t region,size_t size);

/**
Allocate memory in a region aligned.
*/
extern void* HREalign(hre_region_t region,size_t align, size_t size);

/**
Allocate memory in a region aligned.
*/
extern void* HREalignZero(hre_region_t region,size_t align, size_t size);

/**
Allocate and fill with zeros.
*/
extern void* HREmallocZero(hre_region_t region,size_t size);

/**
Free memory.
*/
extern void HREfree(hre_region_t region,void* mem);

/**
Free memory that has been allocated with HREalign, or HREalignZero.
*/
extern void HREalignedFree(hre_region_t region,void* mem);

/**
Macro that allocates room for a new object given the type.
*/
#define HRE_NEW(region,sort) ((sort*)HREmallocZero(region,sizeof(sort)))

/**
Reallocate memory.
 */
extern void* HRErealloc(hre_region_t region,void* mem,size_t size);

typedef void*(*hre_malloc_t)(void* area,size_t size);
typedef void*(*hre_align_t)(void* area,size_t align, size_t size);
typedef void*(*hre_realloc_t)(void* area,void*mem,size_t size);
typedef void(*hre_free_t)(void* area,void*mem);

/**
Create a new allocater given malloc, align, realloc and free methods.
*/

extern hre_region_t HREcreateRegion(void* area,hre_malloc_t malloc,hre_align_t align,hre_realloc_t realloc,hre_free_t free,hre_free_t aligned_free);

extern hre_region_t RTgetMallocRegion();

extern void RTsetMallocRegion(hre_region_t r);

extern hre_region_t HREdefaultRegion(hre_context_t context);

extern size_t HREgetRegionSize(hre_region_t region);

typedef size_t hre_key_t;

extern void  HREcreateLocal(hre_key_t *key, void (*destructor)(void *));

extern void  HREsetLocal(hre_key_t key, void *package);

extern void* HREgetLocal(hre_key_t key);

extern void* RTmalloc(size_t size);

extern void* RTmallocZero(size_t size);

extern void* RTalign(size_t align, size_t size);

extern void* RTalignZero(size_t align, size_t size);

extern void* RTrealloc(void *rt_ptr, size_t size);

extern void RTfree(void *rt_ptr);

/**
Free memory allocated with RTalign or RTalignZero.
*/
extern void RTalignedFree(void *rt_ptr);

/**
\brief Switch (HREsetRegion) to the global allocator provided by the region
        HREdefaultRegion (shared = true). And back (shared = false).
 */
extern void RTswitchAlloc(bool shared);

#define RT_NEW(obj) HRE_NEW(NULL,obj)

/*}@*/

extern char* HREstrdup(const char *str);

/**
\brief Parse a string that represents command line options.
 */
extern void RTparseOptions(const char* argline,int *argc_p,char***argv_p);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

