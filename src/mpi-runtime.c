#include <mpi.h>
#include <mpi-runtime.h>

void RTinitMPI(int*argcp,char**argvp[]){
	MPI_Init(argcp,argvp);
	RTinit(argcp,argvp);
}


void RTinitMPIthread(int*argcp,char**argvp[],int requested,int *provided){
	MPI_Init_thread(argcp,argvp,requested,provided);
	RTinit(argcp,argvp);
}

