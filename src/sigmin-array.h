#include <seg-lts.h>
#include <sig-array.h>

#ifndef SIGMIN_ARRAY_H
#define SIGMIN_ARRAY_H

/**
\brief Compute a sig array equivalence relation.
*/
extern sig_id_t *SAcomputeEquivalence(seg_lts_t lts,const char* equivalence);

#endif

