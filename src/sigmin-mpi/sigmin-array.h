// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef SIGMIN_ARRAY_H
#define SIGMIN_ARRAY_H

#include <sigmin-mpi/seg-lts.h>
#include <sigmin-mpi/sig-array.h>

/**
\brief Compute a sig array equivalence relation.
*/
extern sig_id_t *SAcomputeEquivalence(seg_lts_t lts,const char* equivalence);

#endif
