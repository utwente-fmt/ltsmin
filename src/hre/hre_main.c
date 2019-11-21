// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <libgen.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>

#include <hre/internal.h>
#include <hre/provider.h>

pthread_key_t hre_key;
static char* app_path=NULL;
static char* application=NULL;
static int auto_abort=1;
static int hre_started=0;
static int runtime_count=0;
static array_manager_t runtime_man;
static hre_runtime_t* runtime_array=NULL;
static int thread_count=0;
static int run_threads=0;
static int run_single=1;
static char **global_args;

static void ctx_destroy(void *arg){
    struct thread_context *ctx=arg;
    if (ctx) HREfree(hre_heap,ctx);
}

static struct thread_context* create_context(char *app){
    struct thread_context*ctx=RT_NEW(struct thread_context);
    pthread_setspecific(hre_key,ctx);
    if (gettimeofday(&ctx->init_tv,NULL)){
        AbortCall("gettimeofday");
    }
    ctx->main=0;
    set_label("%s",app);
    return ctx;
}

void HREinitBegin(const char*app_name){
    struct thread_context *ctx;
    if (application==NULL){
        // main thread, first call.
        app_path=strdup(app_name);
        application=basename(app_path);
        pthread_key_create(&hre_key, ctx_destroy);
        ctx=create_context(application);
        ctx->main=1;
        HREinitPopt();
        runtime_man=create_manager(16);
        ADD_ARRAY(runtime_man,runtime_array,hre_runtime_t);
    } else {
        // worker thread or second call.
        ctx=pthread_getspecific(hre_key);
        if (!ctx) create_context((char*)app_name);
    }
    HREinitFeedback(); // CHECK ME: for every thread or just for the main thread?
}

const char* HREappName(){
    return application;
}

const char* HREpathName(){
    return app_path;
}

int HREmainThread(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->main;
}

hre_context_t HREglobal(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->global;
}

void HREglobalSet(hre_context_t context){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (!ctx) ctx=create_context(application);
    ctx->global=context;
}

void HREmainSet(hre_context_t context){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (!ctx) ctx=create_context(application);
    ctx->main_ctx=context;
    ctx->main=1;
}

hre_context_t HREmainGet(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->main_ctx;
}

void HREprocessSet(hre_context_t context){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (!ctx) ctx=create_context(application);
    ctx->process_ctx=context;
}

hre_context_t HREprocessGet(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->process_ctx;
}

void set_label(const char* fmt,...){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    va_list args;
    va_start(args,fmt);
    vsnprintf(ctx->label,sizeof(ctx->label),fmt,args);
    va_end(args);
}

char* get_label(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    return ctx->label;
}

void* HREstackBottom(){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    if (ctx->stack_bottom) return ctx->stack_bottom;
    Abort("stack bottom not available");
}

char* HREgetApplication(){
    return application;
}

static void hre_popt(poptContext con,
                     enum poptCallbackReason reason,
                     const struct poptOption * opt,
                     const char * arg, void * data){
    (void)con;(void)data;
    if(!hre_started) switch(reason){
        case POPT_CALLBACK_REASON_PRE:
        case POPT_CALLBACK_REASON_POST:
            Abort("unexpected call to hre_popt");
            break;
        case POPT_CALLBACK_REASON_OPTION:
            if (!strcmp(opt->longName,"threads")){
                if (!thread_count) return; // ignore if threads disabled.
                if (arg) {
                    thread_count=atoi(arg);
                    if (thread_count<=0) {
                        Abort("less than one thread is impossible!");
                    }
                }
                run_threads = thread_count > 1;
                return;
            }
            Abort("unimplemented option: %s",opt->longName);
            exit(HRE_EXIT_FAILURE);
    }
}

struct poptOption hre_thread_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)hre_popt , 0 , NULL ,NULL },
    { "threads" , 0 , POPT_ARG_INT|POPT_ARGFLAG_OPTIONAL , NULL , 0 ,
      "number of threads to start" , "<thread count>" },
    POPT_TABLEEND
};

void HREabort(int code){
    auto_abort=0;
    hre_context_t global=HREglobal();
    if (global) {
        HREctxAbort(global,code);
    } else {
        exit(code);
    }
}

void HREexit(int code){
    auto_abort=0;
    hre_context_t global=HREglobal();
    if (global) {
        HREctxExit(global,code);
    } else {
        exit(code);
    }
}

