#include "mpi-event-loop.h"
#include "runtime.h"
#include "dynamic-array.h"

struct event_queue_s{
	array_manager_t man;
	MPI_Request *request;
	event_callback *cb;
	void** context;
	int pending;
};

event_queue_t event_queue(){
	event_queue_t queue=(event_queue_t)RTmalloc(sizeof(struct event_queue_s));
	queue->man=create_manager(1024);
	queue->request=NULL;
	add_array(queue->man,&queue->request,sizeof(MPI_Request));
	queue->cb=NULL;
	add_array(queue->man,&queue->cb,sizeof(event_callback));
	queue->context=NULL;
	add_array(queue->man,&queue->context,sizeof(void*));
	queue->pending=0;
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
	queue->cb[queue->pending]=cb;
	queue->context[queue->pending]=context;
	queue->pending++;
}

void event_yield(event_queue_t queue){
	MPI_Status stat;
	int found;
	int completed;
	while(queue->pending){
		MPI_Testany(queue->pending,queue->request,&completed,&found,&stat);
		if(!found) return;
		event_callback cb=queue->cb[completed];
		void*context=queue->context[completed];
		queue->pending--;
		if (completed<queue->pending){
			queue->request[completed]=queue->request[queue->pending];
			queue->cb[completed]=queue->cb[queue->pending];
			queue->context[completed]=queue->context[queue->pending];
		}
		cb(context,&stat); // this call can change the queue!
	}
}

void event_while(event_queue_t queue,int *condition){
	while(*condition){
		MPI_Status stat;
		int completed;
		MPI_Waitany(queue->pending,queue->request,&completed,&stat);
		event_callback cb=queue->cb[completed];
		void*context=queue->context[completed];
		queue->pending--;
		if (completed<queue->pending){
			queue->request[completed]=queue->request[queue->pending];
			queue->cb[completed]=queue->cb[queue->pending];
			queue->context[completed]=queue->context[queue->pending];
		}
		cb(context,&stat); // this call can change the queue!
	}
}

void event_wait(event_queue_t queue,MPI_Request *request,MPI_Status *status){
	for(;;){
		int found;
		MPI_Test(request,&found,status);
		if (found) return;
		//Warning(info,"%d+1 requests",queue->pending);
		int completed;
		ensure_access(queue->man,queue->pending);
		queue->request[queue->pending]=*request;
		MPI_Waitany(queue->pending+1,queue->request,&completed,status);
		if(completed==queue->pending) {
			//Warning(info,"it was the last one");
			return;
		}
		event_callback cb=queue->cb[completed];
		void*context=queue->context[completed];
		queue->pending--;
		if (completed<queue->pending){
			queue->request[completed]=queue->request[queue->pending];
			queue->cb[completed]=queue->cb[queue->pending];
			queue->context[completed]=queue->context[queue->pending];
		}
		cb(context,status); // this call can change the queue!
	}
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
	queue->cb[queue->pending]=cb;
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
	queue->cb[queue->pending]=cb;
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
	idle_detect_t d=(idle_detect_t)RTmalloc(sizeof(struct idle_detect_s));
	d->queue=queue;
	d->comm=comm;
	d->tag=tag;
	d->dirty=0;
	d->count=0;
        MPI_Comm_size(comm,&d->nodes);
        MPI_Comm_rank(comm,&d->me);
	d->msg_pending=(d->me==0)?0:1;
	event_Irecv(queue,&d->term_msg,2,MPI_INT,MPI_ANY_SOURCE,tag,comm,idle_receiver,d);
	return d;
}

void event_idle_send(idle_detect_t detector){
	detector->count++;
}

void event_idle_recv(idle_detect_t detector){
	detector->dirty=1;
	detector->count--;
}

void event_idle_detect(idle_detect_t detector){
	if (detector->me==0){
		int round=0;
		int term_send[2];
		term_send[0]=IDLE;
		term_send[1]=0;
		for(;;){
			//Log(debug,"starting new termination round %d %d\n",detector->dirty,detector->count);
			round++;
			detector->dirty=0;
			//Log(debug,"sending %d %d\n",term_send[0],term_send[1]);
			event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,detector->tag,detector->comm);
			detector->msg_pending++;
			event_while(detector->queue,&detector->msg_pending);
			//Log(debug,"reply is %d %d\n",detector->term_msg[0],detector->term_msg[1]);
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
			event_Send(detector->queue,term_send,2,MPI_INT,detector->nodes-1,
					detector->tag,detector->comm);
			detector->msg_pending++;
			event_while(detector->queue,&detector->msg_pending);
			//Log(debug,"broadcast complete");
			return;
		}
	} else {
		for(;;){
			int term_send[2];
			event_while(detector->queue,&detector->msg_pending);
			//Log(debug,"got %d %d\n",detector->term_msg[0],detector->term_msg[1]);
			if (detector->term_msg[0]==TERMINATED) {
				term_send[0]=TERMINATED;
				event_Send(detector->queue,term_send,2,MPI_INT,detector->me-1,
						detector->tag,detector->comm);
				detector->msg_pending++;
				return;
			}
			term_send[0]=detector->dirty?RUNNING:detector->term_msg[0];
			detector->dirty=0;
			term_send[1]=detector->term_msg[1]+detector->count;
			//Log(debug,"sending %d %d\n",term_send[0],term_send[1]);
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


