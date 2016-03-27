/**
 *
 */

#ifndef LTL_H
#define LTL_H

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins-lib/pins2pins-ltl.h>
#include <util-lib/fast_set.h>

extern struct poptOption ndfs_options[];
extern struct poptOption owcty_options[];
extern struct poptOption alg_ltl_options[];

extern int              ecd;


typedef union trace_info_u {
    struct val_s {
        ref_t           ref;
        lattice_t       lattice;
    } val;
    char                data[16];
} trace_info_t;

extern void find_and_write_dfs_stack_trace (model_t model, dfs_stack_t stack,
                                            bool is_lasso);

extern void ndfs_report_cycle (run_t *run, model_t model, dfs_stack_t stack,
                               state_info_t *cycle_closing_state);

static inline bool
ecd_has_state (fset_t *table, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    int seen = fset_find (table, &hash, &s->ref, NULL, false);
    HREassert (seen != FSET_FULL);
    return seen;
}

static inline uint32_t
ecd_get_state (fset_t *table, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    uint32_t           *p;
    int res = fset_find (table, &hash, &s->ref, (void **)&p, false);
    HREassert (res != FSET_FULL, "ECD table full");
    return res ? *p : UINT32_MAX;
}

static inline void
ecd_add_state (fset_t *table, state_info_t *s, size_t *level)
{
    Debug ("Adding %zu", s->ref);
    uint32_t           *data;
    hash32_t            hash = ref_hash (s->ref);
    int res = fset_find (table, &hash, &s->ref, (void**)&data, true);
    HREassert (res != FSET_FULL, "ECD table full");
    HREassert (!res, "Element %zu already in ECD table", s->ref);
    if (level != NULL && !res) {
        HREassert (*level < UINT32_MAX, "Stack length overflow for ECD");
        *data = *level;
    }
}

static inline void
ecd_remove_state (fset_t *table, state_info_t *s)
{
    Debug ("Removing %zu", s->ref);
    hash32_t            hash = ref_hash (s->ref);
    int success = fset_delete (table, &hash, &s->ref);
    HREassert (success, "Could not remove key from set");
}

#endif // LTL_H
