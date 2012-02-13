// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <config.h>

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <hre/provider.h>

static size_t SHARED_SIZE=134217728;
static size_t SHARED_ALIGN=64;
#define QUEUE_SIZE 64

struct shared_area {
    pthread_mutexattr_t mutexattr;
    pthread_mutex_t mutex;
    pthread_condattr_t condattr;
    size_t size;
    size_t align;
    size_t next;
};

struct hre_context_s {
    struct shared_area* shared;
    struct message_queue *queues;
};

struct message_queue {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct msg_queue_s* comm;
};

struct shm_template {
    char shm_template[24];
    void *shm;
};

union shm_pointer {
    uint64_t val;
    void* ptr;
};

static void* area_malloc(void* ptr,size_t size){
    struct shared_area* area=(struct shared_area*)ptr;
    pthread_mutex_lock(&area->mutex);
    if (area->next+size > area->size) Abort("area memory exhausted");
    size_t tmp=area->next;
    void*res=((void*)area)+area->next;
    int remainder=size%area->align;
    size=size-remainder;
    area->next=area->next+size+(remainder?area->align:0);
    Debug("allocated %d from %d to %d next %d",size,tmp,tmp+size,area->next);
    pthread_mutex_unlock(&area->mutex);
    return res;
}

static void* area_realloc(void* area,void *rt_ptr, size_t size){
    if (rt_ptr && size) {
        Abort("cannot reallocate inside area");
    }
    if (rt_ptr==NULL ) return area_malloc(area,size);
    return NULL;
}

static void area_free(void*area,void *rt_ptr){
    (void)area;
    (void)rt_ptr;
}

static void queue_put(hre_context_t context,hre_msg_t msg,int queue){
    if (msg->comm >= QUEUE_SIZE) Abort("number of communicators exceeds maximum of %d",QUEUE_SIZE);
    pthread_mutex_lock(&context->queues[queue].lock);
    hre_put_msg(&context->queues[queue].comm[msg->comm],msg);
    pthread_cond_broadcast(&context->queues[queue].cond);
    pthread_mutex_unlock(&context->queues[queue].lock);
}

static void queue_send(hre_context_t context,hre_msg_t msg){
    queue_put(context,msg,msg->target);
}

static void queue_while(hre_context_t ctx,int*condition){
    int me=HREme(ctx);
    hre_msg_t msg=NULL;
    int max_comm=array_size(HREcommManager(ctx));
    int comm;
    for(;;){
        pthread_mutex_lock(&ctx->queues[me].lock);
        //Print(infoShort,"scanning queue %d below %d",me,max_comm);
        for(comm=0;comm<max_comm;comm++){
            msg=hre_get_msg(&ctx->queues[me].comm[comm]);
            if (msg) break;
        }
        while(!msg && *condition) {
            //Print(infoShort,"blocking %d",*condition);
            pthread_cond_wait(&ctx->queues[me].cond, &ctx->queues[me].lock);
            //Print(infoShort,"scanning queue %d below %d",me,max_comm);
            for(comm=0;comm<max_comm;comm++){
                msg=hre_get_msg(&ctx->queues[me].comm[comm]);
                if (msg) break;
            }
        }
        pthread_mutex_unlock(&ctx->queues[me].lock);
        if (!msg) break;
        /* Messages are put in the queue with the sending context.
           They have to be processed with respect to the receiving context.
        */
        msg->context=ctx;
        if (me==(int)msg->target) {
            // Message is sent to us.
            HREdeliverMessage(msg);
        } else {
            // Message is being returned to us.
            HREmsgReady(msg);
        }
    }
}

static void queue_recv(hre_context_t ctx,hre_msg_t msg){
    int me=HREme(ctx);
    if (me==(int)msg->source) {
        Abort("receive complete on local message");
    } else {
        Debug("receive complete");
        // Complete send remotely.
        /* there are two reasons for not executing msgReady:
            1. the sender must be sent a signal to unblock in case it's waiting.
            2. the callback may refer to memory that is not visible in this process.
               (multi-process only)
        */
        queue_put(ctx,msg,msg->source);
    }
}

static void queue_yield(hre_context_t ctx){
    int dummy=0;
    queue_while(ctx,&dummy);
}

