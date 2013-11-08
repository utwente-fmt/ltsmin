// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <hre-mpi/user.h>
#include <hre-mpi/mpi_event_loop.h>
#include <util-lib/dynamic-array.h>

struct event_queue_s{
    array_manager_t man;
    MPI_Request *request;
    event_callback *cb;
    void* *context;
    int pending;
    long long int wait_some_calls;
    long long int wait_some_none;
    long long int wait_some_multi;
    long long int test_some_calls;
    long long int test_some_none;
    long long int test_some_multi;
};

void event_statistics(log_t log,event_queue_t queue){
    Print(log,"wait : %lld calls, %lld multiple",
        queue->wait_some_calls,queue->wait_some_multi);
    Print(log,"test : %lld calls, %lld none, %lld multiple",
        queue->test_some_calls,queue->test_some_none,queue->test_some_multi);
    Print(log,"queue size is %d",array_size(queue->man));
}

static void null_cb(void* context,MPI_Status *status){
    (void)context;
    (void)status;
}

event_queue_t event_queue(){
    event_queue_t queue=(event_queue_t)HREmalloc(NULL,sizeof(struct event_queue_s));
    queue->man=create_manager(1024);
    queue->request=NULL;
    ADD_ARRAY(queue->man,queue->request,MPI_Request);
    queue->cb=NULL;
    ADD_ARRAY(queue->man,queue->cb,event_callback);
    queue->context=NULL;
    ADD_ARRAY(queue->man,queue->context,void*);
    queue->pending=0;
    queue->wait_some_calls=0;
    queue->wait_some_multi=0;
    queue->test_some_calls=0;
    queue->test_some_none=0;
    queue->test_some_multi=0;
    return queue;
}

void event_queue_destroy(event_queue_t *queue){
    for(int i=0;i<(*queue)->pending;i++){
        MPI_Cancel(&(*queue)->request[i]);
    }
    destroy_manager ((*queue)->man);
    RTfree(*queue);
    *queue=NULL;
}

void event_post(event_queue_t queue,MPI_Request *request,event_callback cb,void*context){
    ensure_access(queue->man,queue->pending);
    queue->request[queue->pending]=*request;
    queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
    queue->context[queue->pending]=context;
    queue->pending++;
}

static void event_loop(event_queue_t queue,int block){
    while(queue->pending){
        Debug("MPI waiting for %d events",queue->pending);
        int index[queue->pending];
        int completed;
        MPI_Status status[queue->pending];
        if (block) {
            Debug("MPI_Waitsome");
            //int res = MPI_Waitsome(queue->pending,queue->request,&completed,index,status);
            int res = MPI_Waitany(queue->pending,queue->request,index,status);
            completed=1;
            Debug("MPI_Waitsome : %d",res);
            if (res != MPI_SUCCESS) Abort("MPI_Waitsome");
            queue->wait_some_calls++;
            if (completed>1) queue->wait_some_multi++;
            block=0;
        } else {
            Debug("MPI_Testsome");
            //int res = MPI_Testsome(queue->pending,queue->request,&completed,index,status);
            int flag;
            int res = MPI_Testany(queue->pending,queue->request,index,&flag,status);
            completed=flag?1:0;
            Debug("MPI_Testsome : %d",res);
            if (res != MPI_SUCCESS) Abort("MPI_Testsome");
            queue->test_some_calls++;
            if (completed==0) {
                queue->test_some_none++;
                Debug("MPI exit event loop");
                return;
            }
            if (completed>1) queue->test_some_multi++;
        }
        Debug("MPI completion of %d events",completed);
        event_callback cb[completed];
        void *ctx[completed];
        for(int i=0;i<completed;i++){
            cb[i]=queue->cb[index[i]];
            queue->cb[index[i]]=NULL;
            ctx[i]=queue->context[index[i]];
        }
        int k=0;
        for(int i=0;i<queue->pending;i++){
            if (queue->cb[i]) {
                if (k<i) {
                    queue->request[k]=queue->request[i];
                    queue->cb[k]=queue->cb[i];
                    queue->context[k]=queue->context[i];
                }
                k++;
            }
        }
        queue->pending=k;
        for(int i=0;i<completed;i++) {
            Debug("MPI call back");
            cb[i](ctx[i],&status[i]);
            Debug("MPI call back done");
        }
    }
    Debug("MPI exit loop");
}

void event_yield(event_queue_t queue){
    event_loop(queue,0);
}

void event_block(event_queue_t queue){
    event_loop(queue,1);
}



