// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>


#include <hre/provider.h>

#ifndef SIGKILL
# define SIGKILL (-1)
#endif
#ifndef SIGSTOP
# define SIGSTOP (-1)
#endif

/**
 * For pthreads we only use a small shared region for the queues.
 * For forks all shared objects are allocated on the region, so we try to claim
 * as much memory as possible.
 */
static size_t PTHREAD_SHARED_SIZE = 8388608;
static size_t SHARED_ALIGN = sizeof(size_t);
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

typedef struct shm_s {
    size_t id;
    void *ptr;
} shm_t;

static void* area_malloc(void* ptr,size_t size){
    struct shared_area* area=(struct shared_area*)ptr;
    pthread_mutex_lock(&area->mutex);
    if (area->next+size > area->size) Abort("area memory exhausted");
    size_t tmp = area->next;
    size_t old_size = size;
    void *res = ((void *)area) + area->next;
    // modify size to a multiple of ALIGN, so that next will be aligned again:
    size_t remainder = size % area->align;
    if (remainder)
        size = size + area->align - remainder;
    area->next = area->next + size;
    Debug("allocated %zu from %zu to %zu",old_size,tmp,tmp+size);
    (void) old_size; (void) tmp;
    pthread_mutex_unlock(&area->mutex);
    return res;
}

static void* area_align(void* ptr,size_t align,size_t size){
    struct shared_area* area=(struct shared_area*)ptr;
    void*res=((void*)area)+area->next;
    size_t pp = (size_t)res;
    if ((pp / align) * align != pp) {
        // manual alignment only if needed
        res=area_malloc (ptr, size + align);
        size_t pp = (size_t)res;
        res = (void*) ((pp / align + 1) * align);
    } else {
        res=area_malloc (ptr, size);
    }
    Debug("allocated %zu from %p to %p at alignment %zu",size,res,res+size,align);
    return res;
}

static void* area_realloc(void* area,void *rt_ptr, size_t size){
    void *res = area_malloc(area, size);
    if (rt_ptr)
        memmove(res, rt_ptr, size); // over estimation
    Debug("Reallocating %zu from %p to %p", size, rt_ptr, res);
    return res;
}

static void area_free(void*area,void *rt_ptr){
    (void)area;
    (void)rt_ptr;
    Debug("Freeing %p", rt_ptr);
}

static void queue_put(hre_context_t context,hre_msg_t msg,int queue){
    Debug("enqueue message %p",msg);
    if (msg->comm >= QUEUE_SIZE) Abort("number of communicators exceeds maximum of %d",QUEUE_SIZE);
    pthread_mutex_lock(&context->queues[queue].lock);
    hre_put_msg(&context->queues[queue].comm[msg->comm],msg);
    pthread_cond_broadcast(&context->queues[queue].cond);
    pthread_mutex_unlock(&context->queues[queue].lock);
}

static void queue_send(hre_context_t context,hre_msg_t msg){
    Debug("sending message %p",msg);
    queue_put(context,msg,msg->target);
}

