#include <task-queue.h>
#include <mpi-runtime.h>
#include <dynamic-array.h>

#define BUFFER_SIZE 65536

#define IDLE_TAG 0

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
    char buffer[BUFFER_SIZE];
    int lazy;
};

struct task_s {
    task_queue_t queue;
    int tag;
    int arg_len;
    void *context;
    task_exec_t call;
    int *used;
    char **buffer;
};

static void message_handler(void*context,MPI_Status *status){
    task_queue_t queue=(task_queue_t)context;
    event_idle_recv(queue->counter);
    task_t task=queue->task[status->MPI_TAG];
    int count;
    MPI_Get_count(status,MPI_CHAR,&count);
    if(task->arg_len){
        for(int ofs=0;ofs<count;ofs+=task->arg_len){
            task->call(task->context,task->arg_len,queue->buffer+ofs);
        }
    } else {
        Fatal(1,error,"variable length unimplemented");
    }
    event_Irecv(queue->mpi_queue,queue->buffer,BUFFER_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
            MPI_ANY_SOURCE,queue->comm,message_handler,queue);
}


task_queue_t TQcreateMPI(event_queue_t mpi_queue){
    task_queue_t queue=RT_NEW(struct task_queue_s);
    MPI_Comm_dup (MPI_COMM_WORLD, &queue->comm);
    MPI_Comm_rank(queue->comm, &queue->mpi_me);
    MPI_Comm_size(queue->comm, &queue->mpi_nodes);
    queue->mpi_queue=mpi_queue;
    MPI_Comm_dup (MPI_COMM_WORLD, &queue->ctrl);
    queue->counter=event_idle_create(mpi_queue,queue->ctrl,IDLE_TAG);
    queue->man=create_manager(8);
    ADD_ARRAY(queue->man,queue->task,task_t);
    queue->lazy=1;
    event_Irecv(queue->mpi_queue,queue->buffer,BUFFER_SIZE,MPI_CHAR,MPI_ANY_SOURCE,
            MPI_ANY_SOURCE,queue->comm,message_handler,queue);
    return queue;
}

task_t TaskCreateFixed(task_queue_t q,int arg_len,void * context,task_exec_t call){
    task_t task=RT_NEW(struct task_s);
    task->queue=q;
    task->tag=q->next_tag;
    q->next_tag++;
    task->arg_len=arg_len;
    task->context=context;
    task->call=call;
    ensure_access(q->man,task->tag);
    q->task[task->tag]=task;
    task->used=RTmallocZero(q->mpi_nodes*sizeof(int));
    task->buffer=RTmallocZero(q->mpi_nodes*sizeof(char*));
    for(int i=0;i<q->mpi_nodes;i++){
        if(i==q->mpi_me) continue;
        task->buffer[i]=RTmallocZero(BUFFER_SIZE);
    }
    return task;
}

void TaskSubmitFixed(task_t task,int owner,void* arg){
    if (owner==task->queue->mpi_me){
        task->call(task->context,task->arg_len,arg);
    } else {
        if (task->queue->lazy){
            if ((task->used[owner]+task->arg_len)>=BUFFER_SIZE){
                event_idle_send(task->queue->counter);
                event_Send(task->queue->mpi_queue,task->buffer[owner],task->used[owner],MPI_CHAR,owner,task->tag,task->queue->comm);
                task->used[owner]=0;
            }
            memcpy(task->buffer[owner]+task->used[owner],arg,task->arg_len);
            task->used[owner]+=task->arg_len;
        } else {
            event_idle_send(task->queue->counter);
            event_Send(task->queue->mpi_queue,arg,task->arg_len,MPI_CHAR,owner,task->tag,task->queue->comm);
        }
    }
}

void TQwait(task_queue_t queue){
    queue->lazy=0;
    for(int i=0;i<queue->next_tag;i++){
        for(int j=0;j<queue->mpi_nodes;j++){
            if(queue->task[i]->used[j]){
                event_idle_send(queue->counter);
                event_Ssend(queue->mpi_queue,queue->task[i]->buffer[j],
                    queue->task[i]->used[j],MPI_CHAR,j,i,queue->comm);
                queue->task[i]->used[j]=0;
            }
        }
    }
    event_idle_detect(queue->counter);
    queue->lazy=1;
}

