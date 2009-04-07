#include <amconfig.h>
#include "runtime.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include "unix.h"
#include <libgen.h>
#include <git_version.h>

#define LABEL_SIZE 1024

int RTverbosity=1;

#define INCR_VERBOSITY 1
#define PRINT_VERSION 2
#define PRINT_HELP 3
#define PRINT_USAGE 4
#define ENABLE_DEBUG 5

static struct poptOption runtime_options[]={
	{ NULL, 'v' , POPT_ARG_NONE , NULL , INCR_VERBOSITY , "increase verbosity of logging" ,NULL },
	{ NULL, 'q' , POPT_ARG_VAL , &RTverbosity , 0 , "reduces number of messages to absolute minimum",NULL },
	{ "debug" , 0 , POPT_ARG_NONE , NULL , ENABLE_DEBUG , "enable debugging output" ,NULL},
	{ "version" , 0 , POPT_ARG_NONE , NULL , PRINT_VERSION , "print the version of this tool",NULL},
	{ "help" , 'h' , POPT_ARG_NONE , NULL , PRINT_HELP , "print help text",NULL},
	{ "usage" , 0 , POPT_ARG_NONE , NULL , PRINT_USAGE , "print usage",NULL},
	POPT_TABLEEND
};


int linear_search(si_map_entry map[],const char*key){
	while(map[0].key){
		if(!strcmp(map[0].key,key)) return map[0].val;
		map++;
	}
	return -1;
}


static void* stackbottom=NULL;
void* RTstackBottom(){
	return stackbottom;
}

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

char* get_label(){
	return (char *)pthread_getspecific(label_key);
}

static void log_begin(log_t log,int line,const char*file){
	fprintf(log->f,"%s",get_label());
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
#ifdef _GNU_SOURCE
		char*err_msg=strerror_r(errnum,errmsg,256);
		if(!err_msg){
#else
		char*err_msg=errmsg;
		if(strerror_r(errnum,errmsg,256)){
#endif
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
		log_to(log,": %s",err_msg);
	}
	log_end(log);
	va_end(args);
}

void RTinit(int *argcp,char**argvp[]){
	stackbottom=argcp;
	RThandleFatal=NULL;
	error=create_log(stderr,"ERROR",LOG_PRINT);
	info=create_log(stderr,NULL,LOG_PRINT);
	debug=create_log(stderr,NULL,LOG_IGNORE);
	pthread_key_create(&label_key, label_destroy);
	char **copy=RTmalloc((*argcp)*sizeof(char*));
	for(int i=0;i<(*argcp);i++){
		copy[i]=strdup((*argvp)[i]);
	}
	*argvp=copy;
	set_label("%s",basename(copy[0]));
}

void RTparseOptions(const char* argline,int *argc_p,char***argv_p){
	char* cmd=get_label();
	int len=strlen(argline)+strlen(cmd);
	char cmdline[len+4];
	sprintf(cmdline,"%s %s",cmd,argline);
	int res=poptParseArgvString(cmdline,argc_p,(const char ***)argv_p);
	if (res){
		Fatal(1,error,"could not parse %s: %s",cmdline,poptStrerror(res));
	}
}

static poptContext optCon=NULL;
static const char* arg_help_global;

void RTexitUsage(int exit_code){
	if (arg_help_global) poptSetOtherOptionHelp(optCon, arg_help_global);
	poptPrintUsage(optCon,stdout,0);
	exit(exit_code);
}
void RTexitHelp(int exit_code){
	char extra[1024];
	if (arg_help_global){
		sprintf(extra,"[OPTIONS] %s",arg_help_global);
		poptSetOtherOptionHelp(optCon, extra);
	}
	poptPrintHelp(optCon,stdout,0);
	exit(exit_code);
}

void RTinitPopt(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[],
	const char* pgm_prefix,const char* arg_help,const char* extra_help
){
	RTinit(argc_p,argv_p);
	struct poptOption optionsTable[] = {
		{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, options , 0 , extra_help , NULL},
		{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, runtime_options , 0 , "Runtime options",NULL},
		POPT_TABLEEND
	};
	arg_help_global=arg_help;
	char*program=(*argv_p)[0];
	char*pgm_base=strrchr(program,'/');
	if(pgm_base) {
		pgm_base++;
	} else {
		pgm_base=program;
	}
	char*pgm_print;
	if (pgm_prefix) {
		char tmp[strlen(pgm_prefix)+strlen(pgm_base)+2];
		sprintf(tmp,"%s %s",pgm_prefix,pgm_base);
		pgm_print=strdup(tmp);
	} else {
		pgm_print=pgm_base;
	}
	(*argv_p)[0]=pgm_print;
	optCon=poptGetContext(NULL, *argc_p, *argv_p, optionsTable, 0);
	for(;;){
		int res=poptGetNextOpt(optCon);
		switch(res){
		case INCR_VERBOSITY:
			RTverbosity++;
			continue;
		case PRINT_VERSION:
			if (strcmp(GIT_VERSION,"")) {
				fprintf(stdout,"%s\n",GIT_VERSION);
			} else {
				fprintf(stdout,"%s\n",PACKAGE_STRING);
			}
			exit(EXIT_SUCCESS);
		case PRINT_HELP:{
			RTexitHelp(EXIT_SUCCESS);
		}
		case PRINT_USAGE:
			RTexitUsage(EXIT_SUCCESS);
		case ENABLE_DEBUG:
			log_set_flags(debug,LOG_PRINT|LOG_WHERE);
			continue;
		default:
			break;
		}
		if (res==-1) break;
		if (res==POPT_ERROR_BADOPT){
			Fatal(1,error,"bad option: %s (use --help for help)",poptBadOption(optCon,0));
		}
		if (res<0) {
			Fatal(1,error,"option parse error: %s",poptStrerror(res));
		} else {
			Fatal(1,error,"option %s has unexpected return %d",poptBadOption(optCon,0),res);
		}
	}
	for(int i=0;i<min_args;i++){
		args[i]=poptGetArg(optCon);
		if (!args[i]) {
			Warning(error,"not enough arguments");
			RTexitUsage(EXIT_FAILURE);
		}
	}
	if (max_args >= min_args) {
		for(int i=min_args;i<max_args;i++){
			if (poptPeekArg(optCon)){
				args[i]=poptGetArg(optCon);
			} else {
				args[i]=NULL;
			}
		}
		if (poptPeekArg(optCon)!=NULL) {
			Warning(error,"too many arguments");
			RTexitUsage(EXIT_FAILURE);
		}
		poptFreeContext(optCon);
		optCon=NULL;
	}
	(*argv_p)[0]=program;
	Warning(debug,"verbosity is set to %d",RTverbosity);
}

char* RTinitNextArg(){
	if (optCon) {
		char* res=poptGetArg(optCon);
		if (res) return res;
		poptFreeContext(optCon);
		optCon=NULL;	
	}
	return NULL;
}

void* RTmalloc(size_t size){
	if(size==0) return NULL;
	void *tmp=malloc(size);
	if (tmp==NULL) Fatal(0,error,"out of memory trying to get %d",size);
	return tmp;
}

void* RTmallocZero(size_t size){
	void *p=RTmalloc(size);
	memset(p, 0, size);
	return p;
}

void RTfree(void *rt_ptr){
	if(rt_ptr != NULL)
            free (rt_ptr);
}