static void queue_while(hre_context_t ctx,int*condition){
    int me=HREme(ctx);
    hre_msg_t msg=NULL;
    int max_comm=array_size(HREcommManager(ctx));
    int comm;
    for(;;){
        pthread_mutex_lock(&ctx->queues[me].lock);
        Debug("scanning queue %d below %d",me,max_comm);
        for(comm=0;comm<max_comm;comm++){
            msg=hre_get_msg(&ctx->queues[me].comm[comm]);
            if (msg) break;
        }
        while(!msg && *condition) {
            Debug("blocking %d",*condition);
            pthread_cond_wait(&ctx->queues[me].cond, &ctx->queues[me].lock);
            Debug("scanning queue %d below %d",me,max_comm);
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
            Debug("delivering message %p",msg);
            // Message is sent to us.
            HREdeliverMessage(msg);
        } else {
            Debug("completed message %p",msg);
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

static void queue_cond_signal(hre_context_t ctx, int id){
    pthread_mutex_lock(&(ctx->queues[id].lock));
    pthread_cond_signal(&(ctx->queues[id].cond));
    pthread_mutex_unlock(&(ctx->queues[id].lock));
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

void hre_thread_exit(hre_context_t ctx, int code){
    (void) ctx;
    Debug("thread exit(%d)", code);
    intptr_t c = code;
    pthread_exit ((void *) c);
    exit(code); // avoid warning in cygwin
}

static void *
alloc_region(size_t size, bool public)
{
    void *res;
    if (public) {
        res = mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANON,-1,0);
        if (res == MAP_FAILED) res = NULL;
    } else {
        res = calloc((size + CACHE_LINE_SIZE - 1) >> CACHE_LINE, CACHE_LINE_SIZE);
    }
    return res;
}

static void * pthread_shm_get(hre_context_t context,size_t size){
    shm_t shm;
    shm.id = 0;
    if (HREme(context)==0){
        shm.ptr = alloc_region(size, false);
        if (shm.ptr == NULL) AbortCall("calloc");
    }
    HREreduce(context,2,&shm,&shm,Pointer,Max);
    Debug("pthread shared area is %p",shm.ptr);
    return shm.ptr;
}

static const char* pthread_class="Posix threads";

static struct shared_area *
create_shared_region(size_t size, bool public)
{
    size_t real = size;
    struct shared_area *shared = alloc_region(size, public);
    while (shared == NULL) {
        size/=2;
        HREassert (size >> CACHE_LINE, "Could not allocate any space for defailt memory region");
        shared = alloc_region(size, public);
    }
    if (size != real) {
        Warning (info, "=============================================================================");
        Warning (info, "Runtime environment could only preallocate %zu GB while requesting %zu GB.", size >> 30, real >> 30);
        Warning (info, "        Configure your system limits to exploit all memory.");
        Warning (info, "=============================================================================");
    }
    // region needs to be allocated with calloc, we assume it is zero'd
    shared->size=size;
    shared->align=SHARED_ALIGN;
    shared->next=sizeof(struct shared_area);
    size_t res = ((size_t)shared) + shared->next;
    size_t remainder = res % SHARED_ALIGN;
    if (remainder) {
        shared->next = shared->next + SHARED_ALIGN - remainder;
    }
    int flag = public ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE;
    pthread_mutexattr_init(&shared->mutexattr);
    if (pthread_mutexattr_setpshared(&shared->mutexattr, flag)){
        AbortCall("pthread_mutexattr_setpshared");
    }
    pthread_mutex_init(&shared->mutex,&shared->mutexattr);
    pthread_condattr_init(&shared->condattr);
    if (pthread_condattr_setpshared(&shared->condattr, flag)){
        AbortCall("pthread_condattr_setpshared");
    }
    return shared;
}

pthread_attr_t
set_thread_stack_size()
{
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)){
        AbortCall("pthread_attr_init");
    }
    size_t stack_size = 128 * 1024 * 1024;
    if (pthread_attr_setstacksize(&attr, stack_size)){
        AbortCall("pthread_attr_setstacksize to %zu",stack_size);
    }
    return attr;
}

void HREpthreadRun(int threads){
    pthread_t thr[threads];
    /* Caused huge performance regression in MC tool */
    //pthread_attr_t attr = set_thread_stack_size();
    struct shared_area *shared = create_shared_region(PTHREAD_SHARED_SIZE*threads,false);
    hre_region_t region=HREcreateRegion(shared,area_malloc,area_align,area_realloc,area_free,area_free);

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
        HREcondSignalSet(thr_ctx[i], queue_cond_signal);
        HREsendSet(thr_ctx[i],queue_send);
        HRErecvSet(thr_ctx[i],queue_recv,HRErecvActive);
        HREmsgRegionSet(thr_ctx[i],region);
        HREshmGetSet(thr_ctx[i],pthread_shm_get);
        HREctxComplete(thr_ctx[i]);
    }
    for(int i=0;i<threads;i++){
        Debug("starting thread %d",i);
        if (pthread_create(thr+i,NULL,thread_main,thr_ctx[i])) {
            Abort("couldn't create thread %d",i);
        }
    }
    Debug("waiting for threads");
    int code = HRE_EXIT_SUCCESS;
    for(int i=0;i<threads;i++){
        intptr_t c;
        if (pthread_join(thr[i], (void **)&c)) {
            Abort("couldn't join with thread %d",i);
        }
        if (c != HRE_EXIT_SUCCESS) code = c;
        Debug("joined with thread %d",i);
        //pthread_attr_destroy(attr+i);
    }
    HREexit(code);
}

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

