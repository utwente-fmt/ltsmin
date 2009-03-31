#include <stdlib.h>
#include <mpi.h>
#include <mpi-runtime.h>

static int auto_abort=1;

static void check_auto_abort(){
	if(auto_abort==1) {
		fprintf(stderr,"bad exit, aborting\n");
		MPI_Abort(MPI_COMM_WORLD,1);
	}
	if(auto_abort==2) {
		int stop=1;
		int global=0;
		MPI_Allreduce(&stop,&global,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
		MPI_Finalize();
	}
}

void RTinitMPI(int*argcp,char**argvp[]){
	MPI_Init(argcp,argvp);
	RTinit(argcp,argvp);
}

void RTinitPoptMPI(int *argc_p,char**argv_p[],const struct poptOption * options,
	int min_args,int max_args,char*args[],
	const char* pgm_prefix,const char* arg_help,const char* extra_help
){
	MPI_Init(argc_p,argv_p);
	MPI_Errhandler_set(MPI_COMM_WORLD,MPI_ERRORS_ARE_FATAL);
	if (atexit(check_auto_abort)){
		Fatal(1,error,"atexit failed");
	}
	int mpi_nodes;
	int mpi_me;
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	if (mpi_me==0) {
		auto_abort=2;
		RTinitPopt(argc_p,argv_p,options,min_args,max_args,args,pgm_prefix,arg_help,extra_help);
		auto_abort=1;
	}
	int stop=0;
	int global=0;
	MPI_Allreduce(&stop,&global,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
	if (global) {
		RTfiniMPI();
		MPI_Finalize();
		exit(EXIT_SUCCESS);
	}
	if (mpi_me) {
		RTinitPopt(argc_p,argv_p,options,min_args,max_args,args,pgm_prefix,arg_help,extra_help);
		set_label("%s(%2d/%2d)",get_label(),mpi_me,mpi_nodes);
	}
}

void RTinitMPIthread(int*argcp,char**argvp[],int requested,int *provided){
	MPI_Init_thread(argcp,argvp,requested,provided);
	RTinit(argcp,argvp);
}

void RTfiniMPI(){
	auto_abort=0;

}

