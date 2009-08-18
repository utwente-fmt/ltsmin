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
//#include "Ddlts.h"
#include "Dtaudlts.h"
#include "paint.h"
#include "set.h"
#include "time.h"

// some variables requested by bufs.h
int flag;
int send_pending;
int bufnewids_pending;
MPI_Request send_request;
//



//#define VERBOSE

//#define WRITEDIR 	// set this if you want a .dir output
			// the default output is .aut


#define COLOR 		// comment this out if you want to use	
			// the groups method


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
	mytimer_t timer;
	int oldN, oldM, tauN, tauM, N, M,i, j;
  MPI_Init(&argc, &argv); 
	MPI_Comm_size(MPI_COMM_WORLD, &nodes);
  MPI_Comm_rank(MPI_COMM_WORLD, &me);

	timer=createTimer();
	verbosity=1;

  if (argc < 3){
	 if (me==0) 
		Warning(1,"Usage: ce <input file or dir> <output file or dir>");
	 MPI_Barrier(MPI_COMM_WORLD);
	 MPI_Finalize();
	 exit(1);
	}
  MPI_Barrier(MPI_COMM_WORLD);
	if (me==0) Warning(1,"(tau)SCC elimination");
	if (me==0) startTimer(timer);

  lts=dlts_create(MPI_COMM_WORLD);
  dlts_read(lts, argv[1], 0);
	oldN = lts->state_count[me];
	oldM=0; 
	for(i=0;i<lts->segment_count;i++) oldM+=lts->transition_count[me][i];
	MPI_Reduce(&oldN, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&oldM, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);	
	if (me==0) {	 
	 oldN=i; oldM=j;
	 Warning(1,"%d states and %d transitions", oldN, oldM);
	 stopTimer(timer);
	 reportTimer(timer,"\n***** reading the LTS took");	 
	 resetTimer(timer);
	 startTimer(timer);
	}



#ifdef COLOR 	
	dlts_elim_tauscc_colours(lts);	
#else
	if (!dlts_elim_tauscc_groups(lts)){
	 if (me==0)
		Warning(1,"cannot get it small enough!");	 
	 //if (lts != NULL) dlts_free(lts);
	 MPI_Finalize();
	 return 0;
	}
#endif	

	MPI_Barrier(lts->comm);
	if (me==0) {
	 stopTimer(timer); reportTimer(timer,"\n***** SCC reduction took");
	 resetTimer(timer); startTimer(timer);
	}

	// statistics...
	N = lts->state_count[me]; M=0; 
	for(i=0;i<lts->segment_count;i++) M+=lts->transition_count[me][i];
	MPI_Reduce(&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	if (me==0) {
	 Warning(1,"LTS initial:%10d states and %10d transitions",oldN,oldM);
	 Warning(1,"LTS reduced:%10d states [%3.3f] and %10d [%3.3f] transitions", 
					 i, (((float)i)/((float)oldN)) * 100, 
					 j, (((float)j)/((float)oldM)) * 100);
	}
	N = Nfinal + Nleft + Nhooked; M = Mfinal + Mhooked;
	MPI_Reduce(&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	tauN=i; tauM=j;
	N = Nfinal + Nleft; M = Mfinal;
	MPI_Reduce(&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	MPI_Reduce(&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
	if (me==0) {
	 Warning(1,"TAU initial:%10d states [%3.3f] and %10d [%3.3f] transitions",
					 tauN, (((float)tauN)/((float)oldN)) * 100,
					 tauM, (((float)tauM)/((float)oldM)) * 100);
	 Warning(1,"TAU reduced:%10d states [%3.3f] and %10d [%3.3f] transitions", 
					 i, (((float)i)/((float)tauN)) * 100, 
					 j, (((float)j)/((float)tauM)) * 100);
	}


	if (me==0) Warning(1,"\n\nNOW WRITING");

#ifdef WRITEDIR
	dlts_writedir(lts, argv[2], 0);
#else
	dlts_writeaut(lts, argv[2], 0);
#endif

	//if (lts != NULL) dlts_free(lts);
	MPI_Finalize();
	return 0;
}







