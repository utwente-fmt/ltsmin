
#ifndef RUNTIME_H
#define RUNTIME_H

#define _XOPEN_SOURCE 600 
#include <string.h>
#include <pthread.h>
#include <setjmp.h>
#include <errno.h>



extern void* RTmalloc(size_t size);

extern void runtime_init();

typedef struct runtime_log *log_t;

extern log_t error;
extern log_t info;

extern void throw_int(int e);

extern void set_label(const char* fmt,...);

extern void log_message(log_t log,char*file,int line,int errnum,const char *fmt,...);

#define Warning(log,...) log_message(log,__FILE__,__LINE__,0,__VA_ARGS__)
#define WarningCall(log,...) log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__)
#define Fatal(code,log,...) {log_message(log,__FILE__,__LINE__,0,__VA_ARGS__);throw_int(code);}
#define FatalCall(code,log,...) {log_message(log,__FILE__,__LINE__,errno,__VA_ARGS__);throw_int(code);}

extern pthread_key_t jmp_key;

#define jmp_try \
{\
	sigjmp_buf __jmp_env;\
	void *__jmp_old=pthread_getspecific(jmp_key);\
	pthread_setspecific(jmp_key,(void*)__jmp_env);\
	int __jmp_res=sigsetjmp( __jmp_env,1);\
	if(__jmp_res==0){


#define jmp_catch(e) \
		pthread_setspecific(jmp_key,__jmp_old);\
	} else {\
		int e=__jmp_res;\
		pthread_setspecific(jmp_key,__jmp_old);\

#define jmp_end \
	}\
}


#endif
