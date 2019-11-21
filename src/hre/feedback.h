// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef HRE_FEEDBACK_H
#define HRE_FEEDBACK_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <errno.h>
#include <popt.h>
#include <stdlib.h>


#ifndef POPT_ARG_LONGLONG
#define POPT_ARG_LONGLONG POPT_ARG_LONG
#endif

#define HRE_EXIT_FAILURE    255
#define HRE_EXIT_TIMEOUT    254
#define HRE_EXIT_SUCCESS    0

/**
\brief Opaque type log stream or channel.
*/
typedef struct runtime_log *log_t;

/**
\brief Log stream for assertion failures.
*/
extern log_t assertion;

/**
\brief Log stream for error messages.
*/
extern log_t lerror;

/**
\brief Log stream for providing information.
\deprecated Please use either infoLong or infoShort.
*/
extern log_t info;

/**
\brief Channel for providing short feedback to the user.

The total amount of infoShort output should be a short document.
This stream is disabled by the -q option.
*/
extern log_t infoShort;

/**
\brief Channel for providing long feedback to the user.

The total amount of infoLong output may be a long document.
This stream is enabled by the -v option.
*/
extern log_t infoLong;

/**
\brief Log stream for printing statistics.
This stream is enabled by the --stats option.
*/
extern log_t stats;

/**
\brief Get a stream that writes to the given log.

If the log is suppressed then this function will return NULL;
This procedure will be deprecated if a different solution for printing ATerms
can be found.
*/
extern FILE* log_get_stream(log_t log);

/**
\brief Utility function for printing single line messages with headers.
\deprecated Please use the Print macro instead.
*/
extern void log_message(log_t log,const char*file,int line,int errnum,
                        const char *fmt,...)
#ifdef _GNU_C_SOURCE
                        __attribute__ ((format (printf, 5, 6)))
#endif
                        ;

/**
\brief Output directly to a log stream.
\deprecated Please use the Printf macro instead.
*/
extern void log_printf(log_t log,const char*file,const char *fmt,...)
#ifdef _GNU_C_SOURCE
                       __attribute__ ((format (printf, 3, 4)))
#endif
                       ;

/**
\brief Test if the given log is active.
*/
extern int log_active(log_t log);

/**
\brief Channel for printing debug messages.

This channel is a macro. If LTSMIN_DEBUG is defined then
debugging info can be printed by using the --debug option.
Otherwise the debug statements are removed at compile time.
*/
#if defined(LTSMIN_DEBUG) && !defined(NDEBUG)
#define debug hre_debug
#else
#define debug NULL
#endif

/*
internal use only!
*/
extern log_t hre_debug;

/* printf uses three steps.
 1. test if the log is compile time enabled (not NULL).
    this allows the compiler to remove printing code completely.
 2. If yes, test if the log is active at this moment.
 3. If yes, compute the argument and print the message.
 */
#define HREmessage(log,...) {\
    if (log && log_active(log)) {\
        log_message(log,__FILE__,__LINE__,0,__VA_ARGS__);\
    }\
}

#define HREmessageCall(log,...) {\
    if (log && log_active(log)) {\
        log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__);\
    }\
}

#define HREprintf(log,...) {\
    if (log && log_active(log)) {\
        log_printf(log,__FILE__,__VA_ARGS__);\
    }\
}

/**
\brief Macro that prints a one-line debug message.
*/
#ifdef LTSMIN_DEBUG
#define Debug(...) HREmessage(debug,__VA_ARGS__)
#else
#define Debug(...) ((void)0);
#endif

/**
\brief Macro that prints a one-line debug message.
*/
#ifdef LTSMIN_DEBUG
#define Debugf(...) HREprintf(debug,__VA_ARGS__)
#else
#define Debugf(...) ((void)0);
#endif

/**
\brief Macro that prints a one-line message to a channel.
*/
#define Print(log,...) HREmessage(log,__VA_ARGS__);

/**
\brief Macro that prints a one-line message to a channel about a call.
*/
#define PrintCall(log,...) HREmessageCall(log,__VA_ARGS__);

/**
\brief Macro that behaves like printf on a channel.
*/
#define Printf(log,...) HREprintf(log,__VA_ARGS__);

/**
\brief Macro that prints an error messages and then aborts.
*/
#define Abort(...) {\
    log_message(lerror,__FILE__,__LINE__,0,__VA_ARGS__);\
    HREabort(HRE_EXIT_FAILURE);\
}

/**
\brief Macro that prints an error message about a system call and then aborts.
*/
#define AbortCall(...) {\
    log_message(lerror,__FILE__,__LINE__,errno,__VA_ARGS__);\
    HREabort(HRE_EXIT_FAILURE);\
}

#define Warning Print
#define Print1(log,...) if (log && HREme(HREglobal()) == 0) HREmessage(log,__VA_ARGS__);
#define Printf1(log,...) if (log && HREme(HREglobal()) == 0) HREprintf(log,__VA_ARGS__);

#define Fatal(code,chan,...) Abort(__VA_ARGS__)

extern void HREprintStack ();

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
