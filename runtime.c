
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

static int prop_count=0;
static char**prop_name;
static char**prop_val;

void runtime_init_args(int*argc,char**argv[]){
	char**keep=RTmalloc((*argc)*sizeof(char*));
	prop_name=RTmalloc((*argc)*sizeof(char*));
	prop_val=RTmalloc((*argc)*sizeof(char*));
	keep[0]=(*argv)[0];
	int kept=1;
	for(int i=1;i<(*argc);i++){
		//Warning(info,"argument %d is %s",i,(*argv)[i]);
		char *pos=strchr((*argv)[i],'=');
		if(pos){
			char *name=strndup((*argv)[i],pos-((*argv)[i]));
			char *val=strdup(pos+1);
			//Warning(info,"property %s is %s",name,val);
			prop_name[prop_count]=name;
			prop_val[prop_count]=val;
			prop_count++;
		} else {
			keep[kept]=(*argv)[i];
			kept++;
		}
	}
	*argc=kept;
	*argv=keep;
}

char* prop_get_S(char*name,char* def_val){
	for(int i=0;i<prop_count;i++){
		if(!strcmp(name,prop_name[i])) return prop_val[i];
	}
	return def_val;
}

uint32_t prop_get_U32(char*name,uint32_t def_val){
	for(int i=0;i<prop_count;i++){
		if(!strcmp(name,prop_name[i])) return atoi(prop_val[i]);
	}
	return def_val;
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

