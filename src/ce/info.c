#include <stdio.h>
#include <string.h>
#include "options.h"
#include "config.h"
#include "messages.h"
#include <malloc.h>
#include "lts.h"
#include "aut-io.h"
#include <mpi.h>
//#include "mpi_core.h"
#include "Ddlts.h"
//#include "Dtaudlts.h"
//#include "paint.h"
//#include "set.h"


// some variables requested by bufs.h
int flag;
int send_pending;
int bufnewids_pending;
MPI_Request send_request;
//



//#define VERBOSE

#define COLOR

// the number of vertices and arcs ..
// final
int Nfinal, Mfinal;
// hooked, i.e. on a cycle
int Nhooked, Mhooked;
// not yet classified as "final" or "hooked"
int Nleft, Mleft;

int me, nodes;





//***************************************************************
//***************************************************************

int main(int argc,char **argv){
	dlts_t lts;
	int oldN, oldM, i, j, k, intM;
  MPI_Init(&argc, &argv); 
	MPI_Comm_size(MPI_COMM_WORLD, &nodes);
  MPI_Comm_rank(MPI_COMM_WORLD, &me);

	verbosity=1;

  if (argc < 2){
	 if (me==0) 
		Warning(1,"Usage: info <input file or dir> ");
	 MPI_Barrier(MPI_COMM_WORLD);
	 MPI_Finalize();
	 exit(1);
	}
  MPI_Barrier(MPI_COMM_WORLD);
	if (me==0) Warning(1,"Reports over %s ",argv[1]);

  lts=dlts_create(MPI_COMM_WORLD);
  dlts_read(lts, argv[1], 0);
	oldN = lts->state_count[me];
	oldM=0; 
	for(i=0;i<lts->segment_count;i++) oldM+=lts->transition_count[me][i];
	intM = lts->transition_count[me][me];
	for(i=0;i<nodes;i++){
	 if (i == me)
		Warning(1,"%3d: %10d outgoing transitions, %10d internal, %10d states", 
						me, 
						oldM - lts->transition_count[me][me],  
						lts->transition_count[me][me], oldN);
		MPI_Barrier(MPI_COMM_WORLD);
	}

	MPI_Reduce(&intM, &k, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&oldN, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&oldM, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);	
	if (me==0) {	 
	 oldN=i; oldM=j; intM = k;
	 Warning(1,"\n%10d states \n%10d transitions from which %10d internal [%3.3f]", 
					 oldN, oldM, intM,
					 (((float)intM)/((float)oldM)) * 100);
	}

	if (lts != NULL) dlts_free(lts);
	MPI_Finalize();
	return 0;
}







