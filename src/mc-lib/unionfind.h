/**
 * Lockless Union-Find structure for the UFSCC algorithm
 *
 * See renault_unionfind.h for information regarding the 'standard' lockless
 * union-find structure. For a description on the extensions applied to this
 * structure, we refer to REPORT.
 *
 *
 * TODO: add REPORT
 *
 */

#ifndef UNIONFIND_H
#define UNIONFIND_H

#include <pins2lts-mc/algorithm/algorithm.h>

typedef struct uf_s        uf_t;
typedef struct uf_state_s  uf_state_t;

typedef enum pick_result {
    PICK_DEAD         = 1,
    PICK_SUCCESS      = 2,
    PICK_MARK_DEAD    = 3,
} pick_e;

#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3
#define CLAIM_FOUND     4

extern uf_t     *uf_create ();

/* **************************** list operations **************************** */

extern bool      uf_is_in_list (const uf_t *uf, ref_t state);

extern pick_e    uf_pick_from_list (const uf_t *uf, ref_t state, ref_t *ret);

extern bool      uf_remove_from_list (const uf_t *uf, ref_t state);

/* ********************* 'basic' union find operations ********************* */

// TODO: is this necessary?
extern int       uf_owner (const uf_t *uf, ref_t state, size_t worker);

extern char      uf_make_claim (const uf_t *uf, ref_t state, size_t w_id);

extern ref_t     uf_find (const uf_t *uf, ref_t state);

extern bool      uf_sameset (const uf_t *uf, ref_t a, ref_t b);

extern bool      uf_union (const uf_t *uf, ref_t a, ref_t b);

/* ******************************* dead SCC ******************************** */

extern bool      uf_is_dead (const uf_t *uf, ref_t state);

extern bool      uf_mark_dead (const uf_t *uf, ref_t state);

// permanently locks a UF node, regardless of previous value
bool             uf_try_grab (const uf_t *uf, ref_t a);

/* **************************** TGBA acceptance **************************** */

extern uint32_t  uf_get_acc (const uf_t *uf, ref_t state);

extern uint32_t  uf_add_acc (const uf_t *uf, ref_t state, uint32_t acc);

/* ******************************** testing ******************************** */

extern ref_t     uf_debug (const uf_t *uf, ref_t state);

extern ref_t     uf_debug_list (const uf_t *uf, ref_t state);

#endif /* UNIONFIND_H */
