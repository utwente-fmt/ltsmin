/**
 *
 */

#ifndef LTL_H
#define LTL_H

#include <pins2lts-mc/algorithm/algorithm.h>
#include <util-lib/fast_set.h>

extern struct poptOption ndfs_options[];
extern struct poptOption owcty_options[];

extern int              ecd;

extern struct poptOption alg_ltl_options[];

static inline bool
ecd_has_state (fset_t *table, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    return fset_find (table, &hash, &s->ref, NULL, false);
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
    //Warning (info, "Adding %zu", s->ref);
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
    //Warning (info, "Removing %zu", s->ref);
    hash32_t            hash = ref_hash (s->ref);
    int success = fset_delete (table, &hash, &s->ref);
    HREassert (success, "Could not remove key from set");
}

#endif // LTL_H
