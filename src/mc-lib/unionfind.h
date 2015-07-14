#ifndef UNIONFIND_H
#define UNIONFIND_H

#include <pins2lts-mc/algorithm/algorithm.h>

/**
 * The structure could be 'optimized' with the following:
 * - sz_w should scale in size depending on the number of workers (W)
 *   (it is now chosen statically to cope with a maximum of 32 workers)
 * TODO: documentation
 */


typedef struct uf_s uf_t;

typedef enum pick_result {
    PICK_DEAD = 1,
    PICK_SUCCESS,
    PICK_MARK_DEAD,
} pick_e;

#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3
#define CLAIM_FOUND     4

extern uf_t     *uf_create ();

// successor handling

extern pick_e   uf_pick_from_list (const uf_t* uf, ref_t state, ref_t *node);

extern bool     uf_remove_from_list (const uf_t* uf, ref_t state);

extern int      uf_owner (const uf_t* uf, ref_t state, size_t worker);

extern bool     uf_is_in_list (const uf_t* uf, ref_t state);

extern char     uf_make_claim (const uf_t* uf, ref_t state, size_t w_id);

extern void     uf_merge_list(const uf_t* uf, ref_t list_x, ref_t list_y);

// 'basic' union find

extern ref_t     uf_find (const uf_t* uf, ref_t state);

extern bool      uf_sameset (const uf_t* uf, ref_t state_x, ref_t state_y);

extern bool      uf_union (const uf_t* uf, ref_t state_x, ref_t state_y);

// dead

extern bool      uf_mark_dead (const uf_t* uf, ref_t state);

extern bool      uf_is_dead (const uf_t* uf, ref_t state);

// testing

extern bool      uf_mark_undead (const uf_t* uf, ref_t state);

extern int       uf_print_list(const uf_t* uf, ref_t state);

extern int       uf_debug (const uf_t* uf, ref_t state);

extern void      uf_free (uf_t* uf);

#endif
