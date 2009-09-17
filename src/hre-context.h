#ifndef HRE_CONTEXT_H
#define HRE_CONTEXT_H

/**
\file hre-context.h
\brief Hybrid runtime contexts.

A context is the rough equivalent of the MPI communicator.
*/

#include <stream.h>

/// opaque type
typedef struct hre_context_s *hre_context_t;

/**
\brief Get the global context.

When work on HRE advances, we'll need HRElocal, HREprocess, etc.
This information is provided as a function because it is thread
specific information.
*/
extern hre_context_t HREglobal();

/// create bare object
extern hre_context_t HREctxCreate(int me,int peers,size_t user_size);

/// check and complete
extern void HREctxComplete(hre_context_t ctx);

/**
\brief Set linking streams to build library procedures upon.
*/
extern void HREsetLinks(hre_context_t ctx,stream_t* links);

/// destroy type
typedef void(*hre_destroy_m)(hre_context_t ctx);

/// set destroy method
extern void HREsetDestroy(hre_context_t ctx,hre_destroy_m method);

/// call destroy method
extern void HREctxDestroy(hre_context_t ctx);

/// What is my number?
extern int HREme(hre_context_t ctx);

/// How many peers do I have?
extern int HREpeers(hre_context_t ctx);

/// Often used: am I the leader?
extern int HREleader(hre_context_t ctx);

/// barrier check type
typedef int(*hre_check_any_m)(hre_context_t ctx,int arg);

/// set barrier check method
extern void HREsetCheckAny(hre_context_t ctx,hre_check_any_m method);

/// Check if any of the peers passes true.
extern int HREcheckAny(hre_context_t ctx,int arg);

/// abort method
typedef void (*hre_abort_m)(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// set the abort method.
extern void HREsetAbort(hre_context_t ctx,hre_abort_m method);

/// Abort the running application.
extern void HREctxAbort(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// exit method
typedef void (*hre_exit_m)(hre_context_t ctx,int code) __attribute__ ((noreturn));

/// set the exit method.
extern void HREsetExit(hre_context_t ctx,hre_exit_m method);

/// Exit from the running application.
extern void HREctxExit(hre_context_t ctx,int code) __attribute__ ((noreturn));

#endif
