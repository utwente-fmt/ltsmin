
#include "mpi_core.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"

#define PENDING_T char

/** variables **/
int		mpi_nodes;
int		mpi_me;
int*		mpi_zeros;
int*		mpi_ones;
int*		mpi_indices;
char 		mpi_name[MPI_MAX_PROCESSOR_NAME];

static int		core_next=0;
static int		core_max=0;
static core_handler	*handler=NULL;
static void		**arg=NULL;
static PENDING_T	*pending=NULL;
//static int		buffer_size=0;
//static void 		*buffer=NULL;

/** main functions **/

static void barrier_init();
static void terminate_init();

void core_init(){
	int len,i;
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	MPI_Get_processor_name(mpi_name,&len);
	mpi_zeros=(int*)malloc(mpi_nodes*sizeof(int));
	mpi_ones=(int*)malloc(mpi_nodes*sizeof(int));
	mpi_indices=(int*)malloc(mpi_nodes*sizeof(int));
//	buffer_size=1048576 * mpi_nodes;
//	buffer=malloc(buffer_size);
//	MPI_Buffer_attach(buffer,buffer_size);
	for(i=0;i<mpi_nodes;i++){
		mpi_zeros[i]=0;
		mpi_ones[i]=1;
		mpi_indices[i]=i;
	}
	barrier_init();
	terminate_init();
	MPI_Barrier(MPI_COMM_WORLD);
}

int core_add(void*a,core_handler h){
	if (core_next==core_max){
		core_max+=128;
		handler=(core_handler*)realloc(handler,core_max*sizeof(core_handler));
		arg=(void**)realloc(arg,core_max*sizeof(void*));
		pending=(PENDING_T*)realloc(pending,core_max*sizeof(PENDING_T));
	}
	handler[core_next]=h;
	arg[core_next]=a;
	pending[core_next]=0;
	return core_next++;
}

void core_yield(){
	int found;
	MPI_Status status;
	for(;;){
		MPI_Iprobe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&found,&status);
		if(found){
			pending[status.MPI_TAG]=0;
			handler[status.MPI_TAG](arg[status.MPI_TAG],&status);
		} else {
			return;
		}
	}
}

void core_wait(int tag){
	MPI_Status status;
	if (tag==MPI_ANY_TAG){
		MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
		pending[status.MPI_TAG]=0;
		handler[status.MPI_TAG](arg[status.MPI_TAG],&status);
		return;
	}
	pending[tag]=1;
	while(pending[tag]){
		MPI_Probe(MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD,&status);
		pending[status.MPI_TAG]=0;
		handler[status.MPI_TAG](arg[status.MPI_TAG],&status);
	}
}

/** core barrier service **/

static int barrier_tag;
static int barrier_count;

static void barrier_service(void *arg,MPI_Status*probe_status){
	(void)arg;(void)probe_status;
	MPI_Status status,*recv_status=&status;
	int other;
	MPI_Recv(&other,1,MPI_INT,MPI_ANY_SOURCE,barrier_tag,MPI_COMM_WORLD,recv_status);
	barrier_count--;
}

void core_barrier(){
	int i;
	/* first wait for all workers to go from active (client and server) to passive (server only) */
	/* also send message to myself to force all service requests to myself to be flushed */
	for(i=0;i<mpi_nodes;i++) {
		MPI_Send(&mpi_me,1,MPI_INT,i,barrier_tag,MPI_COMM_WORLD);
	}
	while(barrier_count) core_wait(barrier_tag);
	barrier_count=mpi_nodes;
	/* then make sure that all servers have stopped before continuing */
	MPI_Barrier(MPI_COMM_WORLD);
}

static void barrier_init(){
	barrier_tag=core_add(NULL,barrier_service);
	barrier_count=mpi_nodes;
}

/** core termination service **/

static int term_tag;
static int term_message[2];
static int term_count;

#define RUNNING 0
#define IDLE 1
#define TERMINATED 2

static void termination_service(void *arg,MPI_Status*probe_status){
	(void)arg;(void)probe_status;
	MPI_Status status,*recv_status=&status;
	MPI_Recv(term_message,2,MPI_INT,MPI_ANY_SOURCE,term_tag,MPI_COMM_WORLD,recv_status);
	if (term_message[0]==TERMINATED) {
		term_count--;
		return;
	}
}

void core_terminate(TERM_STRUCT*status){
	int i;
	if (mpi_me==0) {
		term_message[0]=RUNNING;
		for(i=0;;i++){
			Log(debug,"starting new termination round %d %d\n",status->dirty,status->count);
			if ((term_message[0]==IDLE)&&(!status->dirty)&&((term_message[1]+status->count)==0)) {
				term_message[0]=TERMINATED;
				Log(debug,"termination detected in %d rounds\n",i);
				break;
			}
			term_message[0]=IDLE;
			term_message[1]=0;
			status->dirty=0;
			MPI_Send(term_message,2,MPI_INT,mpi_nodes-1,term_tag,MPI_COMM_WORLD);
			core_wait(term_tag);
			Log(debug,"reply is %d %d\n",term_message[0],term_message[1]);
		}
	} else {
		if (term_message[0]==TERMINATED) core_wait(term_tag);
		for(;;){
			if (term_message[0]==TERMINATED) break;
			term_message[1]+=status->count;
			if (status->dirty) {
				status->dirty=0;
				term_message[0]=RUNNING;
			}
			MPI_Send(term_message,2,MPI_INT,mpi_me-1,term_tag,MPI_COMM_WORLD);
			core_wait(term_tag);
		}
	}
	for(i=0;i<mpi_nodes;i++) if(i!=mpi_me) {
		MPI_Send(term_message,2,MPI_INT,i,term_tag,MPI_COMM_WORLD);
	}
	while(term_count) core_wait(term_tag);
	term_count=mpi_nodes-1;
	status->dirty=0;
	status->count=0;
	
}

static void terminate_init(){
	term_tag=core_add(NULL,termination_service);
	term_message[0]=TERMINATED;
	term_count=mpi_nodes-1;
}

