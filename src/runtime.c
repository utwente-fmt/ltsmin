#include <amconfig.h>
#include "runtime.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include "unix.h"
#include <libgen.h>
#include <git_version.h>
#include <hre-main.h>

int RTverbosity=1;

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

void (*RThandleFatal)(const char*file,int line,int errnum,int code);

void RTinit(int *argcp,char**argvp[]){
	stackbottom=argcp;
    RThandleFatal=NULL;
    HREinitBare(argcp,argvp);
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

void RTexitUsage(int exit_code){
    HREprintUsage();
    exit(exit_code);
}

void RTexitHelp(int exit_code){
    HREprintHelp();
    exit(exit_code);
}

void RTinitPopt(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[],
	const char* pgm_prefix,const char* arg_help,const char* extra_help
){
    (void) pgm_prefix;
    RTinit(argc_p,argv_p);
    HREaddOptions(options,extra_help);
    HREparseOptions(*argc_p,*argv_p,min_args,max_args,args,arg_help);
    if (!log_active(infoShort)) RTverbosity=0;
    if (log_active(infoLong)) RTverbosity=2;
}

char* RTinitNextArg(){
    return HREnextArg();
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