void event_while(event_queue_t queue,int *condition){
    while(*condition){
        int index[queue->pending];
        int completed=1;
        MPI_Status status[queue->pending];
        Debug("MPI_Waitsome in while");
        //int res = MPI_Waitsome(queue->pending,queue->request,&completed,index,status);
        int res = MPI_Waitany(queue->pending,queue->request,index,status);
        // The Waitsome version led to deadlocks in case of multiple requests and nested call-backs.
        // To use Waitsome the callback queue must be moved to the queue data structure.
        Debug("MPI_Waitsome : %d/%d",res,completed);
        if (res != MPI_SUCCESS) Abort("MPI_Waitsome");
        queue->wait_some_calls++;
        if (completed>1) queue->wait_some_multi++;
        event_callback cb[completed];
        void *ctx[completed];
        for(int i=0;i<completed;i++){
            cb[i]=queue->cb[index[i]];
            queue->cb[index[i]]=NULL;
            ctx[i]=queue->context[index[i]];
        }
        int k=0;
        for(int i=0;i<queue->pending;i++){
            if (queue->cb[i]) {
                if (k<i) {
                    queue->request[k]=queue->request[i];
                    queue->cb[k]=queue->cb[i];
                    queue->context[k]=queue->context[i];
                }
                k++;
            }
        }
        queue->pending=k;
        for(int i=0;i<completed;i++) {
            Debug("MPI call back");
            cb[i](ctx[i],&status[i]);
            Debug("MPI call back done");
        }
    }
}

struct event_status {
    int pending;
    MPI_Status *status;
};

static void copy_status(void* context,MPI_Status *status){
#define e_stat_ptr ((struct event_status*)context)
    *(e_stat_ptr->status)=*status;
    e_stat_ptr->pending=0;
#undef e_stat_ptr
}

void event_wait(event_queue_t queue,MPI_Request *request,MPI_Status *status){
    struct event_status e_stat;
    e_stat.pending=1;
    e_stat.status=status;
    event_post(queue,request,copy_status,&e_stat);
    event_while(queue,&e_stat.pending);
}

void event_Send(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
    int dest, int tag, MPI_Comm comm){
    MPI_Request request;
    MPI_Status status;
    int res = MPI_Isend(buf,count,datatype,dest,tag,comm,&request);
    if (res != MPI_SUCCESS) Abort("MPI_Isend");
    event_wait(queue,&request,&status);
}

void event_Ssend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
    int dest, int tag, MPI_Comm comm){
    MPI_Request request;
    MPI_Status status;
    int res = MPI_Issend(buf,count,datatype,dest,tag,comm,&request);
    if (res != MPI_SUCCESS) Abort("MPI_Issend");
    event_wait(queue,&request,&status);
}

void event_Isend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
    int dest, int tag, MPI_Comm comm,event_callback cb,void*context){
    ensure_access(queue->man,queue->pending);
    int res = MPI_Isend(buf,count,datatype,dest,tag,comm,&queue->request[queue->pending]);
    if (res != MPI_SUCCESS) Abort("MPI_Isend");
    queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
    queue->context[queue->pending]=context;
    queue->pending++;
}

void event_Issend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
    int dest, int tag, MPI_Comm comm,event_callback cb,void*context){
    ensure_access(queue->man,queue->pending);
    int res = MPI_Issend(buf,count,datatype,dest,tag,comm,&queue->request[queue->pending]);
    if (res != MPI_SUCCESS) Abort("MPI_Issend");
    queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
    queue->context[queue->pending]=context;
    queue->pending++;
}

void event_Recv(event_queue_t queue, void *buf, int count, MPI_Datatype datatype,
            int source, int tag, MPI_Comm comm, MPI_Status *status){
    MPI_Request request;
    int res = MPI_Irecv(buf,count,datatype,source,tag,comm,&request);
    if (res != MPI_SUCCESS) Abort("MPI_Irecv");
    event_wait(queue,&request,status);
}

void event_Irecv(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
    int source, int tag, MPI_Comm comm,event_callback cb,void*context){
    ensure_access(queue->man,queue->pending);
    int res = MPI_Irecv(buf,count,datatype,source,tag,comm,&queue->request[queue->pending]);
    if (res != MPI_SUCCESS) Abort("MPI_Irecv");
    queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
    queue->context[queue->pending]=context;
    queue->pending++;
}

struct idle_detect_s {
    event_queue_t queue;
    MPI_Comm comm;
    int tag;
    int me;
    int nodes;
    int dirty;
    int count;
    int exit_code;
    int msg_pending;
    int term_msg[2];
};

#define RUNNING 0
#define IDLE 1
#define TERMINATED 2

static void idle_receiver(void *context,MPI_Status *status){
#define detect ((idle_detect_t)context)
    (void)status;
    detect->msg_pending--;
    event_Irecv(detect->queue,&detect->term_msg,2,MPI_INT,MPI_ANY_SOURCE,
            detect->tag,detect->comm,idle_receiver,context);
#undef detect
}

