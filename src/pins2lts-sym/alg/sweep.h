#ifndef ALG_SWEEP_H
#define ALG_SWEEP_H

#include <pins2lts-sym/alg/reach.h>
#include <vset-lib/vector_set.h>


extern void sweep_vset_next (vset_t dst, vset_t src, int group);

extern void sweep_search(sat_proc_t sat_proc, reach_proc_t reach_proc,
                         vset_t visited, char *etf_output);

extern void sweep_print_lines ();

#endif //ALG_SWEEP_H