extern int main(int,char**);

static void* thread_main(void*arg){
    hre_context_t context=(hre_context_t)arg;
    HREprocessSet(context);
    HREglobalSet(context);
    set_label("%s(%2d/%2d)",HREappName(),HREme(context),HREpeers(context));
    Debug("calling main...");
    int argc=1;
    char *argv[2]={strdup(HREpathName()),NULL};
    main(argc,argv);
    return NULL;
}

void hre_thread_exit(hre_context_t ctx,int code){
    (void)ctx;(void)code;
    Debug("thread exit");
    pthread_exit(NULL);
}

static void * pthread_shm_get(hre_context_t context,size_t size){
    assert(sizeof(union shm_pointer) == sizeof(uint64_t));
    union shm_pointer shm;
    shm.val=0;
    if (HREme(context)==0){
        shm.ptr=malloc(size);
    }
    HREreduce(context,1,&shm,&shm,UInt64,Max);
    Debug("pthread shared area is %p",shm.ptr);
    return shm.ptr;
}

static const char* pthread_class="Posix threads";

void HREpthreadRun(int threads){
    pthread_attr_t attr[threads];
    pthread_t thr[threads];
    for(int i=0;i<threads;i++){
        if (pthread_attr_init(attr+i)){
            AbortCall("pthread_attr_init %d",i);
        }
        size_t stack_size=32 * 1024 * 1024;
        if (pthread_attr_setstacksize(attr+i,stack_size)){
            AbortCall("pthread_attr_setstacksize[%d] to %lld",i,stack_size);
        }
    }
    struct shared_area* shared=(struct shared_area*)HREmalloc(hre_heap,SHARED_SIZE);
    shared->size=SHARED_SIZE;
    shared->align=SHARED_ALIGN;
    shared->next=sizeof(struct shared_area);
    int remainder=sizeof(struct shared_area)%SHARED_ALIGN;
    if (remainder) {
        shared->next=shared->next+SHARED_ALIGN-remainder;
    }
    hre_region_t region=HREcreateRegion(shared,area_malloc,area_realloc,area_free);

    pthread_mutexattr_init(&shared->mutexattr);
    if (pthread_mutexattr_setpshared(&shared->mutexattr,PTHREAD_PROCESS_PRIVATE)){
        AbortCall("pthread_mutexattr_setpshared");
    }
    pthread_mutex_init(&shared->mutex,&shared->mutexattr);
    pthread_condattr_init(&shared->condattr);
    if (pthread_condattr_setpshared(&shared->condattr,PTHREAD_PROCESS_PRIVATE)){
        AbortCall("pthread_condattr_setpshared");
    }
    struct message_queue *queues=HREmallocZero(region,threads*sizeof(struct message_queue));
    hre_context_t thr_ctx[threads];
    for(int i=0;i<threads;i++){
        pthread_mutex_init(&queues[i].lock,&shared->mutexattr);
        pthread_cond_init(&queues[i].cond,&shared->condattr);
        queues[i].comm=HREmallocZero(region,QUEUE_SIZE*sizeof(struct msg_queue_s));
        thr_ctx[i]=HREctxCreate(i,threads,pthread_class,sizeof(struct hre_context_s));
        thr_ctx[i]->shared=shared;
        thr_ctx[i]->queues=queues;
        HREsetExit(thr_ctx[i],hre_thread_exit);
        HREyieldSet(thr_ctx[i],queue_yield);
        HREyieldWhileSet(thr_ctx[i],queue_while);
        HREsendSet(thr_ctx[i],queue_send);
        HRErecvSet(thr_ctx[i],queue_recv,HRErecvActive);
        HREmsgRegionSet(thr_ctx[i],region);
        HREshmGetSet(thr_ctx[i],pthread_shm_get);
        HREctxComplete(thr_ctx[i]);
    }
    for(int i=0;i<threads;i++){
        Debug("starting thread %d",i);
        if (pthread_create(thr+i,attr+i,thread_main,thr_ctx[i])) {
            Abort("couldn't create thread %d",i);
        }
    }
    Debug("waiting for threads");
    for(int i=0;i<threads;i++){
        if (pthread_join(thr[i],NULL)) {
            Abort("couldn't join with thread %d",i);
        }
        Debug("joined with thread %d",i);
        pthread_attr_destroy(attr+i);
    }
    HREexit(EXIT_SUCCESS);
}

