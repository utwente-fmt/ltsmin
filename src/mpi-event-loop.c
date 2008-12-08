#include "mpi-event-loop.h"
#include "runtime.h"
#include "dynamic-array.h"

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

void event_statistics(event_queue_t queue){
	Warning(info,"wait : %lld calls, %lld multiple",
		queue->wait_some_calls,queue->wait_some_multi);
	Warning(info,"test : %lld calls, %lld none, %lld multiple",
		queue->test_some_calls,queue->test_some_none,queue->test_some_multi);
	Warning(info,"queue size is %d",array_size(queue->man));
}

static void null_cb(void* context,MPI_Status *status){
	(void)context;
	(void)status;
}

event_queue_t event_queue(){
	event_queue_t queue=(event_queue_t)RTmalloc(sizeof(struct event_queue_s));
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
	free((*queue)->request);
	free((*queue)->cb);
	free((*queue)->context);
	free(*queue);
	*queue=NULL;
}

void event_post(event_queue_t queue,MPI_Request *request,event_callback cb,void*context){
	ensure_access(queue->man,queue->pending);
	queue->request[queue->pending]=*request;
	queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
	queue->context[queue->pending]=context;
	queue->pending++;
}

void event_yield(event_queue_t queue){
	while(queue->pending){
		int index[queue->pending];
		int completed;
		MPI_Status status[queue->pending];
		MPI_Testsome(queue->pending,queue->request,&completed,index,status);
		queue->test_some_calls++;
		if (completed==0) {
			queue->test_some_none++;
			return;
		}
		if (completed>1) queue->test_some_multi++;
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
			cb[i](ctx[i],&status[i]);
		}
	}
}

void event_while(event_queue_t queue,int *condition){
	for(int i=0;i<queue->pending;i++){
		if (queue->cb[i]==NULL) Fatal(1,error,"illegal NULL in queue");
	}
	while(*condition){
		int index[queue->pending];
		int completed;
		MPI_Status status[queue->pending];
		MPI_Waitsome(queue->pending,queue->request,&completed,index,status);
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
			cb[i](ctx[i],&status[i]);
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
	MPI_Isend(buf,count,datatype,dest,tag,comm,&request);
	event_wait(queue,&request,&status);
}

void event_Isend(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int dest, int tag, MPI_Comm comm,event_callback cb,void*context){
	ensure_access(queue->man,queue->pending);
	MPI_Isend(buf,count,datatype,dest,tag,comm,&queue->request[queue->pending]);
	queue->cb[queue->pending]=cb?cb:null_cb; // we cannot allow NULL as a call-back.
	queue->context[queue->pending]=context;
	queue->pending++;
	
}

void event_Recv(event_queue_t queue, void *buf, int count, MPI_Datatype datatype,
            int source, int tag, MPI_Comm comm, MPI_Status *status){
	MPI_Request request;
	MPI_Irecv(buf,count,datatype,source,tag,comm,&request);
	event_wait(queue,&request,status);
}

void event_Irecv(event_queue_t queue,void *buf, int count, MPI_Datatype datatype,
	int source, int tag, MPI_Comm comm,event_callback cb,void*context){
	ensure_access(queue->man,queue->pending);
	MPI_Irecv(buf,count,datatype,source,tag,comm,&queue->request[queue->pending]);
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
	//Log(info,"got idle message (%d) [%d %d]",detect->msg_pending,detect->term_msg[0],detect->term_msg[1]);
	detect->msg_pending--;
	event_Irecv(detect->queue,&detect->term_msg,2,MPI_INT,MPI_ANY_SOURCE,
			detect->tag,detect->comm,idle_receiver,context);
#undef detect
}

idle_detect_t event_idle_create(event_queue_t queue,MPI_Comm comm,int tag){
	idle_detect_t d=(idle_detect_t)RTmalloc(sizeof(struct idle_detect_s));
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
		int round=0;
		int term_send[2];
		term_send[0]=IDLE;
		term_send[1]=0;
		for(;;){
			//Log(info,"starting new termination round %d %d\n",detector->dirty,detector->count);
			round++;
			detector->dirty=0;
			//Log(info,"sending %d %d",term_send[0],term_send[1]);
			event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,detector->tag,detector->comm);
			detector->msg_pending++;
			//Log(info,"while");
			event_while(detector->queue,&detector->msg_pending);
			//Log(info,"reply is %d %d",detector->term_msg[0],detector->term_msg[1]);
			if (detector->term_msg[0]!=IDLE) {
				//Log(debug,"not idle yet");
				continue;
			}
			if (detector->dirty){
				//Log(debug,"I'm dirty");
				continue;
			}
			if ((detector->term_msg[1]+detector->count)!=0){
				//Log(debug,"message total is %d",detector->term_msg[2]+detector->count);
				continue;
			}
			//Log(debug,"termination detected in %d rounds\n",round);
			term_send[0]=TERMINATED;
			term_send[1]=detector->exit_code;
			event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,
					detector->tag,detector->comm);
			detector->msg_pending++;
			event_while(detector->queue,&detector->msg_pending);
			//Log(debug,"broadcast complete");
			return detector->exit_code;
		}
	} else {
		for(;;){
			int term_send[2];
			//Log(info,"while");
			event_while(detector->queue,&detector->msg_pending);
			//Log(info,"got %d %d",detector->term_msg[0],detector->term_msg[1]);
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
			//Log(info,"sending %d %d",term_send[0],term_send[1]);
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
	event_barrier_t b=(event_barrier_t)RTmalloc(sizeof(struct event_barrier_s));
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


