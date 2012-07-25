/*
Library for distribution-aware manipulation of LTSs. 
Meant to replace "dlts.h".

LOG
start: miercuri 24 septembrie
*/

#ifndef DLTS_H
#define DLTS_H

#include <sys/types.h>
#include <mpi.h>

typedef struct dlts {
 MPI_Comm comm;
 char *dirname;
 char *info;
 int segment_count;
 int root_seg;
 int root_ofs;
 int label_count;
 int tau;
 int *state_count;
 int **transition_count;
 char **label_string;
 int ***src;
 int ***label;
 int ***dest;
} *dlts_t;

dlts_t dlts_read(MPI_Comm communicator, char* filename);

void dlts_free(dlts_t lts);

void dlts_writedir(dlts_t lts, char* filename);

// both write functions
// expect the following fields of the lts structure to be filled (with correct data):
//   comm
//   root_seg
//   root_ofs
//   label_count
//   tau
//   state_count[me]
//   transition_count[me][i], forall i
//   label_string
//   src[me][i], forall i
//   label[me][i], forall i
//   dest[me][i], forall i
//   !!! it is used that lts->segment_count = no. nodes in lts->comm

/*****************  
TO ADD:
 - dlts_write functions for other "types" (other organizations of data, for instance
      when the lts is filled with the incoming transitions..)
 - dlts_balance function to migrate states between workers to make them
      more or less balanced 

*******************/

#endif
