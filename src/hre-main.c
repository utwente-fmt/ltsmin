#include <config.h>
#include <hre-main.h>
#include "hre-internal.h"
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <hre-main.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

pthread_key_t hre_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;

hre_context_t HREglobal(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->global;
}

static void ctx_destroy(void *arg){
    struct thread_context *ctx=arg;
    HREfreeGuess(hre_heap,ctx);
}

static void make_key()
{
    pthread_key_create(&hre_key, ctx_destroy);
}


void set_label(const char* fmt,...){
    pthread_once(&key_once, make_key);
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (ctx==NULL){
        ctx=HRE_NEW(hre_heap,struct thread_context);
        pthread_setspecific(hre_key,ctx);
    }
    if (gettimeofday(&ctx->init_tv,NULL)){
        AbortCall("gettimeofday");
    }
    va_list args;
    va_start(args,fmt);
    vsnprintf(ctx->label,sizeof(ctx->label),fmt,args);
    va_end(args);
}

char* get_label(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->label;
}

static void* stack_bottom=NULL;
void* HREstackBottom(){
    return stack_bottom;
}

static int auto_abort=1;
static char*program=NULL;

static void hre_auto_abort(){
    if(auto_abort==1) {
        Abort("bad exit, aborting");
    }
}

static int threads=1;
static int hre_started=0;
int hre_bare=0;

static void hre_popt(poptContext con,
                     enum poptCallbackReason reason,
                     const struct poptOption * opt,
                     const char * arg, void * data){
    (void)con;(void)data;
    if(!hre_started) switch(reason){
        case POPT_CALLBACK_REASON_PRE:
        case POPT_CALLBACK_REASON_POST:
            Abort("unexpected call to hre_popt");
        case POPT_CALLBACK_REASON_OPTION:
            if (!strcmp(opt->longName,"threads")){
                if (!arg || (threads=atoi(arg))<=0) {
                    Abort("less than one thread is impossible!");
                }
                return;
            }
            Abort("unimplemented option: %s",opt->longName);
            exit(EXIT_FAILURE);
    }
}

struct poptOption hre_boot_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)hre_popt , 0 , NULL ,NULL },
    { "threads" , 0 , POPT_ARG_INT , NULL , 0 ,
    "number of threads to start" , "<thread count>" },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, hre_mpi_options , 0 , NULL , NULL },
    POPT_TABLEEND
};

static void HREsetup(int *argc,char **argv[]){
    stack_bottom=argc;
    set_label("%s",basename((*argv)[0]));
    HREinitFeedback();
    program=get_label();
}

void HREinitBare(int *argc,char **argv[]){
    HREsetup(argc,argv);
    struct thread_context *ctx=pthread_getspecific(hre_key);
    ctx->global=HREctxLinks(0,1,NULL);
    hre_bare=1;
}

void HREinit(int *argc,char **argv[]){
    if (hre_started) return;
    HREsetup(argc,argv);
    struct thread_context *ctx=pthread_getspecific(hre_key);
    poptContext optCon=poptGetContext(NULL, 
                            *argc, (const char**)(*argv),
                            hre_boot_options, 0);
    for(;;){
        int res=poptGetNextOpt(optCon);
        if (res==-1) break; // no more options
            if (res==POPT_ERROR_BADOPT) continue; // option that HRE doesn't know about
                if (res<0) {
                    Abort("option parse error: %s",poptStrerror(res));
                } else {
                    Abort("option %s has unexpected return %d",poptBadOption(optCon,0),res);
                }
    }
    poptFreeContext(optCon);
    hre_started=1;
    if(hre_init_mpi>=0){
        int len=strlen((*argv)[0]);
        if (len>=3 && !strcmp((*argv)[0]+(len-3),"mpi") && !hre_mpirun){
            // name ending in -mpi, without --mpirun means --mpi as well.
            hre_init_mpi=1;
        }
        if (hre_force_mpi && !hre_mpirun) hre_init_mpi=1;
        if (hre_init_mpi){
            if (threads>1) Abort("multi threaded MPI not yet supported");
            HREinitMPI(argc,argv);
            return;
        }
        if (hre_mpirun) HREmpirun(*argc,*argv,hre_mpirun);
    }
    if (atexit(hre_auto_abort)){
        Abort("atexit failed");
    }
    if (threads >1) HREpthreadRun(*argc,*argv,threads);
    ctx->global=HREctxLinks(0,1,NULL);
}


void HREabort(int code){
    auto_abort=0;
    HREctxAbort(HREglobal(),code);
}

void HREexit(int code){
    auto_abort=0;
    HREctxExit(HREglobal(),code);
}

