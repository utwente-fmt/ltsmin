// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef LOWMEM_H
#define LOWMEM_H

#include <lts-lib/lts.h>
#include <util-lib/bitset.h>

extern void lowmem_strong_reduce(lts_t lts);

extern void lowmem_branching_reduce(lts_t lts,bitset_t diverging);

extern void lowmem_lumping_reduce(lts_t lts);

extern void lts_quotient(lts_t lts,uint32_t *map);

#endif

