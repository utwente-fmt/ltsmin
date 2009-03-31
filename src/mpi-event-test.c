#include "config.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>

#include "mpi-event-loop.h"
#include "runtime.h"

static event_queue_t mpi_queue;

static void  send_complete(void* context,MPI_Status *status){
	(void)status;
	printf("message %s sent\n",(char*)context);
}
static char msg[64];
static char msg2[64];

static char who[24];

int main(int argc, char*argv[]){
	int mpi_nodes,mpi_me;
	MPI_Init(&argc, &argv);
	MPI_Errhandler_set(MPI_COMM_WORLD,MPI_ERRORS_ARE_FATAL);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	sprintf(who,"inst-mpi(%2d)",mpi_me);
	RTinit(&argc,&argv);
	set_label(who);

	mpi_queue=event_queue();
	event_yield(mpi_queue);

	MPI_Barrier(MPI_COMM_WORLD);
	printf("I'm %d/%d\n",mpi_me,mpi_nodes);
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Request rq;
	sprintf(msg,"from %d/%d",mpi_me,mpi_nodes);
	for(int i=0;i<5;i++){
		Warning(info,"send %d",i);
		sprintf(msg,"%d from %d/%d",i,mpi_me,mpi_nodes);
		MPI_Isend(msg,strlen(msg)+1,MPI_CHAR,(mpi_me+1)%mpi_nodes,2,MPI_COMM_WORLD,&rq);
		event_post(mpi_queue,&rq,send_complete,msg);
		Warning(info,"recv %d",i);
		MPI_Irecv(msg2,64,MPI_CHAR,(mpi_me+mpi_nodes-1)%mpi_nodes,2,MPI_COMM_WORLD,&rq);
		MPI_Status stat;
		Warning(info,"wait %d",i);
		event_wait(mpi_queue,&rq,&stat);
		Warning(info,"%d/%d got %s",mpi_me,mpi_nodes,msg2);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	sleep(2);
	MPI_Barrier(MPI_COMM_WORLD);
	printf("testing barrier (%d) 1\n",mpi_me);
	event_barrier_t barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,16);
	event_barrier_wait(barrier);
	sleep(mpi_me);
	printf("testing barrier (%d) 2\n",mpi_me);
	event_barrier_wait(barrier);
	sleep(mpi_nodes-mpi_me);
	printf("testing barrier (%d) 3\n",mpi_me);
	event_barrier_wait(barrier);
	printf("testing termination detection\n");
	idle_detect_t counter=event_idle_create(mpi_queue,MPI_COMM_WORLD,32);
	printf("waiting for termination\n");
	event_idle_send(counter);
	event_idle_recv(counter);
	event_idle_detect(counter);
	printf("terminated\n");
	MPI_Barrier(MPI_COMM_WORLD);
	printf("cleaning up\n");
	event_yield(mpi_queue);
	event_queue_destroy(&mpi_queue);
	printf("exiting\n");
	MPI_Finalize();
	return 0;
}


