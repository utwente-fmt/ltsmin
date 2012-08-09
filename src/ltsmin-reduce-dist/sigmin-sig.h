// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef SIGMIN_SIG_H
#define SIGMIN_SIG_H

#include <stdint.h>

#include <util-lib/chunk_support.h>

/**
\file sigmin-sig.h
\brief Definitions of signatures for various bisimulations.
*/


typedef uint32_t partition_id;

/**
\brief signature function.
\param state_id The number of the partition of which state is a member in the current partition.
\param state_lbl Pointer to the state labeling of the current state.
\param count The number of edges.
\param succ_lbl Array of pointers to the edge labelings of each edge.
\param succ_id  Array of partition numbers of successors with respect to the current partition id.
\param succ_sig Array of signatures of the successors.
\param succ_dep Array of dependencies on successor signatures.
 */
typedef chunk(*chunk_sig_fun)(
    partition_id state_id,
    void* state_lbl,
    int count,
    void* *succ_lbl,
    partition_id *succ_id,
    chunk *succ_sig,
    char *succ_dep
);

#endif
