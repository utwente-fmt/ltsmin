#include <config.h>
#include <task-queue.h>
#include <mpi-runtime.h>
#include <dynamic-array.h>

#define BUFFER_SIZE 64512

#define IDLE_TAG 0
#define BARRIER_TAG 1

struct task_queue_s {
    int next_tag;
    MPI_Comm ctrl;
    MPI_Comm comm;
    int mpi_me;
    int mpi_nodes;
    event_queue_t mpi_queue;
    idle_detect_t counter;
    array_manager_t man;
    task_t *task;
    char *buffer;
    int lazy;
    event_barrier_t barrier;
};

struct task_s {
    task_queue_t queue;
    int tag;
    int arg_len;
    void *context;
    task_exec_t call;
    int *used;
    char **write_buf;
    char **send_buf;
    int *send_pending;
};

int TQthreadCount(task_queue_t queue){
    return queue->mpi_nodes;
}

int TQthreadID(task_queue_t queue){
    return queue->mpi_me;
}


void message_handler(void*context,MPI_Status *status){
    task_queue_t queue=(task_queue_t)context;
    event_idle_recv(queue->counter);
    task_t task=queue->task[status->MPI_TAG];
    int count;
    MPI_Get_count(status,MPI_CHAR,&count);
    int from=status->MPI_SOURCE;
    if(task->arg_len){
        for(int ofs=0;ofs<count;ofs+=task->arg_len){
            task->call(task->context,from,task->arg_len,queue->buffer+ofs);
        }
    } else {
        int ofs=0;
        while(ofs<count){
            uint16_t length;
            memcpy(&length,queue->buffer+ofs,2);
            ofs+=2;
            task->call(task->context,from,length,queue->buffer+ofs);
            ofs+=length;
        }
    }
    event_Irecv(queue->mpi_queue,queue->buffer,BUFFER_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
            MPI_ANY_TAG,queue->comm,message_handler,queue);
}


task_queue_t TQcreateMPI(event_queue_t mpi_queue){
    task_queue_t queue=RT_NEW(struct task_queue_s);
    MPI_Comm_dup (MPI_COMM_WORLD, &queue->comm);
    MPI_Comm_rank(queue->comm, &queue->mpi_me);
    MPI_Comm_size(queue->comm, &queue->mpi_nodes);
    queue->mpi_queue=mpi_queue;
    MPI_Comm_dup (MPI_COMM_WORLD, &queue->ctrl);
    queue->counter=event_idle_create(mpi_queue,queue->ctrl,IDLE_TAG);
    queue->barrier=event_barrier_create(mpi_queue,queue->ctrl,BARRIER_TAG);
    queue->man=create_manager(8);
    ADD_ARRAY(queue->man,queue->task,task_t);
    queue->lazy=1;
    queue->buffer=(char*)RTmallocZero(BUFFER_SIZE);
    event_Irecv(queue->mpi_queue,queue->buffer,BUFFER_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
            MPI_ANY_TAG,queue->comm,message_handler,queue);
    return queue;
}

static int new_tag(task_queue_t queue){
    int i;
    for(i=0;i<queue->next_tag;i++){
        if (queue->task[i]==NULL) return i;
    }
    queue->next_tag++;
    return i;
}

static void send_complete(void*context,MPI_Status *status){
    (void)status;
    int *pending=(int*)context;
    *pending=0;
}

void send_rotate(task_t task,int dest){
    task_queue_t queue=task->queue;
    if (task->send_pending[dest]) {
        event_while(queue->mpi_queue,&task->send_pending[dest]);
    } else {
        event_yield(queue->mpi_queue);
    }
    char*tmp=task->send_buf[dest];
    task->send_buf[dest]=task->write_buf[dest];
    task->write_buf[dest]=tmp;
    event_idle_send(task->queue->counter);
    task->send_pending[dest]=1;
    event_Isend(queue->mpi_queue,task->send_buf[dest],task->used[dest],MPI_CHAR,
        dest,task->tag,queue->comm,send_complete,&task->send_pending[dest]);
    task->used[dest]=0;
}


static task_t TaskCreate(task_queue_t q,int arg_len,void * context,task_exec_t call){
    task_t task=RT_NEW(struct task_s);
    task->queue=q;
    task->tag=new_tag(q);
    task->arg_len=arg_len;
    task->context=context;
    task->call=call;
    ensure_access(q->man,task->tag);
    q->task[task->tag]=task;
    task->used=RTmallocZero(q->mpi_nodes*sizeof(int));
    task->write_buf=RTmallocZero(q->mpi_nodes*sizeof(char*));
    task->send_buf=RTmallocZero(q->mpi_nodes*sizeof(char*));
    task->send_pending=RTmallocZero(q->mpi_nodes*sizeof(int));
    for(int i=0;i<q->mpi_nodes;i++){
        if(i==q->mpi_me) continue;
        task->write_buf[i]=RTmallocZero(BUFFER_SIZE);
        task->send_buf[i]=RTmallocZero(BUFFER_SIZE);
    }
    return task;
}

