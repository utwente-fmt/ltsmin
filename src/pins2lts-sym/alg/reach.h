#ifndef ALG_SEARCH_H
#define ALG_SEARCH_H

#include <pins-lib/pins.h>
#include <vset-lib/vector_set.h>


typedef void (*reach_proc_t)(vset_t visited, vset_t visited_old,
                             bitvector_t *reach_groups,
                             long *eg_count, long *next_count, long *guard_count);

typedef void (*sat_proc_t)(reach_proc_t reach_proc, vset_t visited,
                           bitvector_t *reach_groups,
                           long *eg_count, long *next_count, long *guard_count);

typedef void (*guided_proc_t)(sat_proc_t sat_proc, reach_proc_t reach_proc,
                              vset_t visited, char *etf_output);

extern void reach_none(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                       long *eg_count, long *next_count, long *guard_count);

extern void reach_no_sat(reach_proc_t reach_proc, vset_t visited, bitvector_t *reach_groups,
                         long *eg_count, long *next_count, long *guard_count);

extern void unguided(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
                     char *etf_output);

extern void directed(sat_proc_t sat_proc, reach_proc_t reach_proc,
                     vset_t visited, char *etf_output);

extern void reach_chain_stop();

extern void reach_stop (struct reach_s *node);

#endif //ALG_SEARCH_H
