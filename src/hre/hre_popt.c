// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <hre/git_version.h>
#include <hre/internal.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <util-lib/dynamic-array.h>

enum {
    PRINT_VERSION = 1,
    PRINT_HELP,
    PRINT_USAGE,
};

static struct poptOption runtime_options[]={
    { "version" , 0 , POPT_ARG_NONE , NULL , PRINT_VERSION , "print the version of this tool",NULL},
    { "help" , 'h' , POPT_ARG_NONE , NULL , PRINT_HELP , "print help text",NULL},
    { "usage" , 0 , POPT_ARG_NONE , NULL , PRINT_USAGE , "print usage",NULL},
    POPT_TABLEEND
};

static int option_groups=0;
static array_manager_t option_man;
static struct poptOption *option_group=NULL;
static poptContext optCon=NULL;
static pthread_mutex_t next_arg_mutex = PTHREAD_MUTEX_INITIALIZER;
static char user_usage[1024]={0};

void HREinitPopt(){
    option_man=create_manager(16);
    ADD_ARRAY(option_man,option_group,struct poptOption);
    struct poptOption null_opt=POPT_TABLEEND;
    ensure_access(option_man,0);
    option_group[0]=null_opt;
}

void HREprintUsage(){
    if (HREme(HREglobal()) == 0) poptPrintUsage(optCon,stdout,0);
}

void HREprintHelp(){
    if (HREme(HREglobal()) == 0) poptPrintHelp(optCon,stdout,0);
}

char* HREnextArg(){
    if (optCon) {
        pthread_mutex_lock(&next_arg_mutex);
        char* res=HREstrdup(poptGetArg(optCon));
        if (!res) {
            poptFreeContext(optCon);
            optCon=NULL;
        }
        pthread_mutex_unlock(&next_arg_mutex);
        return res;
    }
    return NULL;
}

static int HREcallPopt(int argc,char*argv[],struct poptOption optionsTable[],
                      int min_args,int max_args,char*args[],const char* arg_help){
    optCon=poptGetContext(NULL, argc,(const char**)argv, optionsTable, 0);
    if (arg_help){
        snprintf(user_usage, sizeof user_usage, "[OPTIONS] %s",arg_help);
        poptSetOtherOptionHelp(optCon, user_usage);
    }
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
            Print(lerror,"bad option: %s (use --help for help)",poptBadOption(optCon,0));
            return 1;
        }
        if (res<0) {
            Abort("option parse error: %s",poptStrerror(res));
        } else {
            Abort("option %s has unexpected return %d",poptBadOption(optCon,0),res);
        }
    }
    for(int i=0;i<min_args;i++){
        args[i] = HREstrdup (poptGetArg (optCon));
        if (!args[i]) {
            Print(lerror,"not enough arguments");
            HREprintUsage();
            return 1;
        }
    }
    if (max_args >= min_args) {
        for(int i=min_args;i<max_args;i++){
            if (poptPeekArg(optCon)){
                args[i] = HREstrdup (poptGetArg (optCon));
            } else {
                args[i]=NULL;
            }
        }
        if (poptPeekArg(optCon)!=NULL) {
            Print(lerror,"too many arguments");
            HREprintUsage();
            return 1;
        }
        poptFreeContext(optCon);
        optCon=NULL;
    }
    return 0;
}


void HREaddOptions(const struct poptOption *options,const char* header){
    if (HREmainThread()){
        ensure_access(option_man,option_groups+1);
        option_group[option_groups+1]=option_group[option_groups];
        struct poptOption include=
        { NULL, 0 , POPT_ARG_INCLUDE_TABLE, (struct poptOption *) options , 0 , header , NULL};
        option_group[option_groups]=include;
        option_groups++;
    }
}

int HREdoOptions(int argc,char *argv[],int min_args,int max_args,char*args[],const char* arg_help){
    HREaddOptions(runtime_options,NULL);
    return HREcallPopt(argc,argv,option_group,min_args,max_args,args,arg_help);
}

void RTparseOptions(const char* argline,int *argc_p,char***argv_p){
    int len=strlen(argline)+8;
    char cmdline[len];
    sprintf(cmdline,"fake %s",argline);
    int res=poptParseArgvString(cmdline,argc_p,(const char ***)argv_p);
    if (res){
        Fatal(1,error,"could not parse %s: %s",cmdline,poptStrerror(res));
    }
    (*argv_p)[0]=strdup(HREgetApplication());
}


