#include "runtime.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include "unix.h"
#include <libgen.h>

#define LABEL_SIZE 1024

struct runtime_log {
	FILE*f;
	char *tag;
	int flags;
};

log_t create_log(FILE* f,char*tag,int flags){
	log_t log=(log_t)RTmalloc(sizeof(struct runtime_log));
	log->f=f;
	log->tag=tag?strdup(tag):NULL;
	log->flags=flags;
	return log;
}
void log_set_flags(log_t log,int flags){
	log->flags=flags;
}

int log_get_flags(log_t log){
	return log->flags;
}

FILE* log_get_stream(log_t log){
	if (log->flags & LOG_PRINT) {
		return log->f;
	} else {
		return NULL;
	}
}


static pthread_key_t label_key;

void (*RThandleFatal)(const char*file,int line,int errnum,int code);

log_t error=NULL;
log_t info=NULL;
log_t debug=NULL;

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

static void log_begin(log_t log,int line,const char*file){
	fprintf(log->f,"%s",(char *)pthread_getspecific(label_key));
	if (log->flags & LOG_WHERE) fprintf(log->f,", file %s, line %d",file,line);
	if (log->tag) fprintf(log->f,", %s",log->tag);
	fprintf(log->f,": ");
}

static void log_va(log_t log,const char *fmt, va_list args){
	vfprintf(log->f,fmt,args);
}

static void log_to(log_t log,const char *fmt,...){
	va_list args;
	va_start(args,fmt);
	log_va(log,fmt,args);
	va_end(args);
}

static void log_end(log_t log){
	fprintf(log->f,"\n");
}

void log_message(log_t log,const char*file,int line,int errnum,const char *fmt,...){
	struct runtime_log null_log;
	if (!log) {
		null_log.f=stderr;
		null_log.tag=NULL;
		null_log.flags=LOG_PRINT;
		log=&null_log;
	} else {
		if ((log->flags & LOG_PRINT)==0) return;
	}
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

void RTinit(int argc,char**argv[]){
	RThandleFatal=NULL;
	error=create_log(stderr,"ERROR",LOG_PRINT);
	info=create_log(stderr,NULL,LOG_PRINT);
	debug=create_log(stderr,NULL,LOG_PRINT);
	pthread_key_create(&label_key, label_destroy);
	char **copy=RTmalloc(argc*sizeof(char*));
	for(int i=0;i<argc;i++){
		copy[i]=strdup((*argv)[i]);
	}
	*argv=copy;
	set_label("%s",basename(copy[0]));
}


void* RTmalloc(size_t size){
	if(size==0) return NULL;
	void *tmp=malloc(size);
	if (tmp==NULL) Fatal(0,error,"out of memory trying to get %d",size);
	return tmp;
}