#ifdef HAVE_SHM_OPEN

static size_t           name_counter = 0;

static const char      *process_class="HRE multi-process";

void* hre_posix_shm_get(hre_context_t context,size_t size){
    Debug("Creating posix SHM");
    char                shm_name[LTSMIN_PATHNAME_MAX];
    shm_t               shm = { .ptr = 0, .id = 0 };

    // The first worker sets up the shared memory
    if (HREme(context)==0){
        if (name_counter == 0) {
            struct timeval time;
            gettimeofday(&time, NULL);
            name_counter = time.tv_sec * time.tv_usec;
        } else {
            name_counter++;
        }
        shm.id = name_counter;
        snprintf(shm_name, LTSMIN_PATHNAME_MAX, "HREprocess%zu", name_counter);
        Debug("name is %s",shm_name);
        int fd=shm_open(shm_name,O_CREAT|O_RDWR,FILE_MODE);
        if (fd == -1) {
            AbortCall("shm_open");
        }
        if (ftruncate(fd, size)==-1) {
            shm_unlink(shm_name);
            AbortCall("ftruncate");
        }
        shm.ptr = mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if (shm.ptr == MAP_FAILED) {
            shm_unlink(shm_name);
            AbortCall("mmap");
        }
        Debug("open shared memory %s at %p",shm_name,shm.ptr);
    }

    // Share the data
    HREreduce(context,2,&shm,&shm,Pointer,Max);

    // The other workers attempt to map the shared memory as well
    if (HREme(context)!=0) {
        snprintf(shm_name, LTSMIN_PATHNAME_MAX, "HREprocess%zu", shm.id);
        Debug("trying shared memory %s at %p", shm_name, shm.ptr);
        int fd=shm_open(shm_name,O_RDWR,FILE_MODE);
        if (fd == -1) {
            AbortCall("shm_open");
        }
        void *tmp=mmap(shm.ptr,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,fd,0);
        if (tmp == MAP_FAILED) {
            shm_unlink(shm_name);
            AbortCall("mmap");
        }
        HREassert(tmp == shm.ptr, "OS did not respect MAP_FIXED");
    }

    // Synchronize
    HREbarrier(context);
    shm_unlink(shm_name);
    return shm.ptr;
}

#else

void* hre_posix_shm_get(hre_context_t context,size_t size){
    (void) context; (void) size;
    Abort("no shm");
}

#endif

void* hre_privatefixedmem_get(hre_context_t context, size_t size){
    Debug("Creating Private Memory at Fixed location");
    void* fixedMemory = NULL;

    // The first worker sets up the anonymous private memory
    if (HREme(context)==0){
        fixedMemory = mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        if (fixedMemory == MAP_FAILED || fixedMemory == (void *)-1) {
            AbortCall("mmap");
        }
        Debug("open private fixed memory at %p",fixedMemory);
    }
    
    // Share the data
    HREreduce(context, 1, &fixedMemory, &fixedMemory, Pointer, Max);

    // The other workers attempt to map anoymous private memory to the same address
    if (HREme(context)!=0) {
        Debug("trying private fixed memory at %p", fixedMemory);
        void* tmp = mmap(fixedMemory,size,PROT_READ|PROT_WRITE,MAP_FIXED|MAP_PRIVATE|MAP_ANON,-1,0);
        if (tmp == MAP_FAILED || tmp == (void *)-1) {
            AbortCall("mmap");
        }
        HREassert(tmp == fixedMemory, "OS did not respect MAP_FIXED");
    }

    // Synchronize
    HREbarrier(context);
    return fixedMemory;
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
            break;
        case POPT_CALLBACK_REASON_OPTION:
            if (!strcmp(opt->longName,"procs")){
                if (arg) {
                    fork_count=atoi(arg);
                    if (fork_count < 1) {
                        Abort("less than one process is impossible!");
                    }
                    fork_runtime.selected = fork_count > 1;
                }
                return;
            }
            Abort("unimplemented option: %s",opt->longName);
            exit(HRE_EXIT_FAILURE);
    }
}

