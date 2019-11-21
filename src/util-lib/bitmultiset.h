#ifndef BIT_MULTI_SET_H
#define BIT_MULTI_SET_H

#include <hre/user.h>
#include <util-lib/util.h>

typedef struct bms_s {
    char           *set;
    ci_list       **lists;
    int             corrupt_stack;
    size_t          types;
    size_t          elements;
} bms_t;

static inline bool
bms_has (bms_t *bms, int set, int u)
{
    return (bms->set[ u ] & (1<<set)) != 0;
}

static inline int
bms_count (bms_t *bms, int set)
{
    return bms->lists[set]->count;
}

static inline int
bms_get (bms_t *bms, int set, int idx)
{
    return bms->lists[set]->data[idx];
}

static inline void
bms_clear (bms_t *bms, int set)
{
    bms->corrupt_stack = bms->lists[set]->count != 0;
    bms->lists[set]->count = 0;
}


static inline void
bms_debug_1 (bms_t *bms, int set)
{
    ci_debug (bms->lists[set]);
}

static inline void
bms_debug (bms_t *bms)
{
    if (debug == NULL) return;

    if (bms->types == 1) {
        bms_debug_1 (bms, 0);
    } else {
        for (size_t i = 0; i < bms->types; i++) {
            Printf (debug, "BMS List: %zu\n", i);
            bms_debug_1 (bms, i);
            Printf (debug, "\n");
        }
    }
}

static inline ci_list *
bms_list (bms_t *bms, int set)
{
    return bms->lists[set];
}

static inline void
bms_push (bms_t *bms, int set, int u)
{
    bms->lists[set]->data[ bms->lists[set]->count++ ] = u;
}

static inline void
bms_push_if (bms_t *bms, int set, int u, int condition)
{
    bms->lists[set]->data[ bms->lists[set]->count ] = u;
    bms->lists[set]->count += condition != 0;
}

static inline bool
bms_push_new (bms_t *bms, int set, int u)
{
    int set0 = 1 << set;
    int seen = (bms->set[ u ] & set0) != 0;
    bms->set[ u ] |= set0;
    bms_push_if (bms, set, u, !seen);
    return !seen;
}

static inline int
bms_pop (bms_t *bms, int set)
{
    int set0 = 1 << set;
    HREassert (bms_count(bms, set) != 0 && !(bms->corrupt_stack & set0),
               "Pop on %s set stack %d", bms->corrupt_stack & set0 ? "corrupt" : "empty", set);
    int v = bms->lists[set]->data[ --bms->lists[set]->count ];
    bms->set[ v ] &= ~set0;
    return v;
}

static inline int
bms_top (bms_t *bms, int set)
{
    HREassert (bms_count(bms, set) != 0, "Pop on empty set stack %d", set);
    int v = bms->lists[set]->data[ bms->lists[set]->count - 1 ];
    return v;
}

// Messes up stack, only stack counter is maintained. Not to be combined with pop!
static inline bool
bms_rem (bms_t *bms, int set, int u)
{
    int set0 = 1 << set;
    int seen = (bms->set[ u ] & set0) != 0;
    bms->set[u] &= ~set0;
    bms->lists[set]->count -= seen;
    bms->corrupt_stack |= set0;
    return seen;
}

extern void bms_and_or_all (bms_t *bms, int and1, int and2, int or);

extern void bms_set_all (bms_t *bms, int set);

extern void bms_clear_all(bms_t *bms);

extern void bms_clear_lists(bms_t *bms);

extern bms_t *bms_create (size_t elements, size_t types);

#endif // BIT_MULTI_SET_H
