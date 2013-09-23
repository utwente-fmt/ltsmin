/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/ndfs.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>

/* LNDFS dfs_red */
static void
lndfs_red (wctx_t *ctx, ref_t seed)
{
    alg_local_t        *loc = ctx->local;
    inc_wip (seed);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state.ref);
            if ( !nn_color_eq(color, NNPINK) &&
                 !global_has_color(ctx->state.ref, GRED, loc->rec_bits) ) {
                nn_set_color (&loc->color_map, ctx->state.ref, NNPINK);
                ndfs_explore_state_red (ctx);
            } else {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            dfs_stack_leave (loc->stack);
            loc->red.level_cur--;
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            if (seed == ctx->state.ref) {
                /* exit if backtrack hits seed, leave stack the way it was */
                dec_wip (seed);
                wait_seed (ctx, seed);
                if ( global_try_color(ctx->state.ref, GRED, loc->rec_bits) )
                    loc->counters.accepting++; //count accepting states
                return;
            }
            set_red (ctx, &ctx->state);
            dfs_stack_pop (loc->stack);
        }
    }
    //halted by the load balancer
    dec_wip (seed);
}

/* LNDFS dfs_blue */
void
lndfs_blue (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) &&
                 !global_has_color(ctx->state.ref, GRED, loc->rec_bits) ) {
                bitvector_set (&loc->all_red, loc->counters.level_cur);
                nn_set_color (&loc->color_map, ctx->state.ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( loc->counters.level_cur != 0 && !global_has_color(ctx->state.ref, GRED, loc->rec_bits) )
                    bitvector_unset (&loc->all_red, loc->counters.level_cur - 1);
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (loc->stack))
                return;
            dfs_stack_leave (loc->stack);
            loc->counters.level_cur--;
            state_data = dfs_stack_top (loc->stack);
            state_info_t            seed;
            state_info_deserialize (&seed, state_data, ctx->store);
            if ( all_red && bitvector_is_set(&loc->all_red, loc->counters.level_cur) ) {
                /* all successors are red */
                wait_seed (ctx, seed.ref);
                set_all_red (ctx, &seed);
            } else if ( GBbuchiIsAccepting(ctx->model, seed.data) ) {
                /* call red DFS for accepting states */
                lndfs_red (ctx, seed.ref);
            } else if (loc->counters.level_cur > 0 &&
                       !global_has_color(seed.ref, GRED, loc->rec_bits)) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&loc->all_red, loc->counters.level_cur - 1);
            }
            nn_set_color (&loc->color_map, seed.ref, NNBLUE);
            dfs_stack_pop (loc->stack);
        }
    }
    (void) run;
}

void
lndfs_local_init   (run_t *run, wctx_t *ctx)
{
    ndfs_local_init (run, ctx);
}

void
lndfs_global_init   (run_t *run, wctx_t *ctx)
{
    ndfs_global_init (run, ctx);
}

void
lndfs_destroy   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
lndfs_print_stats   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
lndfs_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, lndfs_local_init);
    set_alg_global_init (run->alg, lndfs_global_init);
    set_alg_destroy (run->alg, lndfs_destroy);
    set_alg_print_stats (run->alg, lndfs_print_stats);
    set_alg_run (run->alg, lndfs_blue);
    set_alg_state_seen (run->alg, ndfs_state_seen);
    set_alg_reduce (run->alg, ndfs_reduce);
}
