/** @file runtime.h
 *  @brief Runtime support library.
 */

#ifndef RUNTIME_H
#define RUNTIME_H

/**
\defgroup runtime Runtime support library.
 */
///@{

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#include <errno.h>
#include <stdint.h>
#include <popt.h>

typedef struct {
	char* key;
	int val;
} si_map_entry;

/**
\brief Find the value for a given key, or -1 if it does not exist.
 */
extern int linear_search(si_map_entry map[],const char*key);

/**
\brief Parse a string that represents command line options.
 */
extern void RTparseOptions(const char* argline,int *argc_p,char***argv_p);

/**
\brief Return the bottom of the stack.

The current implementation returns the bottom of the stack of the main thread,
regardsless of which thread calls this function.
 */
extern void* RTstackBottom();

extern void* RTmalloc(size_t size);

extern void* RTmallocZero(size_t size);

extern void RTfree(void *rt_ptr);

#define RT_NEW(sort) ((sort*)RTmallocZero(sizeof(sort)))


/**
\brief Initialize the runtime library using popt.
\param min_args The minimum number of arguments allowed.
\param max_args The maximum number of arguments allowed, where -1 denotes infinite.
*/
extern void RTinitPopt(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[],
	const char* pgm_prefix,const char* arg_help,const char* extra_help
);

/**
\brief Return the next argument.
 */
extern char* RTinitNextArg();


/**
\brief Print usage of tool.
Due to the fact that we may have different extra string for help and usage,
the function poptPrintUsage should not be called directly.
 */
extern void RTexitUsage(int exit_code);

/**
\brief Print help of tool.
Due to the fact that we may have different extra string for help and usage,
the function poptPrintHelp should not be called directly.
 */
extern void RTexitHelp(int exit_code);

extern int RTverbosity;

extern void RTinit(int *argc, char **argv[]);
/**< @brief Initializes the runtime library.

Some platform do not like it is you change argv. Thus this
call makes a copy of argv allowing subsequent calls
to make changes to argv without copying again.
*/


typedef struct runtime_log *log_t;

extern log_t error;
extern log_t info;
extern log_t debug;

#define LOG_IGNORE 0x00
#define LOG_PRINT 0x01
#define LOG_WHERE 0x02
#define LOG_TAG 0x04

extern log_t create_log(FILE* f,char*tag,int flags);

extern void log_set_flags(log_t log,int flags);
extern int log_get_flags(log_t log);

/**
\brief Get a stream that write to the given log.

If the log is suppressed then this function will return NULL;
*/
extern FILE* log_get_stream(log_t log);


extern void set_label(const char* fmt,...);
extern char* get_label();

extern void log_message(log_t log,const char*file,int line,int errnum,const char *fmt,...);

extern void (*RThandleFatal)(const char*file,int line,int errnum,int code);

#define Warning(log,...) log_message(log,__FILE__,__LINE__,0,__VA_ARGS__)
#define WarningCall(log,...) log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__)
#define Log(log,...) log_message(log,__FILE__,__LINE__,0,__VA_ARGS__)
#define LogCall(log,...) log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__)
#define Fatal(code,log,...) {\
	log_message(log,__FILE__,__LINE__,0,__VA_ARGS__);\
	if (RThandleFatal) RThandleFatal(__FILE__,__LINE__,0,code);\
	if (code==0) {\
		log_message(log,__FILE__,__LINE__,errno,"exit with FAILURE instead of 0");\
		exit(EXIT_FAILURE);\
	} else {\
		exit(code);\
	}\
}
#define FatalCall(code,log,...) {\
	log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__);\
	if (RThandleFatal) RThandleFatal(__FILE__,__LINE__,errno,code);\
	if (code==0) {\
		log_message(log,__FILE__,__LINE__,errno,"exit with FAILURE instead of 0");\
		exit(EXIT_FAILURE);\
	} else {\
		exit(code);\
	}\
}

///@}


#endif