static void hre_process_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void hre_process_exit(hre_context_t ctx,int code){
    (void)ctx;(void)code;
    exit(code);
}

static const char* process_class="HRE multi-process";

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

void* hre_posix_shm_get(hre_context_t context,size_t size){
    assert(sizeof(union shm_pointer) == sizeof(uint64_t));
    Debug("Creating posix SHM");
    union shm_pointer template[4]={{.val=0},{.val=0},{.val=0},{.val=0}};
    void* shm=NULL;
    if (HREme(context)==0){
        strcpy((char*)template,"HREprocessXXXXXX");
        char* shm_name=mktemp((char*)template);;
        Debug("name is %s",shm_name);
        int fd=shm_open(shm_name,O_CREAT|O_RDWR,FILE_MODE);
        if(fd==-1){
            AbortCall("shm_open");
        }
        if(ftruncate(fd, size)==-1){
            shm_unlink(shm_name);
            AbortCall("ftruncate");
        }
        shm=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(shm==MAP_FAILED){
            shm_unlink(shm_name);
            AbortCall("mmap");
        }
        Debug("open shared memory %s at %llx",shm_name,shm);
        template[3].ptr=shm;
    }
    HREreduce(context,4,template,template,UInt64,Max);
    if (HREme(context)!=0){
        shm=template[3].ptr;
        Debug("trying shared memory %s at %p",(char*)template,shm);
        int fd=shm_open((char*)template,O_RDWR,FILE_MODE);
        if(fd==-1){
            AbortCall("shm_open");
        }
        void*tmp=mmap(shm,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
        if(tmp==MAP_FAILED){
            shm_unlink((char*)template);
            AbortCall("mmap");
        }
        if (tmp!=shm) Abort("OS did not respect MAP_FIXED");
    }
    HREbarrier(context);
    shm_unlink((char*)template);
    return shm;
}


static int fork_started=0;
static int fork_count;
static struct hre_runtime_s fork_runtime;

static void fork_popt(poptContext con,
                     enum poptCallbackReason reason,
                     const struct poptOption * opt,
                     const char * arg, void * data);

static struct poptOption fork_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)fork_popt , 0 , NULL ,NULL },
    { "procs" , 0 , POPT_ARG_INT|POPT_ARGFLAG_OPTIONAL , NULL , 0 ,
      "number of processes to fork" , "<process count>" },
    POPT_TABLEEND
};

static void fork_popt(poptContext con,
                     enum poptCallbackReason reason,
                     const struct poptOption * opt,
                     const char * arg, void * data){
    (void)con;(void)data;
    if(!fork_started) switch(reason){
        case POPT_CALLBACK_REASON_PRE:
        case POPT_CALLBACK_REASON_POST:
            Abort("unexpected call to hre_popt");
        case POPT_CALLBACK_REASON_OPTION:
            if (!strcmp(opt->longName,"procs")){
                fork_runtime.selected=1;
                if (arg) {
                    fork_count=atoi(arg);
                    if (fork_count<=0) {
                        Abort("less than one process is impossible!");
                    }
                }
                return;
            }
            Abort("unimplemented option: %s",opt->longName);
            exit(EXIT_FAILURE);
    }
}

