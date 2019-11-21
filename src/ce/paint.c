#include <hre/config.h>

#include "paint.h"
#include <assert.h>
#include "bufs.h"
#include "sortcount.h"
#include <stdio.h>
#include <hre/runtime.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

extern int me, nodes;

//#define VERBOSE       // computes also weights, largest SCC..
//#define DEBUG

// the number of vertices and arcs ..
// final
extern int Nfinal, Mfinal;
// hooked, i.e. on a cycle
extern int Nhooked, Mhooked;
// not yet classified as "final" or "hooked"
extern int Nleft, Mleft;

#define REACH_TAG 30
#define WEIGHT_TAG 40
#define MAX_MANAGER_GRAPH_SIZE 1000000

//
// THE BIG PAINTING EXPERIMENT !!!
//


//*************************************************************
//*************************************************************
//*************************************************************
//*************************************************************

void taudlts_normal2split
     (taudlts_t t, int** srcout, int* begin_to_w, 
			             int** destin, int* begin_from_w){

 int *size_to_w, *size_from_w, *tmp;
 int i,j;
 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me); 
 ///// MPI_Barrier(t->comm);

 if (((*srcout) = (int*)calloc(t->M, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in normal2split");
 if ((size_to_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in normal2split");
 if ((size_from_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in normal2split");
 for(i=0;i<t->N;i++)
	for(j=t->begin[i];j<t->begin[i+1];j++)
	 (*srcout)[j]=i;
 ComputeCount(t->w, t->M, begin_to_w);
 for(i=0;i<nodes;i++) size_to_w[i] = begin_to_w[i];
 Count2BeginIndex(begin_to_w, nodes);
 SortArray(srcout, t->M, t->w, begin_to_w, nodes);
 tmp=t->o;SortArray(&tmp, t->M, t->w, begin_to_w, nodes);t->o=tmp;
 free(t->w); t->w = NULL;

 MPI_Alltoall(size_to_w,1,MPI_INT,size_from_w,1,MPI_INT,t->comm); 
 begin_from_w[0]=0; 
 for(i=1;i<=nodes;i++) begin_from_w[i]=begin_from_w[i-1]+size_from_w[i-1];
 if (((*destin) = (int*)calloc(begin_from_w[nodes], sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in normal2split");
 MPI_Alltoallv(t->o, size_to_w, begin_to_w, MPI_INT,
							 (*destin), size_from_w, begin_from_w,MPI_INT,t->comm);
 free(t->o);t->o=NULL;
 free(size_to_w); free(size_from_w);
}



//*************************************************************
//*************************************************************
//*************************************************************
//*************************************************************

void taudlts_split2normal
           (taudlts_t t, int** srcout, int* begin_to_w, 
			             int** destin, int* begin_from_w){
 int *size_to_w, *size_from_w, *tmp;
 int i,j;
 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me); 
 ///// MPI_Barrier(t->comm);

 if ((size_to_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in split2normal");
 if ((size_from_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in split2normal");
 for(i=0;i<nodes;i++){
	size_to_w[i]=begin_to_w[i+1]-begin_to_w[i];
	size_from_w[i]=begin_from_w[i+1]-begin_from_w[i];
 }
 if ((t->o = (int*)calloc(t->M, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in split2normal");
 MPI_Alltoallv((*destin), size_from_w, begin_from_w, MPI_INT,
							 t->o, size_to_w, begin_to_w, MPI_INT, t->comm);
 free(*destin);*destin = NULL;
 free(size_to_w); free(size_from_w);

 if ((t->w = (int*)calloc(t->M, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in split2normal");
 for(i = 0; i < nodes; i++)
	for(j = begin_to_w[i]; j < begin_to_w[i+1]; j++)
	 t->w[j]=i;

 if ((t->begin=(int*)calloc(t->N+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in split2normal");
#ifdef DEBUG
 for(i=0;i<t->M;i++)
	assert((*srcout)[i] < t->N);
#endif
 ComputeCount((*srcout), t->M, t->begin);
 Count2BeginIndex(t->begin, t->N);
 tmp=t->w;SortArray(&tmp, t->M, (*srcout), t->begin, t->N);t->w=tmp;
 tmp=t->o;SortArray(&tmp, t->M, (*srcout), t->begin, t->N);t->o=tmp;
 free(*srcout); *srcout=NULL;
}





//******************************************************
//******************************************************
//******************************************************
//******************************************************
//******************************************************

void taudlts_paintfwd_all(taudlts_t t, int* color){

// input: t, a color scheme color_
//        some nodes may be not participating - they should have color=-2
// output: a stable color scheme

 int i,x,Nchanged,max,wto,wfrom,diff, itno;
 int *begin_to_w, *begin_from_w, *srcout, *destin, *colorout, *colorin;
 MPI_Request reqsend, reqrecv;
 MPI_Status stsend, strecv;

 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me); 
 ///// MPI_Barrier(t->comm);

 if ((begin_to_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd_all");
 if ((begin_from_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd_all");
 taudlts_normal2split(t, &srcout, begin_to_w, &destin, begin_from_w);
 // for(i=0;i<nodes;i++){
 //	Warning(info,"%3d: to %3d: %10d        from %3d: %10d\n",
 //					me,i,begin_to_w[i+1]-begin_to_w[i],
 //					i,begin_from_w[i+1]-begin_from_w[i]);
 // }
 max=0; for(i=0;i<nodes;i++) 
	if (max<(begin_to_w[i+1]-begin_to_w[i]))
	 max = begin_to_w[i+1]-begin_to_w[i];
 // Warning(info,"%3d: maxout=%d  ",me,max);
 if ((colorout=(int*)calloc(max, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd_all");
 max=0; for(i=0;i<nodes;i++) 
	if (max<(begin_from_w[i+1]-begin_from_w[i]))
	 max = begin_from_w[i+1]-begin_from_w[i];
 // Warning(info,"%3d: maxin=%d  ",me,max);
 if ((colorin=(int*)calloc(max, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd_all");

 Nchanged=1;itno=0;
 //// repeated part
 for(;Nchanged>0;){
	x=0;
	// re-colour own nodes
#ifdef DEBUG
	assert(begin_from_w[me+1]-begin_from_w[me] == begin_to_w[me+1]-begin_to_w[me]);
#endif
	//	Warning(info,"%3d: %d  own transitions beginto %d beginfrom %d \n",
	//					me,begin_from_w[me+1]-begin_from_w[me],begin_to_w[me],begin_from_w[me]);
	for(i=0; i < (begin_from_w[me+1]-begin_from_w[me]); i++){
#ifdef DEBUG
	 assert(begin_from_w[me]+i < begin_from_w[nodes]);
	 assert(destin[begin_from_w[me]+i] < t->N);
	 assert(begin_to_w[me]+i < t->M);
	 assert(srcout[begin_to_w[me]+i] < t->N);
#endif
	 //	 if (me>0) printf(" %d.%d.colorsc=%d.colordest=%d \n",
	 //							me,i,color[srcout[begin_to_w[me]+i]],
	 //							color[destin[begin_from_w[me]+i]]);
	 if ((color[destin[begin_from_w[me]+i]] >= -1)
			 &&(color[srcout[begin_to_w[me]+i]] >= 0))
		if ((color[destin[begin_from_w[me]+i]] == -1)
				||(color[destin[begin_from_w[me]+i]] > 
					 color[srcout[begin_to_w[me]+i]])){
		 color[destin[begin_from_w[me]+i]] = 
			color[srcout[begin_to_w[me]+i]]; 
		 x++;
		}
	}
	//	Warning(info,"%3d: coloured own nodes. x=%d\n",me,x);
	// exchange two by two. send to me+diff, recv from me-diff
	for (diff=1;diff<nodes;diff++){
	 wto = (me+diff)%nodes;
	 wfrom=(me-diff+nodes)%nodes;
	 for(i=0;i<begin_to_w[wto+1]-begin_to_w[wto];i++)
		colorout[i] = color[srcout[begin_to_w[wto]+i]];
	 MPI_Isend(colorout,
						 begin_to_w[wto+1]-begin_to_w[wto], MPI_INT, 
						 wto, REACH_TAG, t->comm, &reqsend);
	 MPI_Irecv(colorin, begin_from_w[wfrom+1]-begin_from_w[wfrom], MPI_INT,
						 wfrom, REACH_TAG, t->comm, &reqrecv);
	 MPI_Wait(&reqsend, &stsend);	 
	 MPI_Wait(&reqrecv, &strecv); 
	 for(i=0;i<begin_from_w[wfrom+1]-begin_from_w[wfrom];i++)
		if((color[destin[begin_from_w[wfrom]+i]] >= -1)&&(colorin[i]>=0))
		 if ((color[destin[begin_from_w[wfrom]+i]] == -1)
				 ||(color[destin[begin_from_w[wfrom]+i]] > colorin[i])){
			color[destin[begin_from_w[wfrom]+i]] = colorin[i];
			x++;
		 }
	}

	// decide whether to stop	
	MPI_Allreduce(&x, &Nchanged, 1, MPI_INT, MPI_SUM, t->comm);
#ifdef VERBOSE
	if (me==0) Warning(info,"Nchanged=%10d",Nchanged);
#endif
	itno++;
 } // end color iteration
 if (me==0) Warning(info,"there were %10d iterations",itno);
 free(colorin);free(colorout);
 //// back to the original form
 taudlts_split2normal(t, &srcout, begin_to_w, &destin, begin_from_w);
 free(begin_to_w);free(begin_from_w);
}

//*************************************************************
//*************************************************************
//*************************************************************
//*************************************************************

#define OWNCOLOR(w,o) (100*(o)+(w))
#define WORKER_FROM_COLOR(c) ((c)%100) 
#define OFFSET_FROM_COLOR(c) ((c)/100)


void taudlts_init_paint_owncolor(taudlts_t t, int* color){
 int i;
 // the -2 nodes are not painted
 MPI_Comm_rank(t->comm, &me); 
 for(i = 0; i < t->N; i++)
	if (color[i] != -2)
	 color[i] = OWNCOLOR(me,i);
}

void taudlts_paintfwd(taudlts_t t, int* color){
// input: color must be  allocated and filled with 
//            c >= 0 painted in the colour c
//            -1 not painted
//            -2 not participating
// output: a new colour scheme s.t.
//         for every participating node X
//         every participating node reachable from X
//         has X's colour
//         the -2 nodes remain -2
// some -1 nodes might remain -1

 int c,c0,c1,call;
 int *weight, *count_from_w, *begin, *count_to_w, *ooo, *tmp;
 int *wcolor, *ocolor;
 int i,j;
 MPI_Request* request_array;
 MPI_Status* status_array;

 ///// MPI_Barrier(t->comm);
 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me); 
 if (me==0) Warning(info,"\nPAINT  ALL");
 ///// MPI_Barrier(t->comm);

 // how many negatives
 c=0;c0=0;c1=0;
 for(i=0;i<t->N;i++){
	if (color[i]==-2) c++;
	else if(color[i]==-1) c0++;
	else c1++;
 }
 i=c;MPI_Reduce(&i, &c, 1, MPI_INT, MPI_SUM, 0, t->comm);
 i=c0;MPI_Reduce(&i, &c0, 1, MPI_INT, MPI_SUM, 0, t->comm);
 i=c1;MPI_Reduce(&i, &c1, 1, MPI_INT, MPI_SUM, 0, t->comm);
 if (me==0) 
	Warning(info,"%10d not participating   %10d unpainted   %10d coloured", 
					c,c0,c1);
 c=0;c0=0;c1=0;
 // colorify
 taudlts_paintfwd_all(t, color);

 MPI_Barrier(t->comm);
 if (me==0) Warning(info,"\nPAINT  ALL done. Now compute weights (if VERBOSE)");

#ifdef VERBOSE
 // count colour weights..
 if ((request_array = (MPI_Request*)calloc(2*nodes, sizeof(MPI_Request)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");
 if ((status_array = (MPI_Status*)calloc(2*nodes, sizeof(MPI_Status)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");
 if ((weight = (int*)calloc(t->N, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");
 if ((begin = (int*)calloc(nodes+2, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd"); 
 if ((count_to_w = (int*)calloc(nodes+2, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd"); 
 if ((count_from_w = (int*)calloc(nodes+2, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd"); 
 if ((wcolor=(int*)calloc(t->N, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd"); 
 if ((ocolor=(int*)calloc(t->N, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");

 Warning(info,"\n%d: allocated ",me);
 for(i=0;i<t->N;i++){
	if (color[i]<0){
	 wcolor[i]=nodes;
	 ocolor[i]=0;
	}
	else{
	 wcolor[i] = WORKER_FROM_COLOR(color[i]);
	 ocolor[i] = OFFSET_FROM_COLOR(color[i]);
#ifdef DEBUG
	 assert(wcolor[i]>=0);assert(wcolor[i]<nodes);
	 assert(ocolor[i]>=0);assert(ocolor[i]<t->N);
#endif
	}
 }
 // Warning(info,"\n%d: wcolor, ocolor computed ",me);
 ComputeCount(wcolor, t->N, begin);
 Warning(info,"\n%d: begin (count) computed: %d . ",me,begin[nodes]);
 MPI_Barrier(t->comm);
 i=begin[nodes];
 MPI_Reduce(&i, &c, 1, MPI_INT, MPI_SUM, 0, t->comm);
 if (me==0) 
	Warning(info,"%10d not participating or left unpainted  nodes", c);
 c=0;
 for(i=0;i<=nodes;i++) count_to_w[i]=begin[i];
 MPI_Barrier(t->comm);
 Warning(info,"\n%d: sss ",me);
 Count2BeginIndex(begin, nodes+1);
#ifdef DEBUG
 if (begin[nodes+1] != t->N)
			 Warning(info,"%3d: begin[nodes+1]=%d, t->N=%d",me,begin[nodes+1],t->N);
 assert(begin[nodes+1]==t->N);
#endif
 if ((ooo = (int*)calloc(t->N, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");
 for(i=0;i<t->N;i++) ooo[i]=ocolor[i]; 
 SortArray(&ooo, t->N, wcolor, begin, nodes+1);	
 i=0;
 MPI_Allreduce(&(t->N), &i, 1, MPI_INT, MPI_MAX, t->comm);
 MPI_Alltoall(count_to_w,1,MPI_INT,count_from_w,1,MPI_INT,t->comm);
 if ((tmp = (int*)calloc(i, sizeof(int)))==NULL)
	Fatal(1,error,"out of mem in paintfwd");
 Warning(info,"%d: ooo computed..  ",me);
 MPI_Barrier(t->comm);
 for(i=0;i<nodes;i++){
	MPI_Isend(ooo + begin[i], count_to_w[i], MPI_INT, 
						i, WEIGHT_TAG, t->comm, request_array);
	MPI_Irecv(tmp, count_from_w[i], MPI_INT,
						i, WEIGHT_TAG, t->comm,request_array+1);
	MPI_Wait(request_array, status_array);	 
	MPI_Wait(request_array+1, status_array+1);
	for(j=0;j<count_from_w[i];j++){
	 //assert(tmp[j]<t->N);
	 weight[tmp[j]]++;
	}
 }  
 free(tmp); free(ooo); free(begin);free(count_to_w); free(count_from_w); 	
 free(request_array); free(status_array);

 // centralize color statistics
 c=c1=0;
 for(i=0;i<t->N;i++){
	if (weight[i]>=1)
	 c++; 
	if (weight[i]==1){
	 //	 assert((wcolor[i]==me)&&(ocolor[i]==i));
	 c1++;
	}
 }

 i=c;MPI_Reduce(&i, &c, 1, MPI_INT, MPI_SUM, 0, t->comm);
 j=c1;MPI_Reduce(&j, &c1, 1, MPI_INT, MPI_SUM, 0, t->comm);
 if (me==0) Warning(info,"%10d colours(roots) %10d one-node colours (hidden trivial)", 
										c, c1); 
 c=i; c1=j;

 MPI_Barrier(t->comm);
 for (i=0;i<nodes;i++){
	if (i==me){
	 c0=0;
	 for(j=0;j<t->N;j++)
		if (ocolor[j] == -1) c0++;
	 Warning(info,"%3d: %10d trivial %10d roots %10d hidden trivial",
					 me,c0,c,c1);
	 for(j=0;j<t->N;j++)
		if (weight[j] >= MAX_MANAGER_GRAPH_SIZE)
		 Warning(info,"      root %10d size %10d\n",j,weight[j]);
	}
	///// MPI_Barrier(t->comm);
 }
 free(weight); free(wcolor); free(ocolor);
#endif
if (me==0) Warning(info,"\nPAINT  ALL FINISHED");
}

void dlts_fwd2back(dlts_t lts){

}

//*********************************
//*********************************
//*********************************
//*********************************
//*********************************

void taudlts_elim_mixed_transitions(taudlts_t t, taudlts_t tv, int* color){


 int diff,i,j,Aout,wto,wfrom,max;
 int *osrc, *tmp, *begin_to_w, *size_to_w, *size_from_w, *bufin, *bufcolor;
 char *deleted;
 MPI_Request reqsend, reqrecv;
 MPI_Status stsend, strecv;

 ///// MPI_Barrier(t->comm);
 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me); 
 if (me==0) Warning(info,"\nELIM MIXED TRANSITIONS");
 ///// MPI_Barrier(t->comm);

 // transform t->begin to osrc and sort on t->w
 if ((osrc = (int*)calloc(t->M, sizeof(int))) == NULL)
	Fatal(1,error,"out of memory in elim_mixed");
 for(i = 0; i < t->N; i++)
	for(j = t->begin[i];j < t->begin[i+1]; j++)
	 osrc[j] = i;
 free(t->begin);t->begin = NULL;
 if ((size_to_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of memory in elim_mixed");
 if ((begin_to_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
		 Fatal(1,error,"out of memory in elim_mixed");
 if ((size_from_w = (int*)calloc(nodes+1, sizeof(int)))==NULL)
	Fatal(1,error,"out of memory in elim_mixed");
 ComputeCount(t->w, t->M, begin_to_w);
 for(i=0;i<nodes;i++) size_to_w[i] = begin_to_w[i];
 Count2BeginIndex(begin_to_w, nodes);
 SortArray(&osrc, t->M, t->w, begin_to_w, nodes); 
 tmp = t->o;  SortArray(&tmp, t->M, t->w, begin_to_w, nodes); t->o = tmp; 
 free(t->w);
 MPI_Alltoall(size_to_w,1,MPI_INT,size_from_w,1,MPI_INT,t->comm);
 ///// MPI_Barrier(t->comm);
 // if (me==0) Warning(info,"sorted");
 // bufin, bufcolor
 max=0; for(i=0;i<nodes;i++) 
	if (max<(begin_to_w[i+1]-begin_to_w[i]))
	 max = begin_to_w[i+1]-begin_to_w[i];
 if ((bufcolor=(int*)calloc(max, sizeof(int)))==NULL)
	Fatal(1,error,"out of memory in elim_mixed");
 max=0; for(i=0;i<nodes;i++) 
	if (max<(size_from_w[i]))
	 max = size_from_w[i];
 if ((bufin = (int*)calloc(max, sizeof(int))) == NULL)
	Fatal(1,error,"out of memory in elim_mixed");
 Aout=0;
 ///// MPI_Barrier(t->comm);
 // if (me==0) Warning(info,"allocated bufs");
 // solve transitions to own nodes : not needed, solved below with diff=0
 
 // exchange two by two. send to me+diff, recv from me-diff
 for (diff=0;diff<nodes;diff++){
	wto = (me+diff)%nodes;
	wfrom=(me-diff+nodes)%nodes;
	//	Warning(info,"%3d: diff=%d wto: %3d begin %d size %d, wfrom %3d size %d\n", 
	//			me, diff, wto, begin_to_w[wto], size_to_w[wto], 
	//			wfrom, size_from_w[wfrom]);
	MPI_Isend(t->o+begin_to_w[wto],
						size_to_w[wto], MPI_INT, 
						wto, REACH_TAG, t->comm, &reqsend);
	MPI_Irecv(bufin, size_from_w[wfrom], MPI_INT,
						wfrom, REACH_TAG, t->comm, &reqrecv);
	MPI_Wait(&reqsend, &stsend);	 
	MPI_Wait(&reqrecv, &strecv);  
	for(i = 0; i < size_from_w[wfrom]; i++){
	 //	 assert((bufin[i] >= 0) && (bufin[i] < t->N));
	 bufin[i] = color[bufin[i]];
	}
	MPI_Isend(bufin,
						size_from_w[wfrom], MPI_INT, 
						wfrom, REACH_TAG, t->comm, &reqsend);
	MPI_Irecv(bufcolor, size_to_w[wto], MPI_INT,
						wto, REACH_TAG, t->comm, &reqrecv);
	MPI_Wait(&reqsend, &stsend);	 
	MPI_Wait(&reqrecv, &strecv);  	
	// count the new mixed transitions
	//	Warning(info,"bla");
	max=0;
	for(i = 0; i < size_to_w[wto]; i++)
	 if (color[osrc[begin_to_w[wto]+i]] != bufcolor[i])
		max++;

#ifdef VERBOSE
	///// MPI_Barrier(t->comm);
	MPI_Reduce(&max, &i, 1, MPI_INT, MPI_SUM, 0, t->comm);
	if (me==0) Warning(info,"nog %d final (diff=%d)",i,diff);
#endif

	max += tv->M+1;
	if ((tv->begin = (int*)realloc(tv->begin, sizeof(int) * max)) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");
	if ((tv->w = (int*)realloc(tv->w, sizeof(int) * max)) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");
	if ((tv->o = (int*)realloc(tv->o, sizeof(int) * max)) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");

	// then mark them
	for(i = 0; i < size_to_w[wto]; i++)
	 if (color[osrc[begin_to_w[wto]+i]] != bufcolor[i]){
		tv->begin[tv->M] = osrc[begin_to_w[wto]+i];
		tv->w[tv->M] = wto;
    tv->o[tv->M++] = t->o[begin_to_w[wto]+i];
		t->o[begin_to_w[wto]+i] = -1; // as mark that it has to be deleted
		Aout++;
	 }
 }
 ///// MPI_Barrier(t->comm);
 // re-build t
 free(size_to_w); free(bufin); free(bufcolor);free(size_from_w);
 // 	Warning(info,"%3d: rebuild", me);
 if ((t->w = (int*)calloc(t->M, sizeof(int))) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");
 for(i = 0; i < nodes; i++) 
	for (j = begin_to_w[i]; j < begin_to_w[i+1]; j++)
	 t->w[j]=i;
 free(begin_to_w);
 if ((t->begin=(int*)calloc(t->N+1, sizeof(int))) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");
 ComputeCount(osrc, t->M, t->begin);
 Count2BeginIndex(t->begin, t->N);
 tmp=t->w;SortArray(&tmp, t->M, osrc, t->begin, t->N);t->w=tmp;
 tmp=t->o;SortArray(&tmp, t->M, osrc, t->begin, t->N);t->o=tmp;
 free(osrc);
#ifdef DEBUG
 for(i = 0; i < t->N; i++){
	assert(t->begin[i]>=0);
	assert(t->begin[i]<=t->M);
 }
#endif

 // really delete
 if ((deleted = (char*)calloc(t->M, sizeof(char))) == NULL)
	 Fatal(1,error,"out of memory in elim_mixed");
 for(i = 0; i < t->M; i++)
	if (t->o[i] == -1) deleted[i] = 1;
 taudlts_delete_transitions(t, deleted);
 free(deleted);
#ifdef DEBUG
 for(i = 0; i < t->N; i++){
	assert(t->begin[i]>=0);
	assert(t->begin[i]<=t->M);
 }
#endif
 // statistics
 MPI_Reduce(&Aout, &i, 1, MPI_INT, MPI_SUM, 0, t->comm);
 Mfinal += Aout; Mleft -= Aout;
 if (me==0) Warning(info,"%d new final arcs\nEND ELIM MIXED TRANSITIONS",i);
}





void taudlts_decapitate(taudlts_t t, int* cf, int* wscc, int* oscc){
 // this only modifies scc 
 // and cf - the nodes newly found on cycles get cf = -2 (including the roots)
 // doesn't affect t 
 int i, Vout, other;
 int* cb;

 ///// MPI_Barrier(t->comm);
 MPI_Comm_size(t->comm, &nodes);
 MPI_Comm_rank(t->comm, &me);

 if (me==0) Warning(info,"\nHEADS OFF");
 if ((cb = (int*)calloc(t->N,sizeof(int)))==NULL)
	Fatal(1,error,"out of memory in decapitate");

 Vout=other=0;
 for(i = 0; i < t->N; i++) {
	//	Warning(info,"%d.%d : owncolor is %d, cf is %d    ",
	//					me,i,OWNCOLOR(me,i), cf[i]);
	if (cf[i] == OWNCOLOR(me,i)) cb[i] = cf[i];
	else cb[i] = -1;
 }

 taudlts_paintfwd(t,cb);

 for(i = 0; i < t->N; i++)
	if (cb[i] >= 0){
	 if (cf[i] == cb[i]){
		Vout++;		
		wscc[i] = WORKER_FROM_COLOR(cf[i]);
		oscc[i] = OFFSET_FROM_COLOR(cf[i]);
		cf[i] = -2;
		if ((wscc[i] != me) || (oscc[i] != i)){
		 Nhooked++; Nleft--;
		}
	 }
	 else 
		other++;
	}
 free(cb);
 // print statistics
 ///// MPI_Barrier(t->comm);

#ifdef VERBOSE
 for (i=0;i<nodes;i++){
	if (i==me){
	 Warning(info,"%3d: %10d nodes in root SCCs  %10d painted back in a different colour",
					 me, Vout, other);
	}
	///// MPI_Barrier(t->comm);
 }
#endif
}









//********************************************************
void extreme_colours(taudlts_t t, taudlts_t tviz, int* wscc, int* oscc){
//********************************************************

 taudlts_t taux;
 int* cf;
 int i,m,cno, me;

 MPI_Comm_rank(t->comm, &me);

 taux = taudlts_create(t->comm);
 taux->M=0; taux->N = t->N;	

 cf=(int*)calloc(t->N,sizeof(int));	
 m=1;
 cno=0;
 while (m > 0){

	if (me==0) Warning(info,"\n\n___________________________________________\ncolour round no. %10d\n____________",cno++);

	taux->N = t->N;
	taudlts_elim_trivial(t, taux, oscc); 
	taudlts_simple_join(tviz, taux); taux->N = t->N;
	taudlts_fwd2back(t);
	for(i=0;i<t->N;i++)
	 if (t->begin[i]==t->begin[i+1]) cf[i]=-2;
	taux->N = t->N;
	taudlts_elim_trivial(t, taux, oscc); 
	taudlts_aux2normal(taux); 
	taudlts_fwd2back(taux); 
	taudlts_normal2aux(taux);
	taudlts_simple_join(tviz, taux); taux->N = t->N;
	taudlts_fwd2back(t);
	for(i=0;i<t->N;i++)
	 if (t->begin[i] == t->begin[i+1]) cf[i]=-2;

	taudlts_init_paint_owncolor(t,cf);
	taudlts_paintfwd(t,cf);
	taudlts_elim_mixed_transitions(t, tviz, cf);

	taudlts_fwd2back(t);
	taudlts_decapitate(t, cf, wscc, oscc);
	taudlts_fwd2back(t);
	taudlts_elim_mixed_transitions(t, tviz, cf);
	taudlts_cleanup(t, wscc, oscc);

	//	print_status(t);

	MPI_Allreduce(&Mleft, &m, 1, MPI_INT, MPI_SUM, t->comm);
 }

 free(cf);
 taudlts_free(taux);

 // taudlts_write(t,"twrong.aut");
}



//********************************************************************
//**********************************************
//**********************************************


void dlts_elim_tauscc_colours(dlts_t lts){

// detects and collapses the strongly connected 
// components of lts
// formed with edges labelled lts->tau

 taudlts_t t, tviz;
 int i;
 int *oscc, *wscc;
 int* weight = NULL;
 rt_timer_t bugtimer;

 bugtimer=RTcreateTimer();RTstartTimer(bugtimer);

 t = taudlts_create(lts->comm);
 taudlts_extract_from_dlts(t, lts);
 
 Nfinal = Mfinal = Nhooked = Mhooked = 0;
 Nleft = t->N; Mleft = t->M;
 
 // for tviz and taux, _begin_ is array of local sources of transitions..
 // i.o.t. avoid allocating a size N array..
 tviz = taudlts_create(t->comm);
 tviz->M=0; tviz->N = t->N;
 oscc=(int*)calloc(t->N,sizeof(int));	
 wscc=(int*)calloc(t->N,sizeof(int));
 for(i=0;i<t->N;i++) {
	oscc[i] = i;
	wscc[i] = me;
 }
#ifdef DEBUG
 RTreportTimer(bugtimer, "taugraph IN: ");
#endif
 
 extreme_colours(t, tviz, wscc, oscc);

 
#ifdef DEBUG
 Warning(info,"ec finished");
#endif
 taudlts_aux2normal(tviz);
 taudlts_cleanup(tviz, wscc, oscc);

 print_status(t);

#ifdef DEBUG
 RTreportTimer(bugtimer, "E2 finished: ");
#endif
 
 // insert t and tviz in taudlts (???? is inserting t needed ???) 
 taudlts_insert_to_dlts(t, lts); taudlts_free(t);
 taudlts_insert_to_dlts(tviz, lts); taudlts_free(tviz);
#ifdef DEBUG
 Warning(info,"real taus inserted");
#endif

 dlts_shuffle(lts, wscc, oscc); 
#ifdef DEBUG
 Warning(info,"shuffled");
 RTreportTimer(bugtimer, "shuffled: ");
#endif

#ifdef VERBOSE
 dlts_shrink(lts, wscc, oscc, &weight);
#else
 dlts_shrink(lts, wscc, oscc, NULL);
#endif

#ifdef DEBUG
 Warning(info,"shrinked");
 RTreportTimer(bugtimer, "shrinked: ");
#endif
 if (weight!=NULL) {free(weight); weight=NULL;}
 if (wscc!=NULL) {free(wscc); wscc=NULL;}
 // if (oscc!=NULL) {free(oscc); oscc=NULL;}
}




