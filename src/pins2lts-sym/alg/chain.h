#ifndef ALG_CHAIN_H
#define ALG_CHAIN_H

#include <pins-lib/pins.h>
#include <pins2lts-sym/alg/reach.h>
#include <vset-lib/vector_set.h>


extern void reach_chain_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                             long *eg_count, long *next_count, long *guard_count);

extern void reach_chain(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                        long *eg_count, long *next_count, long *guard_count);

#endif // ALG_CHAIN_H
