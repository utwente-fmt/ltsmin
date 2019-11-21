/**
 * Lockless Union-Find structure for Renault's parallel Tarjan SCC algorithm
 *
 * Implementation of a lockless union-find structure based on the one described
 * by Anderson and Woll. We use path-compression to reduce the tree heights.
 *
 * As noted by Goel et. al, there is no need for the rank field in the UF
 * states. Instead, we utilize that state reference indexes are distributed
 * randomly as a result of the hashing scheme. Thus, we choose the state with
 * the highest state reference index as the 'new' representative in a union
 * procedure. Preliminary experiments showed no equivalent performance compared
 * to a structure that does use a rank field.
 *
 *
 * Wait-free Parallel Algorithms for the Union-find Problem
 *
 * @inproceedings{Anderson.91.STOC,
     author    = {Anderson, Richard J. and Woll, Heather},
     title     = {{Wait-free Parallel Algorithms for the Union-find Problem}},
     booktitle = {Proceedings of the Twenty-third Annual ACM Symposium on
                  Theory of Computing},
     series    = {STOC '91},
     year      = {1991},
     isbn      = {0-89791-397-3},
     location  = {New Orleans, Louisiana, USA},
     pages     = {370--380},
     numpages  = {11},
     url       = {http://doi.acm.org/10.1145/103418.103458},
     doi       = {10.1145/103418.103458},
     acmid     = {103458},
     publisher = {ACM},
     address   = {New York, NY, USA},
  }
 *
 * Disjoint Set Union with Randomized Linking
 *
 * @inproceedings{Goel.14.SODA,
     author    = {Goel, Ashish and Khanna, Sanjeev and Larkin, Daniel H. and
                  Tarjan, Robert E.},
     title     = {{Disjoint Set Union with Randomized Linking}},
     booktitle = {Proceedings of the Twenty-Fifth Annual ACM-SIAM Symposium on
                  Discrete Algorithms},
     series    = {SODA '14},
     year      = {2014},
     isbn      = {978-1-611973-38-9},
     location  = {Portland, Oregon},
     pages     = {1005--1017},
     numpages  = {13},
     url       = {http://dl.acm.org/citation.cfm?id=2634074.2634149},
     acmid     = {2634149},
     publisher = {SIAM},
  }
 *
 */

#ifndef RENAULT_UNIONFIND_H
#define RENAULT_UNIONFIND_H

#include <pins2lts-mc/algorithm/algorithm.h>

typedef struct r_uf_s        r_uf_t;
typedef struct r_uf_state_s  r_uf_state_t;

#define CLAIM_DEAD      1
#define CLAIM_FIRST     2
#define CLAIM_SUCCESS   3

extern r_uf_t   *r_uf_create ();

/* ********************* 'basic' union find operations ********************* */

extern char      r_uf_make_claim (const r_uf_t *uf, ref_t state);

extern ref_t     r_uf_find (const r_uf_t *uf, ref_t state);

extern bool      r_uf_sameset (const r_uf_t *uf, ref_t state_x, ref_t state_y);

extern bool      r_uf_union (const r_uf_t *uf, ref_t state_x, ref_t state_y);

/* ******************************* dead SCC ******************************** */

extern bool      r_uf_is_dead (const r_uf_t *uf, ref_t state);

extern bool      r_uf_mark_dead (const r_uf_t *uf, ref_t state);

/* **************************** TGBA acceptance **************************** */

extern uint32_t  r_uf_get_acc (const r_uf_t *uf, ref_t state);

extern uint32_t  r_uf_add_acc (const r_uf_t *uf, ref_t state, uint32_t acc);

/* ******************************** testing ******************************** */

extern ref_t     r_uf_debug (const r_uf_t *uf, ref_t state);

#endif // RENAULT_UNIONFIND_H
