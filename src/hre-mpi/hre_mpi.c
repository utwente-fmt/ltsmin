// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fcntl.h>
#include <limits.h>
#include <mpi.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hre-mpi/user.h>
#include <hre-mpi/mpi_event_loop.h>
#include <hre/provider.h>
#include <util-lib/dynamic-array.h>

typedef struct mpi_shared_s {
    pthread_mutex_t mutex;
    array_manager_t comm_man;
    MPI_Comm comm;
    MPI_Comm *comm_array;
    struct mpi_addr {
        int host;
        int thread;
    } *addr;
    int max_threads;
} *mpi_shared_t;

struct hre_context_s {
    MPI_Comm comm;
    event_queue_t mpi_queue;
    MPI_Comm *action_comm;
    mpi_shared_t shared;
};

union mpi_pointer {
    uint64_t val;
    mpi_shared_t ptr;
};

void HREenableAllMPI(){
    HREenableStandard();
    HREenableMPI();
}

void mpi_send_ready(void* context,MPI_Status *status){
    hre_msg_t msg=(hre_msg_t)context;
    Debug("completed send %d -> %d on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    HREmsgReady(msg);
    (void)status;
}

static void mpi_send(hre_context_t context,hre_msg_t msg){
    Debug("posting send %d -> %d on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    event_Isend(context->mpi_queue,msg->buffer,msg->tail,MPI_CHAR,msg->target,msg->tag,
        context->action_comm[msg->comm],mpi_send_ready,msg);
}

static void mpi_thread_send(hre_context_t context,hre_msg_t msg){
    Debug("posting send %d -> %d on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    HREassert(msg->source==(unsigned int)HREme(context), "Caller should be sender");
    HREassert(msg->target!=(unsigned int)HREme(context), "Caller cannot be receiver and sender");
    int host=context->shared->addr[msg->target].host;
    int tag=msg->tag*context->shared->max_threads*context->shared->max_threads;
    tag=tag+context->shared->addr[msg->source].thread*context->shared->max_threads;
    tag=tag+context->shared->addr[msg->target].thread;
    Debug("translation is %d tag %d",host,tag);
    event_Isend(context->mpi_queue,msg->buffer,msg->tail,MPI_CHAR,host,tag,
        context->action_comm[msg->comm],mpi_send_ready,msg);
}

void mpi_recv_ready(void* context,MPI_Status *status){
    hre_msg_t msg=(hre_msg_t)context;
    Debug("completed recv %d -> %d on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    MPI_Get_count(status,MPI_CHAR,(int*)&msg->tail);
    HREdeliverMessage(msg);
}

static void mpi_recv(hre_context_t context,hre_msg_t msg){
    event_Irecv(context->mpi_queue,msg->buffer,msg->size,MPI_CHAR,msg->source,msg->tag,
        context->action_comm[msg->comm],mpi_recv_ready,msg);
}

void mpi_thread_recv_ready(void* context,MPI_Status *status){
    hre_msg_t msg=(hre_msg_t)context;
    Debug("completed recv %d -> %d on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    MPI_Get_count(status,MPI_CHAR,(int*)&msg->tail);
    HREdeliverMessage(msg);
}

static void mpi_thread_recv(hre_context_t context,hre_msg_t msg){
    Debug("posting %d -> %d recv on comm %d tag %d (%p)",msg->source,msg->target,msg->comm,msg->tag,msg);
    HREassert(msg->source==(unsigned int)HREme(context), "Caller should be receiver");
    HREassert(msg->target!=(unsigned int)HREme(context), "Caller cannot be receiver and sender");
    int source=context->shared->addr[msg->source].host;
    int tag=msg->tag*context->shared->max_threads*context->shared->max_threads;
    tag=tag+context->shared->addr[msg->source].thread*context->shared->max_threads;
    tag=tag+context->shared->addr[msg->target].thread;
    Debug("translation is %d tag %d",source,tag);
    event_Irecv(context->mpi_queue,msg->buffer,msg->size,MPI_CHAR,source,tag,
        context->action_comm[msg->comm],mpi_thread_recv_ready,msg);
}

static void * mpi_shm_get(hre_context_t context,size_t size){
    (void)context; (void)size;
    Abort("MPI processes not all local");
}

static int mpi_force=0;
static char* hre_mpirun=NULL;
static char mpirun_workers[16];
static int mpi_started=0;
static struct hre_runtime_s mpi_runtime;

static void hre_popt(poptContext con,
                     enum poptCallbackReason reason,
                     const struct poptOption * opt,
                     const char * arg, void * data){
    (void)con;(void)data;
    if(!mpi_started) switch(reason){
        case POPT_CALLBACK_REASON_PRE:
        case POPT_CALLBACK_REASON_POST:
            Abort("unexpected call to hre_popt");
        case POPT_CALLBACK_REASON_OPTION:
            if (!strcmp(opt->longName,"mpi")) {
                mpi_runtime.selected=1;
                mpi_force=1;
                return;
            }
            if (!strcmp(opt->longName,"mpirun")) {
                if (hre_mpirun) {
                    Abort("mpirun already set.");
                }
                if (!arg) {
                    Abort("--mpirun needs an argument");
                }
                hre_mpirun=strdup(arg);
                mpi_runtime.selected=1;
                return;
            }
            if (!strcmp(opt->longName,"workers")){
                if (hre_mpirun) {
                    Abort("mpirun already set.");
                }
                if (!arg) {
                    Abort("--workers needs an argument");
                }
                int workers=atoi(arg);
                if (workers<=0) {
                    Abort("less than one worker is impossible!");
                }
                sprintf(mpirun_workers,"-np %d",workers);
                hre_mpirun=mpirun_workers;
                mpi_runtime.selected=1;
                return;
            }
            Abort("unimplemented option: %s",opt->longName);
            exit(HRE_EXIT_FAILURE);
    }
}

struct poptOption mpi_options[]={
    { NULL, 0 , POPT_ARG_CALLBACK , (void*)hre_popt , 0 , NULL ,NULL },
    { "mpirun" , 0 , POPT_ARG_STRING , NULL , 0 ,
      "execute mpirun" , "<mpirun argument>" },
    { "workers" , 0 , POPT_ARG_INT , NULL , 0 ,
      "number of MPI workers to be started, equivalent to --mpirun=\"-np <worker count>\"","<worker count>"},
    { "mpi" , 0 , POPT_ARG_VAL , NULL , 1 ,
      "Enable MPI during execution, if this option is present then both --workers and --mpirun are ignored",NULL},
    POPT_TABLEEND
};

static void mpi_start(int*argc_p,char**argv_p[],int run_threads);
static void mpi_start_thread();

void HREenableMPI(){
    mpi_runtime.start=mpi_start;
    mpi_runtime.start_thread=mpi_start_thread;
    mpi_runtime.options=mpi_options;
    HREregisterRuntime(&mpi_runtime);
}

void HREselectMPI(){
    HREenableMPI();
    mpi_runtime.selected=1;
}

static void mpi_abort(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void mpi_abort(hre_context_t ctx,int code){
    MPI_Abort(ctx->comm,code);
    exit(HRE_EXIT_FAILURE);
}

static void mpi_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void mpi_exit(hre_context_t ctx,int code){
    (void)ctx;
    MPI_Finalize();
    if (code) {
        exit(HRE_EXIT_FAILURE);
    } else {
        exit(HRE_EXIT_SUCCESS);
    }
}

static void HREmpirun(int argc,char*argv[],const char *mpi_args){
    int mpi_argc=0;
    char**mpi_argv=NULL;
    if (strlen(mpi_args)){
        int res=poptParseArgvString(mpi_args,&mpi_argc,(const char***)&mpi_argv);
        if (res){
            Abort("could not parse %s: %s",mpi_args,poptStrerror(res));
        }
    }
    char*newargv[argc+3+mpi_argc];
    newargv[0]="mpirun";
    for(int i=0;i<mpi_argc;i++) newargv[i+1]=mpi_argv[i];
    for(int i=0;i<argc;i++) newargv[i+1+mpi_argc]=argv[i];
    newargv[argc+1+mpi_argc]="--mpi";
    newargv[argc+2+mpi_argc]=NULL;
    execvp("mpirun",newargv);
    AbortCall("execvp");
}

static void hre_event_yield(hre_context_t ctx){
    event_yield(ctx->mpi_queue);
}

static void hre_event_while(hre_context_t ctx,int*condition){
    event_while(ctx->mpi_queue,condition);
}

static const char* class_name="HRE MPI";

static void action_comm_thread_resize(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    (void)old_array;
    (void)new_array;
    if (new_size<old_size) Abort("unimplemented");
    hre_context_t ctx=(hre_context_t)arg;
    pthread_mutex_lock(&ctx->shared->mutex);
    for(int i=old_size;i<new_size;i++){
        ensure_access(ctx->shared->comm_man,i);
        if (ctx->shared->comm_array[i]) {
            Debug("copying comm %d",i);
        } else {
            Debug("duplicating comm %d",i);
            MPI_Comm_dup(ctx->comm,ctx->shared->comm_array+i);
            Debug("duplication comm %d OK",i);
        }
        ctx->action_comm[i]=ctx->shared->comm_array[i];
    }
    pthread_mutex_unlock(&ctx->shared->mutex);
}

static void action_comm_resize(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    (void)old_array;
    if (new_size<old_size) Abort("unimplemented");
    hre_context_t ctx=(hre_context_t)arg;
    MPI_Comm* comm_array=(MPI_Comm*)new_array;
    for(int i=old_size;i<new_size;i++){
        Debug("duplicating comm %d",i);
        MPI_Comm_dup(ctx->comm,comm_array+i);
        Debug("duplication comm %d OK",i);
    }
}

static int single_host(MPI_Comm comm){
    int me,peers;
    MPI_Comm_size(comm, &peers);
    MPI_Comm_rank(comm, &me);
    char name[MPI_MAX_PROCESSOR_NAME];
    char first[MPI_MAX_PROCESSOR_NAME];
    int len;
    MPI_Get_processor_name(name,&len);
    if (me==0) memcpy(first,name,MPI_MAX_PROCESSOR_NAME);
    MPI_Bcast(first,MPI_MAX_PROCESSOR_NAME, MPI_CHAR, 0 , comm );
    int same=(strcmp(name,first)==0)?1:0;
    int shm;
    MPI_Allreduce(&same,&shm,1,MPI_INT,MPI_MIN,comm);
    if (me==0) {
        if (shm) {
            Debug("single host");
        } else {
            Debug("multiple hosts");
        }
    }
    return shm;
}

static hre_context_t HREctxMPIshared(MPI_Comm parent,hre_context_t local){
    int local_me,local_peers,main_me,main_peers,global_me=0,global_peers;
    local_me=HREme(local);
    local_peers=HREpeers(local);
    MPI_Comm_rank(parent,&main_me);
    MPI_Comm_size(parent,&main_peers);
    mpi_shared_t shared;
    if (local_me==0){
        Debug("using %d threads",local_peers);
        int thread_count[main_peers];
        MPI_Allgather(&local_peers,1,MPI_INT,thread_count,1,MPI_INT,MPI_COMM_WORLD);
        global_peers=0;
        for(int i=0;i<main_peers;i++){
            if (main_me==i) global_me=global_peers;
            global_peers+=thread_count[i];
        }
        shared=RT_NEW(struct mpi_shared_s);
        pthread_mutex_init(&shared->mutex, NULL);
        MPI_Comm_dup(parent,&shared->comm);
        shared->comm_man=create_manager(1);
        shared->comm_array=NULL;
        ADD_ARRAY(shared->comm_man,shared->comm_array,MPI_Comm);
        shared->addr=(struct mpi_addr*)RTmalloc(global_peers*sizeof(struct mpi_addr));
        int idx=0;
        shared->max_threads=0;
        for(int i=0;i<main_peers;i++){
            if (thread_count[i]>shared->max_threads) {
                shared->max_threads=thread_count[i];
            }
            for(int j=0;j<thread_count[i];j++){
                shared->addr[idx].host=i;
                shared->addr[idx].thread=j;
                idx++;
            }
        }
    }
    HREassert(sizeof(union mpi_pointer) == sizeof(uint64_t), "Expected 64 bit pointer");
    union mpi_pointer temp[4]={{.val=0},{.val=0},{.val=0},{.val=0}};
    if (local_me==0){
        temp[0].val=global_peers;
        temp[1].val=global_me;
        temp[2].ptr=shared;
        temp[3].val=single_host(parent);
        HREreduce(local,4,temp,temp,UInt64,Max);
    } else {
        HREreduce(local,4,temp,temp,UInt64,Max);
        global_peers=(int)temp[0].val;
        global_me=(int)temp[1].val+local_me;
        shared=temp[2].ptr;
    }
    set_label("%s[%2d/%2d](%2d/%2d)",HREappName(),main_me,local_me,global_me,global_peers);
    Debug("identity established");
    hre_context_t ctx=HREctxCreate(global_me,global_peers,class_name,sizeof(struct hre_context_s));
    ctx->comm=shared->comm;
    ctx->shared=shared;
    ctx->mpi_queue=event_queue();
    HREsetAbort(ctx,mpi_abort);
    HREsetExit(ctx,hre_thread_exit);
    HREyieldSet(ctx,hre_event_yield);
    HREyieldWhileSet(ctx,hre_event_while);
    HREsendSet(ctx,mpi_thread_send);
    HRErecvSet(ctx,mpi_thread_recv,HRErecvPassive);
    if (temp[3].val) HREshmGetSet(ctx,hre_posix_shm_get);
    else HREshmGetSet(ctx,mpi_shm_get);
    ctx->action_comm=NULL;
    ADD_ARRAY_CB(HREcommManager(ctx),ctx->action_comm,MPI_Comm,action_comm_thread_resize,ctx);
    HREctxComplete(ctx);
    Debug("context created");
    if (local_me==0){
        MPI_Barrier(parent);
    }
    HREbarrier(local);
    Debug("synchronized return");
    return ctx;
}

static hre_context_t HREctxMPI(MPI_Comm comm){
    int me,peers;
    char label[PATH_MAX];
    MPI_Comm_size(comm, &peers);
    MPI_Comm_rank(comm, &me);
    strncpy(label, get_label(), sizeof(label));
    set_label("%s(%2d/%2d)",label,me,peers);
    int shm=single_host(comm);
    hre_context_t ctx=HREctxCreate(me,peers,class_name,sizeof(struct hre_context_s));
    ctx->comm=comm;
    ctx->mpi_queue=event_queue();
    HREsetAbort(ctx,mpi_abort);
    HREsetExit(ctx,mpi_exit);
    HREyieldSet(ctx,hre_event_yield);
    HREyieldWhileSet(ctx,hre_event_while);
    HREsendSet(ctx,mpi_send);
    HRErecvSet(ctx,mpi_recv,HRErecvPassive);
    if (shm) HREshmGetSet(ctx,hre_posix_shm_get);
    else HREshmGetSet(ctx,mpi_shm_get);
    ctx->action_comm=NULL;
    ADD_ARRAY_CB(HREcommManager(ctx),ctx->action_comm,MPI_Comm,action_comm_resize,ctx);
    HREctxComplete(ctx);
    MPI_Barrier(comm);
    return ctx;
}

event_queue_t HREeventQueue(hre_context_t ctx){
    if (HREclass(ctx)!=class_name) Abort("event queue not defined for %s",HREclass(ctx));
    return ctx->mpi_queue;
}

static void mpi_start_thread(){
    hre_context_t process_ctx=HREprocessGet();
    hre_context_t global_ctx=HREctxMPIshared(MPI_COMM_WORLD,process_ctx);
    HREglobalSet(global_ctx);
}

static void mpi_start(int*argc_p,char**argv_p[],int run_threads){
    if (hre_mpirun && !mpi_force) {
        HREmpirun(*argc_p,*argv_p,hre_mpirun);
    }
    mpi_started=1;
    if (run_threads){
        int provided;
        MPI_Init_thread(argc_p,argv_p,MPI_THREAD_MULTIPLE,&provided);
        switch(provided){
        case MPI_THREAD_SINGLE:
            Abort("Single threaded MPI: thread support impossible");
        case MPI_THREAD_FUNNELED:
            Abort("Funneled thread support provided: thread support unimplemented");
        case MPI_THREAD_SERIALIZED:
            Abort("Serialized thread support provided: thread support unimplemented");
        case MPI_THREAD_MULTIPLE:
            break;
        default:
            Abort("unknown thread support provided");
        }
    } else {
        MPI_Init(argc_p,argv_p);
    }
    MPI_Errhandler_set(MPI_COMM_WORLD,MPI_ERRORS_ARE_FATAL);
    hre_context_t ctx=HREctxMPI(MPI_COMM_WORLD);
    HREmainSet(ctx);
    HREglobalSet(ctx);
}

