#include <amconfig.h>
#include <hre-main.h>
#include <git_version.h>
#include <hre-internal.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include "unix.h"
#include <runtime.h>

#define PRINT_VERSION 1
#define PRINT_HELP 2
#define PRINT_USAGE 3

static struct poptOption runtime_options[]={
    { "version" , 0 , POPT_ARG_NONE , NULL , PRINT_VERSION , "print the version of this tool",NULL},
    { "help" , 'h' , POPT_ARG_NONE , NULL , PRINT_HELP , "print help text",NULL},
    { "usage" , 0 , POPT_ARG_NONE , NULL , PRINT_USAGE , "print usage",NULL},
    POPT_TABLEEND
};

void HREinitPopt(){
    
}

static poptContext optCon=NULL;
static const char* arg_help_global;

void HREprintUsage(){
    if (arg_help_global) poptSetOtherOptionHelp(optCon, arg_help_global);
    poptPrintUsage(optCon,stdout,0);
}

void HREprintHelp(){
    char extra[1024];
    if (arg_help_global){
        sprintf(extra,"[OPTIONS] %s",arg_help_global);
        poptSetOtherOptionHelp(optCon, extra);
    }
    poptPrintHelp(optCon,stdout,0);
}

char* HREnextArg(){
    if (optCon) {
        char* res=strdup(poptGetArg(optCon));
        if (res) return res;
        poptFreeContext(optCon);
        optCon=NULL;    
    }
    return NULL;
}

static int HREcallPopt(int argc,char*argv[],struct poptOption optionsTable[],
                      int min_args,int max_args,char*args[]){
    optCon=poptGetContext(NULL, argc,(const char**)argv, optionsTable, 0);
    for(;;){
        int res=poptGetNextOpt(optCon);
        switch(res){
            case PRINT_VERSION:
                if (strcmp(GIT_VERSION,"")) {
                    fprintf(stdout,"%s\n",GIT_VERSION);
                } else {
                    fprintf(stdout,"%s\n",PACKAGE_STRING);
                }
                return 1;
            case PRINT_HELP:{
                HREprintHelp();
                return 1;
            }
            case PRINT_USAGE:
                HREprintUsage();
                return 1;
            default:
                break;
        }
        if (res==-1) break;
        if (res==POPT_ERROR_BADOPT){
            Print(error,"bad option: %s (use --help for help)",poptBadOption(optCon,0));
            return 1;
        }
        if (res<0) {
            Fatal(1,error,"option parse error: %s",poptStrerror(res));
        } else {
            Fatal(1,error,"option %s has unexpected return %d",poptBadOption(optCon,0),res);
        }
    }
    for(int i=0;i<min_args;i++){
        args[i]=strdup(poptGetArg(optCon));
        if (!args[i]) {
            Warning(error,"not enough arguments");
            HREprintUsage();
            return 1;
        }
    }
    if (max_args >= min_args) {
        for(int i=min_args;i<max_args;i++){
            if (poptPeekArg(optCon)){
                args[i]=strdup(poptGetArg(optCon));
            } else {
                args[i]=NULL;
            }
        }
        if (poptPeekArg(optCon)!=NULL) {
            Warning(error,"too many arguments");
            HREprintUsage();
            return 1;
        }
        poptFreeContext(optCon);
        optCon=NULL;
    }
    return 0;
}


void HREaddOptions(struct poptOption *options,const char* header){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (ctx->next_group>=MAX_OPTION_GROUPS) {
        Fatal(1,error,"too many options groups");
    }
    struct poptOption include=
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, options , 0 , header , NULL};
    ctx->option_group[ctx->next_group]=include;
    ctx->next_group++;
    struct poptOption null_opt=POPT_TABLEEND;
    ctx->option_group[ctx->next_group]=null_opt;
}

void HREparseOptions(
    int argc,char*argv[],
    int min_args,int max_args,char*args[],
    const char* arg_help
){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    HREaddOptions(runtime_options,"Runtime options");
    HREaddOptions(hre_feedback_options,"Feedback options");
    if (hre_bare==0) HREaddOptions(hre_boot_options,"Parallel boot options");
    arg_help_global=arg_help;
    int stop=0;
    if (HREleader(HREglobal())){
        stop=HREcallPopt(argc,argv,ctx->option_group,min_args,max_args,args);
    }
    if (HREcheckAny(HREglobal(),stop)) {
        HREexit(0);
    }
    if (!HREleader(HREglobal())){
        stop=HREcallPopt(argc,argv,ctx->option_group,min_args,max_args,args);
    }
    if (HREcheckAny(HREglobal(),stop)) {
        HREexit(0);
    }
}
