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

void event_wait(event_queue_t queue,MPI_Request *request,MPI_Status *status){
	for(;;){
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


/** mpi lock implementation */

struct mpi_lock_s {
	MPI_Comm comm;
	int tag;
	int nodes;
	int me;
};


mpi_lock_t mpi_lock_create(MPI_Comm comm,int tag){
	mpi_lock_t lock=(mpi_lock_t)RTmalloc(sizeof(struct mpi_lock_s));
	lock->comm=comm;
	lock->tag=tag;
	MPI_Comm_size(comm, &lock->nodes);
	MPI_Comm_rank(comm, &lock->me);
	return lock;
}

void mpi_lock_get(mpi_lock_t lock){
}

int mpi_lock_try(mpi_lock_t lock){
	return 0;
}

void mpi_lock_free(mpi_lock_t lock){
}

int mpi_lock_check(mpi_lock_t lock){
	return 0;
}


