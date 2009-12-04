#ifndef HRE_MAIN_H
#define HRE_MAIN_H

/**
\file hre-main.h
\brief Hybrid Runtime Environment (HRE)

The hybrid runtime environment provides support for
running parallel applications on top of
a variety of shared memory and message passing architectures.
These architectures include
- Posix threads
- MPI
*/

#include <hre-popt.h>
#include <hre-feedback.h>
#include <hre-context.h>
#include <hre-malloc.h>

/**
\brief Start the HRE environment.

This function start all processes and all threads, specified
on the command line.
*/
extern void HREinit(int *argc,char **argv[]);

/**
\brief Emergency shutdown (async).
*/
extern void HREabort(int code) __attribute__ ((noreturn));

/**
\brief Perform a graceful shutdown (collaborative).
*/
extern void HREexit(int code) __attribute__ ((noreturn));

/**
Set the label of this thread.
*/
extern void set_label(const char* fmt,...);
/**
Get the label of this thread.
*/
extern char* get_label();

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

#endif
