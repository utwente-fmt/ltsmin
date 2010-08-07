#include <config.h>
#include "runtime.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "unix.h"
#include <libgen.h>
#include <git_version.h>
#include <hre-main.h>
#include <assert.h>

int RTverbosity=1;

int linear_search(si_map_entry map[],const char*key){
	while(map[0].key){
		if(!strcmp(map[0].key,key)) return map[0].val;
		map++;
	}
	return -1;
}

void (*RThandleFatal)(const char*file,int line,int errnum,int code);

void RTinit(int *argcp,char**argvp[]){
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

void* RTalign(size_t align, size_t size)
{
    void *ret;
    errno = posix_memalign(&ret, align, size);
    if (errno) {
    switch (errno) {
        case ENOMEM:
            Fatal(0,error,"out of memory on allocating %d bytes aligned at %d", 
                  size, align);
        case EINVAL:
            Fatal(0,error,"invalid alignment %d", align);
        default:
            Fatal(0,error,"unknown error allocating %d bytes aligned at %d", 
                  size, align);
    }}
    assert(NULL != ret);
    return ret;
}

void* RTrealloc(void *rt_ptr, size_t size){
    void *tmp=realloc(rt_ptr,size);
    if (tmp==NULL) Fatal(0,error,"out of memory trying to resize to %d",size);
    return tmp;
}

char* RTstrdup(const char *str){
    if (str == NULL) return NULL;
    char *tmp = strdup (str);
    if (tmp == NULL) Fatal(0, error, "out of memory trying to get %d",
                           strlen (str)+1);
    return tmp;
}

void RTfree(void *rt_ptr){
	if(rt_ptr != NULL)
            free (rt_ptr);
}


void *
RTdlsym (const char *libname, void *handle, const char *symbol)
{
    void *ret = dlsym (handle, symbol);
    if (ret == NULL) {
        const char *dlerr = dlerror ();
        Fatal (1, error, "dynamically loading from `%s': %s",
               libname,
               dlerr != NULL ? dlerr : "unknown error");
    }
    return ret;
}
