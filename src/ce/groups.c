#include <hre/config.h>

#include "groups.h"
#include <assert.h>
#include "bufs.h"
#include "sortcount.h"
#include <stdio.h>
#include <hre/runtime.h>

//#include "Dtaudlts.h"

#define MAXMANAGERGRAPH 24000000
//#define SOKOBAN

//#define VERBOSE

static int me, nodes;

#pragma GCC diagnostic ignored "-Wunused-variable"


void reduce_all (taudlts_t t, int* wscc, int* oscc){

 // assume me, nodes are already initialized

 char* workers;
 int j;
 MPI_Barrier(t->comm);
 workers=(char*)calloc(nodes, sizeof(char));	
 for(j = 0; j<nodes;j++) workers[j]=1;
 if (0==me)
	Warning(info,"\n\n@@@@@@@@ REDUCE ALL with manager 0 @@@@@@@@@@\n\n");
 taudlts_reduce_some(t, workers, 0, wscc, oscc);
 if (0==me)
	Warning(info,"\n\n@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n\n");
 MPI_Barrier(t->comm);
}





void reduce_pairs(taudlts_t t, int* wscc, int* oscc){

 // assume me, nodes are already initialized

 char* workers;
 int i;

 MPI_Barrier(t->comm);
 workers=(char*)calloc(nodes, sizeof(char));	
 if ((me < nodes-1)&&(me % 2 == 0)){
	workers[me]=workers[me+1]=1;
	Warning(info,"\n\n@@@@@@@@ REDUCE SOME with manager %d",me);
	taudlts_reduce_some(t, workers, me, wscc, oscc);
 }	
 else if (me % 2 != 0){
	workers[me]=workers[me-1]=1;
	taudlts_reduce_some(t, workers, me-1, wscc, oscc);
 }
 else{
	taudlts_reduce_some(t, workers, -1, wscc, oscc);
 }

 MPI_Barrier(t->comm);

 // taudlts_cleanup(t, wscc, oscc);
 taudlts_global_collapse(t, wscc, oscc);

 MPI_Barrier(t->comm);
 taudlts_global_collapse(t, wscc, oscc);
 // taudlts_cleanup(t, wscc, oscc);
 // print_status(t);
 free(workers);
}




void reduce_tree(taudlts_t t, int* wscc, int* oscc){

 // assume me, nodes are already initialized

 char* workers;
 int i,m, groupsize, maxgroupsize, tsize, mymanager;
 int *mysize, *size;

 MPI_Barrier(t->comm);
 workers=(char*)calloc(nodes, sizeof(char));	
 mysize=(int*)calloc(nodes, sizeof(int));
 size=(int*)calloc(nodes, sizeof(int));

 maxgroupsize=nodes/2;
 for (groupsize=2 ; groupsize <= maxgroupsize; groupsize++){	

	mymanager = (me/groupsize) * groupsize;
	Warning(info,"\n\nme %d manager %d ", me,mymanager);	
	 MPI_Barrier(t->comm);
	if (me==mymanager)
	 Warning(info,"\n\n@@@@@@@@ REDUCE SOME with manager %d and groupsize %d ",
					 me,groupsize);	
	for(i=0;i<nodes;i++) mysize[i] = t->M;
	MPI_Alltoall(mysize,1,MPI_INT,size,1,MPI_INT,t->comm); 
	tsize=0;
	for(i=mymanager;(i<mymanager+groupsize)&&(i<nodes);i++) tsize += size[i];
	MPI_Allreduce(&tsize, &i, 1, MPI_INT, MPI_MAX, t->comm);
	if (i >= MAXMANAGERGRAPH){
	 free(workers); free(mysize); free(size);
	 return;
	}

	for(i=0;i<nodes;i++) workers[i]=0;
	if (nodes-mymanager >= 2){
	 for(i=mymanager;(i<mymanager+groupsize)&&(i<nodes);i++)
		workers[i]=1;
	 taudlts_reduce_some(t, workers, mymanager, wscc, oscc);
	}
	else
	 taudlts_reduce_some(t, workers, -1, wscc, oscc);
	
	MPI_Barrier(t->comm);
	taudlts_global_collapse(t, wscc, oscc);
 }

 free(workers); free(mysize); free(size);
}

















int dlts_elim_tauscc_groups(dlts_t lts){
// detects and collapses the strongly connected components of lts
// formed with edges labelled lts->tau

 taudlts_t t, tviz;
 int *oscc, *wscc;
 int Mtot,i;
 rt_timer_t tau_timer;

 MPI_Comm_size(lts->comm, &nodes);
 MPI_Comm_rank(lts->comm, &me);
 
 tau_timer=RTcreateTimer();RTstartTimer(tau_timer);

 t = taudlts_create(lts->comm);
 taudlts_extract_from_dlts(t, lts);
 tviz = taudlts_create(t->comm);
 tviz->M=0; tviz->N = t->N;

 RTstopTimer(tau_timer);

 oscc=(int*)calloc(t->N,sizeof(int));	
 wscc=(int*)calloc(t->N,sizeof(int));
 for(i=0;i<t->N;i++) {
	oscc[i] = i;
	wscc[i] = me;
 }

 MPI_Allreduce(&(t->M), &Mtot, 1, MPI_INT, MPI_SUM, t->comm);

 //#ifdef SOKOBAN
 if (Mtot >= MAXMANAGERGRAPH){
	taudlts_elim_trivial(t, tviz, oscc);
	reduce_tree(t, wscc, oscc);
 }

 taudlts_cleanup(t, wscc, oscc);

 MPI_Allreduce(&(t->M), &Mtot, 1, MPI_INT, MPI_SUM, t->comm);
 if (me==0) Warning(info,"\n\nMtot:::: %d\n\n",Mtot);


 // if it's small enough, send it to one manager
 if (Mtot < MAXMANAGERGRAPH){	

	reduce_all(t, wscc, oscc);
	
	taudlts_scc_stabilize(t, wscc, oscc);

	RTstartTimer(tau_timer);

	taudlts_aux2normal(tviz);
	taudlts_cleanup(tviz, wscc, oscc);
	taudlts_insert_to_dlts(t, lts); taudlts_free(t);
	taudlts_insert_to_dlts(tviz, lts); taudlts_free(tviz);
	dlts_shuffle(lts, wscc, oscc); 
	dlts_shrink(lts, wscc, oscc, NULL);
	if (wscc!=NULL) {free(wscc); wscc=NULL;}
	// if (oscc!=NULL) {free(oscc); oscc=NULL;}

	RTstopTimer(tau_timer); RTprintTimer(info, tau_timer, "taugraph I/O: ");
	return 1;
 }
 // else, give up
 else{
	taudlts_free(t); taudlts_free(tviz);
	free(wscc); free(oscc); RTprintTimer(info, tau_timer, "taugraph I/O: ");
	return 0;
 }
}
