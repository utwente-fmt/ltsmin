#ifndef UNIONFIND_H
#define UNIONFIND_H

#include <pins2lts-mc/algorithm/algorithm.h>

/**
 * The structure could be 'optimized' with the following:
 * - sz_w should scale in size depending on the number of workers (W)
 *   (it is now chosen statically to cope with a maximum of 32 workers)
 * TODO: documentation
 */

typedef uint64_t    sz_w;

typedef struct uf_node_s {
    ref_t           parent;         // The parent in the UF tree
    unsigned char   rank;           // The height of the UF tree
    sz_w            w_set;          // Set of worker IDs (one bit for each worker)
    char            uf_status;      // {UNSEEN, INIT, LIVE, LOCKED, DEAD}
    char            list_status;    // {LIVE, BUSY, REMOVED}
    ref_t           list_next;      // next list `pointer' (we could also use a standard pointer)
} uf_node_t;


struct uf_s {
    uf_node_t      *array;   // array: [ref_t] -> uf_node
};
typedef struct uf_s uf_t;

#define PICK_DEAD       1
#define PICK_SUCCESS    2
#define PICK_MARK_DEAD  3

#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3
#define CLAIM_FOUND     4

extern uf_t     *uf_create ();

// successor handling

extern char     uf_pick_from_list (const uf_t* uf, ref_t state, ref_t *node);

extern void     uf_remove_from_list (const uf_t* uf, ref_t state);

extern bool     uf_is_in_list (const uf_t* uf, ref_t state);

extern char     uf_make_claim (const uf_t* uf, ref_t state, sz_w w_id);

extern void     uf_merge_list(const uf_t* uf, ref_t list_x, ref_t list_y);

// 'basic' union find

extern ref_t     uf_find (const uf_t* uf, ref_t state);

extern bool      uf_sameset (const uf_t* uf, ref_t state_x, ref_t state_y);

extern void      uf_union (const uf_t* uf, ref_t state_x, ref_t state_y);

// dead

extern bool      uf_mark_dead (const uf_t* uf, ref_t state);

extern bool      uf_is_dead (const uf_t* uf, ref_t state);

// locking

extern ref_t     uf_lock (const uf_t* uf, ref_t state_x, ref_t state_y);

extern void      uf_unlock (const uf_t* uf, ref_t state);

// testing

extern bool      uf_mark_undead (const uf_t* uf, ref_t state);

extern char     *uf_get_str_w_set(sz_w w_set);

extern void      uf_debug (const uf_t* uf, ref_t state);

extern void      uf_free (uf_t* uf);

#endif