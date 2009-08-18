#include <stdio.h>
#include <string.h>
#include "options.h"
#include "config.h"
#include "messages.h"
#include "lts.h"
#include "aut-io.h"
#include <mpi.h>
#include "mpi_core.h"
//#include "Ddlts.h"
#include "Dtaudlts.h"
#include "set.h"
#include "time.h"

// some variables requested by bufs.h
int flag;
int send_pending;
int bufnewids_pending;
MPI_Request send_request;
//



int main(int argc,char **argv){
	dlts_t lts;
  taudlts_t t, tc;
	int i,j, k;
	int Finish;
	int *oscc, *wscc, *weight;
	char* workers, *mark;
	mytimer_t timer,compute_timer,exchange_timer;

  MPI_Init(&argc, &argv);
	timer=createTimer();
	compute_timer=createTimer();
	verbosity=1;

  if (argc < 3){
	 if (mpi_me==0) Warning(1,"Usage: ce <input file or dir> <output file>");
	 MPI_Barrier(MPI_COMM_WORLD);
	 MPI_Finalize();
	 exit(1);
	}

  MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);

  MPI_Barrier(MPI_COMM_WORLD);
	if (mpi_me==0) Warning(1,"(tau)SCC elimination");
	if (mpi_me==0) startTimer(timer);

  lts=dlts_create(MPI_COMM_WORLD);
  dlts_read(lts, argv[1], 0);

	if (mpi_me==0) {
		stopTimer(timer);
		reportTimer(timer,"reading the LTS took");
		resetTimer(timer);
		startTimer(timer);
	}
	
	t = taudlts_create(lts->comm);
	taudlts_extract_from_dlts(t, lts);

	if (mpi_me==0) {
		stopTimer(timer);
		reportTimer(timer,"extracting the taus took");
		resetTimer(timer);
		startTimer(timer);
	}

	/*	
	oscc=(int*)calloc(t->N,sizeof(int));	
	wscc=(int*)calloc(t->N,sizeof(int));
	for(i=0;i<t->N;i++) {
	 oscc[i] = i;
	 wscc[i] = mpi_me;
	}
	taudlts_elim_trivial(t, oscc);
	taudlts_printinfo(t, oscc);
	free(oscc);free(wscc);
	exit(0);
	*/

	cf=(int*)calloc(t->N,sizeof(int));	
	for(i=0;i<t->N;i++)
	 cf[i] = -1;

	taudlts_paintfwd(t,cf);
	//	taudlts_fwd2back(t);
	//	taudlts_paintfwd(t,cb);

	if (mpi_me==0) {
	 stopTimer(timer);
	 reportTimer(timer,"painting took");
	 resetTimer(timer);
	 startTimer(timer);
	}

		
	MPI_Finalize();
	return 0;


	oscc=(int*)calloc(t->N,sizeof(int));	
	wscc=(int*)calloc(t->N,sizeof(int));
	for(i=0;i<t->N;i++) {
	 oscc[i] = i;
	 wscc[i] = mpi_me;
	}
	taudlts_compute_scc(t,cf,cb,wscc,oscc);
	free(cf); free(cb);
	taudlts_clear_useless_transitions(t);
	taudlts_fwd2back(t);

	taudlts_insert_to_dlts(t, lts); 
	dlts_shuffle(lts, wscc, oscc);
	taudlts_free(t);
	dlts_shrink(lts, wscc, oscc, NULL);
	
	if (mpi_me==0) Warning(1,"\n\nNOW WRITING");
	dlts_writeaut(lts, argv[2], 0);

	//	dlts_free(lts);
		
	MPI_Finalize();
	return 0;

	/*
	dlts_paintback(lts, cf);
	dlts_fwd2back(lts);
	dlts_paintback(lts, cb);
	dlts_fwd2back(lts);
	*/
	
}







