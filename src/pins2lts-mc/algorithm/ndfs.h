/**
 * Courcoubetis et al. NDFS, with extensions:
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

#ifndef NDFS_H
#define NDFS_H

#include <stdlib.h>

#include <hre/stringindex.h>
#include <pins2lts-mc/algorithm/ltl.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/state-store.h>

extern int              all_red;

// used for tracing // TODO
typedef union ta_cndfs_state_u {
    struct val_s {
        ref_t           ref;
        lattice_t       lattice;
    } val;
    char                data[16];
} ta_cndfs_state_t;

typedef struct counter_s {
    size_t              explored;
    size_t              trans;
    size_t              level_max;
    size_t              level_cur;

    size_t              waits;
    size_t              accepting;
    size_t              allred;         // counter: allred states
    size_t              bogus_red;      // number of bogus red colorings
    size_t              exit;           // recursive ndfss
} counter_t;

struct alg_local_s {
    dfs_stack_t         stack;          // Successor stack
    dfs_stack_t         in_stack;       //
    bitvector_t         color_map;      // Local NDFS coloring of states (ref-based)
    counter_t           counters;       // reachability/NDFS_blue counters
    counter_t           red;            // NDFS_red counters
    ref_t               seed;           // current NDFS seed
    bitvector_t         all_red;        // all_red gaiser/Schwoon
    size_t              rec_bits;
    strategy_t          strat;

    string_index_t      si;             // Trace index
    fset_t             *cyan;           // Cyan states for ta_cndfs or OWCTY_ECD
    fset_t             *pink;           // Pink states for ta_cndfs
    fset_t             *cyan2;          // Cyan states for ta_cndfs_sub
};

struct alg_reduced_s {
    counter_t           blue;
    counter_t           red;
};

extern void ndfs_explore_state_red (wctx_t *ctx);

extern void ndfs_explore_state_blue (wctx_t *ctx);

extern void ndfs_blue (run_t *alg, wctx_t *ctx);

extern void ndfs_local_init   (run_t *alg, wctx_t *ctx);

extern void ndfs_local_setup   (run_t *alg, wctx_t *ctx);

extern void ndfs_global_init   (run_t *alg, wctx_t *ctx);

extern void ndfs_print_stats   (run_t *alg, wctx_t *ctx);

extern void ndfs_report_cycle (wctx_t *ctx, state_info_t *cycle_closing_state);

extern int  ndfs_state_seen (void *ptr, ref_t ref, int seen);

extern void ndfs_print_state_stats (run_t* run, wctx_t* ctx, int index,
                                    float waittime);

extern void ndfs_reduce  (run_t *alg, wctx_t *ctx);

static inline void
wait_seed (wctx_t *ctx, ref_t seed)
{
    int didwait = 0;
    while (get_wip(seed) > 0 && !lb_is_stopped(global->lb)) { didwait = 1; } //wait
    if (didwait) {
        ctx->local->red.waits++;
    }
}

static inline void
set_all_red (wctx_t *ctx, state_info_t *state)
{
    if (global_try_color(state->ref, GRED, ctx->local->rec_bits)) {
        ctx->local->counters.allred++;
        if ( GBbuchiIsAccepting(ctx->model, state->data) )
            ctx->local->counters.accepting++; /* count accepting states */
    } else {
        ctx->local->red.allred++;
    }
}

static inline void
set_red (wctx_t *ctx, state_info_t *state)
{
    if (global_try_color(state->ref, GRED, ctx->local->rec_bits)) {
        ctx->local->red.explored++;
        if ( GBbuchiIsAccepting(ctx->model, get_state(state->ref, ctx)) )
            ctx->local->counters.accepting++; /* count accepting states */
    } else {
        ctx->local->red.bogus_red++;
    }
}

#endif // NDFS_H
