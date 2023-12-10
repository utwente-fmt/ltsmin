#ifndef AUX_PROP_H
#define AUX_PROP_H

#include <pins2lts-sym/aux/options.h>
#include <vset-lib/vector_set.h>


typedef struct rel_expr_info {
    int* vec; // a long vector to use for expanding short vectors
    int len; // number of dependencies in this relational expression
    int* deps; // the dependencies in this relational expression
    ltsmin_expr_t e; // the relation expression
    ltsmin_parse_env_t env; // its environment
} rel_expr_info_t;

extern void rel_expr_cb(vset_t set, void *context, int *e);

extern void init_action_detection();

extern void init_invariant_detection();

extern void inv_info_prepare(ltsmin_expr_t e, ltsmin_parse_env_t env, int i);

extern void find_trace(int trace_end[][N], int end_count, int level,
                       vset_t *levels, char* file_prefix);

extern void find_action(int* src, int* dst, int* cpy, int group, char* action);

extern void check_invariants(vset_t set, int level);

extern void deadlock_check(vset_t deadlocks, bitvector_t *reach_groups);

#endif //AUX_PROP_H