#ifdef HAVE_FORK

static void hre_process_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void hre_process_exit(hre_context_t ctx,int code){
    (void)ctx;
    exit(code);
}

static void
handle_signal (int sig)
{
    // nada
    (void) sig;
}

static void fork_start(int* argc,char **argv[],int run_threads){
    if (run_threads){
        Abort("multi-process and threads are incompatible");
    }
    int procs=fork_count;
    int children=0;
    int success=1;
    int code = 0;
    int kill_sent=0;
    pid_t pid[procs];
    for(int i=0;i<procs;i++) pid[i]=0;

    // this area is mmapped before the fork, so no shm_open is required
    struct shared_area* shared = create_shared_region(RTmemSize()*16, true);
    hre_region_t region=HREcreateRegion(shared,area_malloc,area_align,area_realloc,area_free,area_free);

    struct message_queue *queues=HREmallocZero(region,procs*sizeof(struct message_queue));
    for(int i=0;i<procs;i++){
        pthread_mutex_init(&queues[i].lock,&shared->mutexattr);
        pthread_cond_init(&queues[i].cond,&shared->condattr);
        queues[i].comm=HREmallocZero(region,QUEUE_SIZE*sizeof(struct msg_queue_s));
    }
    for(int i=0;i<procs;i++){
        pid[i]=fork();
        if (pid[i]==-1) {
            PrintCall(lerror,"fork");
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
            HREcondSignalSet(hre_ctx, queue_cond_signal);
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
    signal (SIGINT, handle_signal); // avoid exit on ctrl + c
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
        Debug("process %d received %d,%d (exit: %d, signal: %d)", res, WEXITSTATUS(status), status, WIFEXITED(status), WIFSIGNALED(status));
        if (res==-1 ) {
            PrintCall(lerror,"wait");
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
                if (WEXITSTATUS(status)) {
                    if (success) code = WEXITSTATUS(status);
                    success=0;
                } else if (WIFSIGNALED(status)) {
                    if (success) code = 128+WTERMSIG(status);
                    success=0;
                }
            }
        }
    }
    Debug("last child terminated");
    if (success) {
        HREexit(HRE_EXIT_SUCCESS);
    } else {
        HREexit(code);
    }
    (void)argc;(void)argv;
}

#else

static void fork_start(int* argc,char **argv[],int run_threads){
    (void) argc; (void) argv; (void) run_threads;
    Abort("No multi process");
}

#endif

void HREenableFork(int procs, bool selected){
    Debug("Enabling process runtime environment.");
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
    fork_runtime.selected = selected && procs > 1;
    fork_count=procs;
    HREregisterRuntime(&fork_runtime);
}

size_t
HREgetRegionSize(hre_region_t region)
{
    struct shared_area *area = HREgetArea(region);
    return area->size;
}

typedef struct hre_local_s {
    void               *ptr;
    char               *pad[CACHE_LINE_SIZE - sizeof(void *)];
} hre_local_t;

void
HREcreateLocal(hre_key_t *key, void (*destructor)(void *))
{
    hre_local_t       **local = (hre_local_t **)key;
    size_t              workers = HREpeers (HREglobal());
    hre_region_t        region = HREdefaultRegion (HREglobal());
    *local = HREalign (region, CACHE_LINE_SIZE, sizeof(hre_local_t[workers]));
    for (size_t i = 0; i < workers; i++)
        (*local)[i].ptr = NULL;
    (void) destructor; // TODO: deallocation
}

void
HREsetLocal(hre_key_t key, void *package)
{
    size_t              id = HREme (HREglobal());
    hre_local_t        *local = (hre_local_t *)key;
    local[id].ptr = package;
}

void *
HREgetLocal(hre_key_t key)
{
    size_t              id = HREme (HREglobal());
    hre_local_t        *local = (hre_local_t *)key;
    return local[id].ptr;
}