idle_detect_t event_idle_create(event_queue_t queue,MPI_Comm comm,int tag){
    idle_detect_t d=(idle_detect_t)HREmalloc(NULL,sizeof(struct idle_detect_s));
    d->queue=queue;
    d->comm=comm;
    d->tag=tag;
    d->dirty=0;
    d->count=0;
    d->exit_code=0;
        MPI_Comm_size(comm,&d->nodes);
        MPI_Comm_rank(comm,&d->me);
    d->msg_pending=(d->me==0)?0:1;
    event_Irecv(queue,&d->term_msg,2,MPI_INT,MPI_ANY_SOURCE,tag,comm,idle_receiver,d);
    return d;
}

void event_idle_set_code(idle_detect_t detector,int code){
    detector->exit_code=code;
}

void event_idle_send(idle_detect_t detector){
    detector->count++;
}

void event_idle_recv(idle_detect_t detector){
    detector->dirty=1;
    detector->count--;
}

int event_idle_detect(idle_detect_t detector){
    if (detector->me==0){
        if (detector->nodes==1){
            if (detector->count) {
                Abort("illegal non-zero count %d",detector->count);
            }
            return detector->exit_code;
        }
        int round=0;
        int term_send[2];
        term_send[0]=IDLE;
        term_send[1]=0;
        for(;;){
            round++;
            detector->dirty=0;
            event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,detector->tag,detector->comm);
            detector->msg_pending++;
            event_while(detector->queue,&detector->msg_pending);
            if (detector->term_msg[0]!=IDLE) {
                continue;
            }
            if (detector->dirty){
                continue;
            }
            if ((detector->term_msg[1]+detector->count)!=0){
                continue;
            }
            term_send[0]=TERMINATED;
            term_send[1]=detector->exit_code;
            event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,
                    detector->tag,detector->comm);
            detector->msg_pending++;
            event_while(detector->queue,&detector->msg_pending);
            return detector->exit_code;
        }
    } else {
        for(;;){
            int term_send[2];
            event_while(detector->queue,&detector->msg_pending);
            if (detector->term_msg[0]==TERMINATED) {
                detector->exit_code=detector->term_msg[1];
                term_send[0]=TERMINATED;
                term_send[1]=detector->term_msg[1];
                event_Send(detector->queue,term_send,2,MPI_INT,detector->me-1,
                        detector->tag,detector->comm);
                detector->msg_pending++;
                return detector->exit_code;
            }
            term_send[0]=detector->dirty?RUNNING:detector->term_msg[0];
            detector->dirty=0;
            term_send[1]=detector->term_msg[1]+detector->count;
            event_Send(detector->queue,term_send,2,MPI_INT,detector->me-1,
                        detector->tag,detector->comm);
            detector->msg_pending++;
        }
    }
}


struct event_barrier_s{
    event_queue_t queue;
    MPI_Comm comm;
    int tag;
    int me;
    int nodes;
    int wait;
    int msg[1];
};

static void barrier_recv(void *context,MPI_Status *status){
#define barrier ((event_barrier_t)context)
    (void)status;
    barrier->wait--;
    if (barrier->wait) event_Irecv(barrier->queue,&barrier->msg,1,
            MPI_INT,MPI_ANY_SOURCE,barrier->tag,barrier->comm,barrier_recv,context);
#undef barrier
}

event_barrier_t event_barrier_create(event_queue_t queue,MPI_Comm comm,int tag){
    event_barrier_t b=(event_barrier_t)HREmalloc(NULL,sizeof(struct event_barrier_s));
    b->queue=queue;
    b->comm=comm;
    b->tag=tag;
        MPI_Comm_size(comm,&b->nodes);
        MPI_Comm_rank(comm,&b->me);
    b->wait=b->nodes-1;
    event_Irecv(queue,&b->msg,1,MPI_INT,MPI_ANY_SOURCE,tag,comm,barrier_recv,b);
    return b;
}

void event_barrier_wait(event_barrier_t barrier){
    for(int i=0;i<barrier->nodes;i++){
        if(i==barrier->me) continue;
        event_Send(barrier->queue,&barrier->me,1,MPI_INT,i,barrier->tag,barrier->comm);
    }
    event_while(barrier->queue,&barrier->wait);
    barrier->wait=barrier->nodes-1;
    event_Irecv(barrier->queue,&barrier->msg,1,
            MPI_INT,MPI_ANY_SOURCE,barrier->tag,barrier->comm,barrier_recv,barrier);
}

void event_decr(void*context,MPI_Status *status){
    (void)status;
    (*((int*)context))--;
}


