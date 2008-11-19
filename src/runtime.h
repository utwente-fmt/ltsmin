/** @file runtime.h
 *  @brief Runtime support library.
 */

#ifndef RUNTIME_H
#define RUNTIME_H

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#include <errno.h>
#include <stdint.h>
#include "options.h"
#include <stdio.h>

extern void* RTmalloc(size_t size);

#define runtime_init() RTinit(argc,&argv)
#define runtime_init_args(argcp,argvp) RTinit(*argcp,argvp);take_vars(argcp,*argvp)

extern void RTinit(int argc, char **argv[]);
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

extern void set_label(const char* fmt,...);

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
		log_message(log,__FILE__,__LINE__,errno,"exit with 1 instead of 0");\
		exit(1);\
	} else {\
		exit(code);\
	}\
}
#define FatalCall(code,log,...) {\
	log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__);\
	if (RThandleFatal) RThandleFatal(__FILE__,__LINE__,errno,code);\
	if (code==0) {\
		log_message(log,__FILE__,__LINE__,errno,"exit with 1 instead of 0");\
		exit(1);\
	} else {\
		exit(code);\
	}\
}


#endif
