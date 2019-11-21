/**
 * Next-state permutator
 *
 * Integrated state storage f the multi-core tool to increase efficiency
 */


#ifndef PERMUTE_H
#define PERMUTE_H

#include <stdlib.h>

#include <hre/runtime.h>
#include <mc-lib/trace.h>
#include <pins2lts-mc/parallel/state-info.h>


/* permute_get_transitions is a replacement for GBgetTransitionsLong
 */
#define                     TODO_MAX 10000

typedef enum {
    Perm_None,      /* normal group order */
    Perm_Shift,     /* shifted group order (lazy impl., thus cheap) */
    Perm_Shift_All, /* eq. to Perm_Shift, but non-lazy */
    Perm_Sort,      /* order on the state index in the DB */
    Perm_Random,    /* generate a random fixed permutation */
    Perm_RR,        /* more random */
    Perm_SR,        /* sort according to a random fixed permutation */
    Perm_Otf,       /* on-the-fly calculation of a random perm for num_succ */
    Perm_Dynamic,   /* generate a dynamic permutation based on color feedback */
    Perm_Unknown    /* not set yet */
} permutation_perm_t;

extern si_map_entry permutations[];

extern struct poptOption perm_options[];

typedef int             (*alg_state_seen_f) (void *ctx, transition_info_t *ti,
                                             ref_t ref, int seen);

typedef void            (*perm_cb_f)    (void *context, state_info_t *dst,
                                         transition_info_t *ti, int seen);

typedef struct permute_s permute_t;

extern permutation_perm_t permutation;

/**
 * Create a permuter.
 * arguments:
 * permutation: see permutation_perm_t
 */
extern permute_t       *permute_create (permutation_perm_t permutation,
                                        model_t model, alg_state_seen_f ssf,
                                        int worker_index, void *run_ctx);

extern void             permute_set_model (permute_t *perm, model_t model);

/**
 * Set to 0 to force ignoring proviso
 */
extern void             permute_set_por (permute_t *perm, int por);

extern void             permute_free (permute_t *perm);

extern int              permute_next (permute_t *perm, state_info_t *state,
                                      int group, perm_cb_f cb, void *ctx);

extern int              permute_trans (permute_t *perm, state_info_t *state,
                                       perm_cb_f cb, void *ctx);

extern state_info_t    *permute_state_info (permute_t *perm);

#endif // PERMUTE_H
