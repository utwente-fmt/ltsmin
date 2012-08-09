// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef SIGMIN_ARRAY_H
#define SIGMIN_ARRAY_H

#include <ltsmin-reduce-dist/seg-lts.h>
#include <ltsmin-reduce-dist/sig-array.h>

/**
\brief Compute a sig array equivalence relation.
*/
extern sig_id_t *SAcomputeEquivalence(seg_lts_t lts,const char* equivalence);

#endif
