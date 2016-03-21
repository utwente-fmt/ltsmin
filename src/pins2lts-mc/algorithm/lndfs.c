/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/lndfs.h>

/* LNDFS dfs_red */
static void
lndfs_red (wctx_t *ctx, ref_t seed)
{
    alg_local_t        *loc = ctx->local;
    state_store_inc_wip (seed);
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state->ref);
            if ( !nn_color_eq(color, NNPINK) &&
                 !state_store_has_color(ctx->state->ref, GRED, loc->rec_bits) ) {
                nn_set_color (&loc->color_map, ctx->state->ref, NNPINK);
                ndfs_explore_state_red (ctx);
            } else {
                if (seed == ctx->state->ref)
                    break;
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            dfs_stack_leave (loc->stack);
            loc->red_work.level_cur--;
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (ctx->state, state_data);
            if (seed == ctx->state->ref) {
                /* exit if backtrack hits seed, leave stack the way it was */
                state_store_dec_wip (seed);
                wait_seed (ctx, seed);
                if ( state_store_try_color(ctx->state->ref, GRED, loc->rec_bits) )
                    loc->counters.accepting++; //count accepting states
                return;
            }
            set_red (ctx, ctx->state);
            dfs_stack_pop (loc->stack);
        }
    }
    //halted by the load balancer
    state_store_dec_wip (seed);
}

/* LNDFS dfs_blue */
void
lndfs_blue (run_t *run, wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    transition_info_t       ti = GB_NO_TRANSITION;
    ndfs_blue_handle (ctx, ctx->initial, &ti, 0);
    ctx->counters->trans = 0; //reset trans count

    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state->ref);
            if ( nn_color_eq(color, NNWHITE) &&
                 !state_store_has_color(ctx->state->ref, GRED, loc->rec_bits) ) {
                if (all_red)
                    bitvector_set (&loc->stackbits, ctx->counters->level_cur);
                nn_set_color (&loc->color_map, ctx->state->ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( all_red && ctx->counters->level_cur != 0 &&
                     !state_store_has_color(ctx->state->ref, GRED, loc->rec_bits) )
                    bitvector_unset (&loc->stackbits, ctx->counters->level_cur - 1);
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (loc->stack))
                return;
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (loc->seed, state_data);
            if ( all_red && bitvector_is_set(&loc->stackbits, ctx->counters->level_cur) ) {
                /* all successors are red */
                wait_seed (ctx, loc->seed->ref);
                set_all_red (ctx, loc->seed);
            } else if ( pins_state_is_accepting(ctx->model, state_info_state(loc->seed)) ) {
                /* call red DFS for accepting states */
                lndfs_red (ctx, loc->seed->ref);
            } else if (all_red && ctx->counters->level_cur > 0 &&
                       !state_store_has_color(loc->seed->ref, GRED, loc->rec_bits)) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&loc->stackbits, ctx->counters->level_cur - 1);
            }
            nn_set_color (&loc->color_map, loc->seed->ref, NNBLUE);
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
lndfs_local_deinit   (run_t *run, wctx_t *ctx)
{
    ndfs_local_deinit (run, ctx);
}

void
lndfs_global_init   (run_t *run, wctx_t *ctx)
{
    ndfs_global_init (run, ctx);
}

void
lndfs_global_deinit   (run_t *run, wctx_t *ctx)
{
    ndfs_global_deinit (run, ctx);
}

void
lndfs_print_stats   (run_t *run, wctx_t *ctx)
{
    ndfs_print_stats (run, ctx);

    Warning (infoLong, " ");
    Warning (infoLong, "LNDFS waits:");
    Warning (infoLong, "Waits: %zu",  run->reduced->blue.waits);
}

void
lndfs_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, lndfs_local_init);
    set_alg_local_deinit (run->alg, lndfs_local_deinit);
    set_alg_global_init (run->alg, lndfs_global_init);
    set_alg_global_deinit (run->alg, lndfs_global_deinit);
    set_alg_print_stats (run->alg, lndfs_print_stats);
    set_alg_run (run->alg, lndfs_blue);
    set_alg_state_seen (run->alg, ndfs_state_seen);
    set_alg_reduce (run->alg, ndfs_reduce);
}