static void fork_start(int* argc,char **argv[],int run_threads){
    if (run_threads){
        Abort("multi-process and threads are incompatible");
    }
    int procs=fork_count;
    int children=0;
    int success=1;
    int kill_sent=0;
    pid_t pid[procs];
    for(int i=0;i<procs;i++) pid[i]=0;
    char shm_template[24]="HREprocessXXXXXX";
    char* shm_name=mktemp(shm_template);
    int fd=shm_open(shm_name,O_CREAT|O_RDWR,FILE_MODE);
    if(fd==-1){
        AbortCall("shm_open");
    }
    if(ftruncate(fd, SHARED_SIZE)==-1){
        shm_unlink(shm_name);
        AbortCall("ftruncate");
    }
    struct shared_area* shared=(struct shared_area*)mmap(NULL,SHARED_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(shared==MAP_FAILED){
        shm_unlink(shm_name);
        AbortCall("mmap");
    }
    shared->size=SHARED_SIZE;
    shared->align=SHARED_ALIGN;
    shared->next=sizeof(struct shared_area);
    int remainder=sizeof(struct shared_area)%SHARED_ALIGN;
    if (remainder) {
        shared->next=shared->next+SHARED_ALIGN-remainder;
    }
    hre_region_t region=HREcreateRegion(shared,area_malloc,area_realloc,area_free);

    pthread_mutexattr_init(&shared->mutexattr);
    if (pthread_mutexattr_setpshared(&shared->mutexattr,PTHREAD_PROCESS_SHARED)){
        AbortCall("pthread_mutexattr_setpshared");
    }
    pthread_mutex_init(&shared->mutex,&shared->mutexattr);
    pthread_condattr_init(&shared->condattr);
    if (pthread_condattr_setpshared(&shared->condattr,PTHREAD_PROCESS_SHARED)){
        AbortCall("pthread_condattr_setpshared");
    }
    struct message_queue *queues=HREmallocZero(region,procs*sizeof(struct message_queue));
    for(int i=0;i<procs;i++){
        pthread_mutex_init(&queues[i].lock,&shared->mutexattr);
        pthread_cond_init(&queues[i].cond,&shared->condattr);
        queues[i].comm=HREmallocZero(region,QUEUE_SIZE*sizeof(struct msg_queue_s));
    }
    for(int i=0;i<procs;i++){
        pid[i]=fork();
        if (pid[i]==-1) {
            PrintCall(error,"fork");
            success=0;
            break;
        }
        if (pid[i]==0) {
            Debug("forked process %d/%d",i,procs);
            set_label("%s(%2d/%2d)",strdup(get_label()),i,procs);
            hre_context_t hre_ctx=HREctxCreate(i,procs,process_class,sizeof(struct hre_context_s));
            hre_ctx->shared=shared;
            hre_ctx->queues=queues;
            HREyieldSet(hre_ctx,queue_yield);
            HREyieldWhileSet(hre_ctx,queue_while);
            HREsendSet(hre_ctx,queue_send);
            HRErecvSet(hre_ctx,queue_recv,HRErecvActive);
            HREmsgRegionSet(hre_ctx,region);
            HREsetExit(hre_ctx,hre_process_exit);
            HREshmGetSet(hre_ctx,hre_posix_shm_get);
            HREctxComplete(hre_ctx);
            HREmainSet(hre_ctx);
            HREglobalSet(hre_ctx);
            return;
        }
        children++;
    }
    while(children>0){
        // If a failure occurred then we need to shut down all children.
        if (!success && !kill_sent) {
            for(int i=0;i<procs;i++){
                if (pid[i]) {
                    Debug("killing child %d",i);
                    kill(pid[i],SIGKILL);
                }
            }
            kill_sent=1;
        }
        int status;
        pid_t res=wait(&status);
        if (res==-1 ) {
            PrintCall(error,"wait");
            success=0;
        } else {
            int i;
            for(i=0;i<procs;i++){
                if (pid[i]==res) {
                    break;
                }
            }
            if (i==procs) {
                Debug("child was not one of the workers");
                continue;
            }
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                Debug("child %d terminated",i);
                pid[i]=0;
                children--;
                if (WEXITSTATUS(status) || WIFSIGNALED(status)) {
                    success=0;
                }
            }
        }
    }
    Debug("last child terminated");
    shm_unlink(shm_name);
    if (success) {
        HREexit(EXIT_SUCCESS);
    } else {
        HREexit(EXIT_FAILURE);
    }
    (void)argc;(void)argv;
}

void HREenableFork(int procs){
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    int res=pthread_condattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
    pthread_condattr_destroy(&attr);
    if (res){
        Warning(infoLong,"multi-process disabled: inter process locks are not supported");
        return;
    }
    fork_runtime.start=fork_start;
    fork_runtime.options=fork_options;
    fork_count=procs;
    HREregisterRuntime(&fork_runtime);
}
