#ifndef ALG_SAT_H
#define ALG_SAT_H

#include <pins-lib/pins.h>
#include <pins2lts-sym/alg/reach.h>
#include <vset-lib/vector_set.h>


extern void reach_sat_loop(reach_proc_t reach_proc, vset_t visited,
                           bitvector_t *reach_groups, long *eg_count,
                           long *next_count, long *guard_count);

extern void reach_sat_fix(reach_proc_t reach_proc, vset_t visited,
                          bitvector_t *reach_groups, long *eg_count,
                          long *next_count, long *guard_count);

extern void reach_sat_like(reach_proc_t reach_proc, vset_t visited,
                           bitvector_t *reach_groups, long *eg_count,
                           long *next_count, long *guard_count);

extern void reach_sat(reach_proc_t reach_proc, vset_t visited,
                      bitvector_t *reach_groups, long *eg_count,
                      long *next_count, long *guard_count);

#endif //ALG_SAT_H
