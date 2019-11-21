/**
 * Courcoubetis et al. NDFS, with extensions:
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

#ifndef NDFS_H
#define NDFS_H

#include <stdlib.h>

#include <hre/stringindex.h>
#include <pins-lib/pins-util.h>
#include <pins2lts-mc/algorithm/ltl.h>

extern int              all_red;

typedef enum ndfs_bits_e {
    ALLRED = 1,
    NOCYAN = 2,
} ndfs_bits_t;

typedef struct counter_s {
    size_t              waits;
    size_t              accepting;
    size_t              allred;         // counter: allred states
    size_t              bogus_red;      // number of bogus red colorings
    size_t              exit;           // recursive ndfss
    size_t              ignoring;
} counter_t;

struct alg_local_s {
    dfs_stack_t         stack;          // Successor stack
    bitvector_t         color_map;      // Local NDFS coloring of states (ref-based)
    counter_t           counters;       // reachability/NDFS_blue counters
    work_counter_t      red_work;
    counter_t           red;            // NDFS_red counters
    bitvector_t         stackbits;      // all_red gaiser/Schwoon + other stack bits
    size_t              rec_bits;
    strategy_t          strat;
    state_info_t       *seed;
    int                 bits;
    int                 seed_bits;
};

struct alg_reduced_s {
    counter_t           blue;
    counter_t           red;
    work_counter_t      red_work;
};

extern void ndfs_blue_handle (void *arg, state_info_t *successor,
                              transition_info_t *ti, int seen);

extern void ndfs_explore_state_red (wctx_t *ctx);

extern void ndfs_explore_state_blue (wctx_t *ctx);

extern void ndfs_blue (run_t *alg, wctx_t *ctx);

extern void ndfs_local_init   (run_t *alg, wctx_t *ctx);

extern void ndfs_local_setup   (run_t *alg, wctx_t *ctx);

extern void ndfs_local_deinit   (run_t *alg, wctx_t *ctx);

extern void ndfs_global_init   (run_t *alg, wctx_t *ctx);

extern void ndfs_global_deinit   (run_t *alg, wctx_t *ctx);

extern void ndfs_print_stats   (run_t *alg, wctx_t *ctx);

extern int  ndfs_state_seen (void *ptr, transition_info_t *ti,
                             ref_t ref, int seen);

extern void ndfs_print_state_stats (run_t* run, wctx_t* ctx, int index,
                                    float waittime);

extern void ndfs_reduce  (run_t *alg, wctx_t *ctx);

static inline void
wait_seed (wctx_t *ctx, ref_t seed)
{
    int didwait = 0;
    while (state_store_get_wip(seed) > 0 && !run_is_stopped(ctx->run)) {
        didwait = 1; // spin wait
    }
    if (didwait) {
        ctx->local->red.waits++;
    }
}

static inline void
set_all_red (wctx_t *ctx, state_info_t *state)
{
    if (state_store_try_color(state->ref, GRED, ctx->local->rec_bits)) {
        ctx->local->counters.allred++;
        if ( pins_state_is_accepting(ctx->model, state_info_state(state)) )
            ctx->local->counters.accepting++; /* count accepting states */
    } else {
        ctx->local->red.allred++;
    }
}

static inline void
set_red (wctx_t *ctx, state_info_t *state)
{
    if (state_store_try_color(state->ref, GRED, ctx->local->rec_bits)) {
        ctx->local->red_work.explored++;
        if ( pins_state_is_accepting(ctx->model, state_info_state(state)) )
            ctx->local->counters.accepting++; /* count accepting states */
    } else {
        ctx->local->red.bogus_red++;
    }
}

#endif // NDFS_H