task_t TaskCreateFixed(task_queue_t q,int arg_len,void * context,task_exec_t call){
    if (arg_len<=0) {
        Fatal(1,error,"illegal length %d: length has to be >0",arg_len);
    }
    return TaskCreate(q,arg_len,context,call);
}

task_t TaskCreateFlex(task_queue_t q,void * context,task_exec_t call){
    return TaskCreate(q,0,context,call);
}

void TaskDestroy(task_t task){
    task_queue_t queue=task->queue;
    queue->task[task->tag]=NULL;
    for(int i=0;i<queue->mpi_nodes;i++){
        if(i==queue->mpi_me) continue;
        RTfree(task->write_buf[i]);
        RTfree(task->send_buf[i]);
    }
    RTfree(task->used);
    RTfree(task->write_buf);
    RTfree(task->send_buf);
    RTfree(task->send_pending);
    RTfree(task);
}


void TaskSubmitFixed(task_t task,int owner,void* arg){
    if (owner==task->queue->mpi_me){
        task->call(task->context,owner,task->arg_len,arg);
    } else {
        if (task->queue->lazy){
            if ((task->used[owner]+task->arg_len)>=BUFFER_SIZE){
                send_rotate(task,owner);
            }
            memcpy(task->write_buf[owner]+task->used[owner],arg,task->arg_len);
            task->used[owner]+=task->arg_len;
        } else {
            event_idle_send(task->queue->counter);
            event_Send(task->queue->mpi_queue,arg,task->arg_len,MPI_CHAR,owner,task->tag,task->queue->comm);
        }
    }
}

void TaskSubmitFlex(task_t task,int owner,int len,void* arg){
    if (owner==task->queue->mpi_me){
        task->call(task->context,owner,len,arg);
    } else {
        if (task->queue->lazy){
            if ((task->used[owner]+len+2)>=BUFFER_SIZE){
                send_rotate(task,owner);
            }
            uint16_t length=(uint16_t)len;
            memcpy(task->write_buf[owner]+task->used[owner],&length,2);
            task->used[owner]+=2;
            memcpy(task->write_buf[owner]+task->used[owner],arg,len);
            task->used[owner]+=len;
        } else {
            char msg[len+2];
            uint16_t length=(uint16_t)len;
            memcpy(msg,&length,2);
            memcpy(msg+2,arg,len);
            event_idle_send(task->queue->counter);
            event_Send(task->queue->mpi_queue,msg,len+2,MPI_CHAR,owner,task->tag,task->queue->comm);
        }
    }
}

void flush_buffers(task_queue_t queue){
    for(int i=0;i<queue->next_tag;i++){
        if (!queue->task[i]) continue;
        for(int j=0;j<queue->mpi_nodes;j++){
            if(queue->task[i]->used[j]){
                send_rotate(queue->task[i],j);
            }
            if (queue->task[i]->send_pending[j]){
                event_while(queue->mpi_queue,&queue->task[i]->send_pending[j]);
            }
        }
    }
}

void TQwait(task_queue_t queue){
    queue->lazy=0;
    flush_buffers(queue);
    event_idle_detect(queue->counter);
    queue->lazy=1;
}

/*
void TQwait2(task_queue_t q1,task_queue_t q2){
    q1->lazy=0;
    flush_buffers(q1);
    q2->lazy=0;
    flush_buffers(q2);
    event_idle_detect(q1->counter);
    event_idle_detect(q2->counter);
    q1->lazy=1;
    q2->lazy=1;
}
*/

void TQwhile(task_queue_t queue,int*condition){
    queue->lazy=0;
    flush_buffers(queue);
    event_while(queue->mpi_queue,condition);
    queue->lazy=1;
}

void TQyield(task_queue_t queue){
    event_yield(queue->mpi_queue);
}

void TQblock(task_queue_t queue){
    event_block(queue->mpi_queue);
}

void TQbarrier(task_queue_t queue){
    event_barrier_wait(queue->barrier);
}

task_queue_t TQdup(task_queue_t queue){
    return TQcreateMPI(queue->mpi_queue);
}