void HREexitUsage(int code){
    HREprintUsage();
    HREexit(code);
}

void HREregisterRuntime(hre_runtime_t runtime){
    for(int i=0;i<runtime_count;i++){
        if (runtime_array[i]==runtime) return;
    }
    ensure_access(runtime_man,runtime_count);
    runtime_array[runtime_count]=runtime;
    runtime_count++;
}

void HREenableThreads(int threads, bool selected){
    Debug("Enabling posix threads runtime environment.");
    if (HREmainThread()){
        thread_count=threads;
        if (selected && thread_count > 1) {
            run_threads = 1;
        }
    }
}

void HREdisableSingle(){
    run_single=0;
}

void HREinitStart(int *argc,char **argv[],int min_args,int max_args,char*args[],const char* arg_help){
    struct thread_context *ctx=pthread_getspecific(hre_key);
    ctx->stack_bottom=(void*)argc; // ATerm library needs this.
    if (HREmainThread()){
        // Debug will not work until options are parsed.
        //Print(infoShort,"Starting main thread.");
        //Print(infoShort,"there are %d runtimes configured",runtime_count);
        struct poptOption temp;
        struct poptOption runtime_options[runtime_count+3];
        for(int i=0;i<runtime_count;i++){
            temp=(struct poptOption){
                NULL, 0, POPT_ARG_INCLUDE_TABLE,
                (struct poptOption*)runtime_array[i]->options,
                0, NULL, NULL
            };
            runtime_options[i]=temp;
        }
        int idx=runtime_count;
        if (thread_count){
            temp=(struct poptOption){ NULL, 0 , POPT_ARG_INCLUDE_TABLE, (struct poptOption *) hre_thread_options , 0 , NULL , NULL};
            runtime_options[idx]=temp;
            idx++;
        }
        temp=(struct poptOption){ NULL, 0 , POPT_ARG_INCLUDE_TABLE, (struct poptOption *) hre_feedback_options , 0 , NULL , NULL};
        runtime_options[idx]=temp;
        idx++;
        temp=(struct poptOption)POPT_TABLEEND;
        runtime_options[idx]=temp;
        HREaddOptions(runtime_options,"Runtime options");
        //Print(infoShort,"parsing run time options");
        poptContext optCon=poptGetContext(NULL, *argc, (const char**)(*argv), runtime_options, 0);
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
        // From here on Debug works.
        Debug("starting selected run time");
        hre_context_t main_ctx=NULL;
        for(int i=0;i<runtime_count;i++){
            if (runtime_array[i]->selected){
                hre_started=1;
                runtime_array[i]->start(argc,argv,run_threads);
                main_ctx=HREmainGet();
                break;
            }
        }
        if (!hre_started && run_single) {
            hre_started=1;
            main_ctx=HREcreateSingle();
            HREmainSet(main_ctx);
            HREglobalSet(main_ctx);
        }
        if (!hre_started){
            Abort("None of the configured run times was selected.");
        }
        int res;
        Debug("parsing options");
        // parse options at worker 0.
        if (HREme(main_ctx)==0){
            res=HREdoOptions(*argc,*argv,min_args,max_args,args,arg_help);
        } else {
            res=0;
        }
        // Exit gracefully if there is an error.
        if (HREcheckAny(main_ctx,res)){
            HREexit(HRE_EXIT_FAILURE);
        }
        // Parse at other workers.
        if (HREme(main_ctx)!=0){
            res=HREdoOptions(*argc,*argv,min_args,max_args,args,arg_help);
            if (res) Abort("unexpected failure during option parsing");
        }
        // if necessary start threads.
        if (run_threads){
            Debug("starting %d threads",thread_count);
            global_args=args;
            HREpthreadRun(thread_count);
            Abort("HREpthreadRun is not supposed to return");
        }
        Debug("HRE is started");
        // return to do the real work.
        return;
    } else {
        Debug("Starting worker thread.");
        for(int i=0;i<runtime_count;i++){
            if (runtime_array[i]->selected){
                runtime_array[i]->start_thread();
                break;
            }
        }
        for(int i=0;i<max_args;i++){
            // copy arguments
            args[i]=global_args[i];
        }
    }
}

void HREenableStandard(){
    HREenableThreads(RTnumCPUs(), false);
    HREenableFork(RTnumCPUs(), false);
}

