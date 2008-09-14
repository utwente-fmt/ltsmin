
#include "runtime.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>

pthread_key_t jmp_key;

#define LABEL_SIZE 1024

static pthread_key_t label_key;

log_t err=NULL;
log_t info=NULL;

void set_label(const char* fmt,...){
	va_list args;
	va_start(args,fmt);
	char* lbl=(char*)malloc(LABEL_SIZE);
	vsnprintf(lbl,LABEL_SIZE,fmt,args);
	pthread_setspecific(label_key,(void*)lbl);
	va_end(args);
}

static void label_destroy(void *buf){
	if(buf) free(buf);
}

static void log_begin(log_t log,int line,char*file){
	fprintf(stderr,"%s, file %s, line %d: ",(char *) pthread_getspecific(label_key),file,line);
}

static void log_va(log_t log,const char *fmt, va_list args){
	vfprintf(stderr,fmt,args);
}

static void log_to(log_t log,const char *fmt,...){
	va_list args;
	va_start(args,fmt);
	log_va(log,fmt,args);
	va_end(args);
}

static void log_end(log_t log){
	fprintf(stderr,"\n");
}

void log_message(log_t log,char*file,int line,int errnum,const char *fmt,...){
	va_list args;
	va_start(args,fmt);
	log_begin(log,line,file);
	log_va(log,fmt,args);
	if (errnum) {
		char errmsg[256];
		if(strerror_r(errnum,errmsg,256)){
			switch(errno){
			case EINVAL:
				sprintf(errmsg,"%d is not an error",errnum);
				break;
			case ERANGE:
				sprintf(errmsg,"preallocated errmsg too short");
				break;
			default:
				sprintf(errmsg,"this statement should have been unreachable");
			}
		}
		log_to(log,": %s",errmsg);
	}
	log_end(log);
	va_end(args);
}


void runtime_init(){
	pthread_key_create(&jmp_key, NULL);
	pthread_key_create(&label_key, label_destroy);
}

void throw_int(int e){
	void *jump=pthread_getspecific(jmp_key);
	if (jump==NULL) {
		exit(e);
	} else {
		siglongjmp(jump,e);
	}
}

void* RTmalloc(size_t size){
	if(size==0) return NULL;
	void *tmp=malloc(size);
	if (tmp==NULL) Fatal(0,error,"out of memory");
	return tmp;
}

