/**
 * Union-Find structure used for Renault's parallel Tarjan SCC algorithm
 *
 * TODO: remove rank in the implementation?
 *       if so, refer to paper discussing random linking
 */

#ifndef RENAULT_UNIONFIND_H
#define RENAULT_UNIONFIND_H

#include <pins2lts-mc/algorithm/algorithm.h>

/**
 * The structure could be 'optimized' with the following:
 * - sz_w should scale in size depending on the number of workers (W)
 *   (it is now chosen statically to cope with a maximum of 32 workers)
 * TODO: documentation
 */

typedef struct r_uf_s r_uf_t;

#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3

extern r_uf_t   *r_uf_create ();

// successor handling

extern char      r_uf_make_claim (const r_uf_t* uf, ref_t state);

// 'basic' union find

extern ref_t     r_uf_find (const r_uf_t* uf, ref_t state);

extern bool      r_uf_sameset (const r_uf_t* uf, ref_t state_x, ref_t state_y);

extern void      r_uf_union (const r_uf_t* uf, ref_t state_x, ref_t state_y);

// dead

extern bool      r_uf_mark_dead (const r_uf_t* uf, ref_t state);

extern bool      r_uf_is_dead (const r_uf_t* uf, ref_t state);

// locking

extern ref_t     r_uf_lock (const r_uf_t* uf, ref_t state_x, ref_t state_y);

extern void      r_uf_unlock (const r_uf_t* uf, ref_t state);

// testing

extern bool      r_uf_mark_undead (const r_uf_t* uf, ref_t state);

extern void      r_uf_debug (const r_uf_t* uf, ref_t state);

extern void      r_uf_free (r_uf_t* uf);

#endif
