/**
 *
 */

#include <hre/config.h>

#include <popt.h>

#include <pins2lts-mc/algorithm/ndfs.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

static const size_t     MAX_STACK = 100000000;  // length of allred bitset

int              all_red = 1;

struct poptOption ndfs_options[] = {
    {"nar", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &all_red, 0,
     "turn off red coloring in the blue search (NNDFS/MCNDFS)", NULL},
    POPT_TABLEEND
};

static void
add_results (counter_t *res, counter_t *cnt)
{
    res->accepting += cnt->accepting;
    res->allred += cnt->allred;
    res->waits += cnt->waits;
    res->bogus_red += cnt->bogus_red;
    res->exit += cnt->exit;
    res->ignoring += cnt->ignoring;
}

static void
ndfs_red_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    nndfs_color_t color = nn_get_color(&loc->color_map, successor->ref);
    ti->por_proviso = 1; // only visit blue states to stay in reduced search space

    if (proviso != Proviso_None && !nn_color_eq(color, NNBLUE))
        return; // only revisit blue states to determinize POR
    if ( nn_color_eq(color, NNCYAN) ) {
        /* Found cycle back to the stack */
        ndfs_report_cycle (ctx->run, ctx->model, loc->stack, successor);
    } else if ( nn_color_eq(color, NNBLUE) && (loc->strat != Strat_LNDFS ||
            !state_store_has_color(ctx->state->ref, GRED, loc->rec_bits)) ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

void
ndfs_blue_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    nndfs_color_t color = nn_get_color (&loc->color_map, successor->ref);
    if (proviso == Proviso_Stack)
        ti->por_proviso = !nn_color_eq(color, NNCYAN);
    /**
     * The following lines bear little resemblance to the algorithms in the
     * respective papers (NNDFS / LNDFS), but we must store all non-red states
     * on the stack here, in order to calculate all-red correctly later.
     */
    if ( ecd && nn_color_eq(color, NNCYAN) &&
            (pins_state_is_accepting(ctx->model, state_info_state(ctx->state)) ||
             pins_state_is_accepting(ctx->model, state_info_state(successor))) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle (ctx->run, ctx->model, loc->stack, successor);
    } else if ((loc->strat == Strat_LNDFS && !state_store_has_color(ctx->state->ref, GRED, loc->rec_bits)) ||
               (loc->strat != Strat_LNDFS && !nn_color_eq(color, NNPINK))) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

void
ndfs_explore_state_red (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    work_counter_t     *cnt = &loc->red_work;
    dfs_stack_enter (loc->stack);
    increase_level (cnt);
    cnt->trans += permute_trans (ctx->permute, ctx->state, ndfs_red_handle, ctx);
    run_maybe_report (ctx->run, cnt, "[Red ] ");
}

void
ndfs_explore_state_blue (wctx_t *ctx)
{
    work_counter_t     *cnt = ctx->counters;
    dfs_stack_enter (ctx->local->stack);
    increase_level (cnt);
    cnt->trans += permute_trans (ctx->permute, ctx->state, ndfs_blue_handle, ctx);
    cnt->explored++;
    run_maybe_report (ctx->run, cnt, "[Blue] ");
}

/* NNDFS dfs_red */
void
ndfs_red (wctx_t *ctx, ref_t seed)
{
    alg_local_t        *loc = ctx->local;
    loc->counters.accepting++; //count accepting states
    ndfs_explore_state_red (ctx);
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state->ref);
            if ( nn_color_eq(color, NNBLUE) ) {
                nn_set_color (&loc->color_map, ctx->state->ref, NNPINK);
                ndfs_explore_state_red (ctx);
                loc->red_work.explored++;
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
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed == ctx->state->ref)
                break;
            dfs_stack_pop (loc->stack);
        }
    }
}

/* NDFS dfs_blue */
void
ndfs_blue (run_t *run, wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    transition_info_t       ti = GB_NO_TRANSITION;
    ndfs_blue_handle (ctx, ctx->initial, &ti, 0);
    ctx->counters->trans = 0; //reset trans count

    while ( !run_is_stopped(run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state->ref);
            if ( nn_color_eq(color, NNWHITE) ) {
                if (all_red)
                    bitvector_set ( &loc->stackbits, ctx->counters->level_cur );
                nn_set_color (&loc->color_map, ctx->state->ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( all_red && ctx->counters->level_cur != 0 &&
                                !nn_color_eq(color, NNPINK) )
                    bitvector_unset ( &loc->stackbits, ctx->counters->level_cur - 1);
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
                /* exit if backtrack hits seed, leave stack the way it was */
                nn_set_color (&loc->color_map, loc->seed->ref, NNPINK);
                loc->counters.allred++;
                if ( pins_state_is_accepting(ctx->model, state_info_state(loc->seed)) )
                    loc->counters.accepting++;
            } else if ( pins_state_is_accepting(ctx->model, state_info_state(loc->seed)) ) {
                /* call red DFS for accepting states */
                ndfs_red (ctx, loc->seed->ref);
                nn_set_color (&loc->color_map, loc->seed->ref, NNPINK);
            } else {
                if (all_red && ctx->counters->level_cur > 0)
                    bitvector_unset (&loc->stackbits, ctx->counters->level_cur - 1);
                nn_set_color (&loc->color_map, loc->seed->ref, NNBLUE);
            }
            dfs_stack_pop (loc->stack);
        }
    }
}

void
ndfs_local_init   (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof(alg_local_t));

    ndfs_local_setup (run, ctx);

    size_t              local_bits = 2;
    alg_local_t        *loc = ctx->local;
    bitvector_create (&loc->color_map, local_bits<<dbs_size);
}

void
ndfs_local_setup   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    if (all_red) {
        bitvector_create (&loc->stackbits, MAX_STACK);
    }
    loc->rec_bits = 0;
    loc->strat = get_strategy (run->alg);
    loc->seed = state_info_create ();
    size_t              len = state_info_serialize_int_size (ctx->state);

    //state_info_add_simple (ctx->state, sizeof(int), &loc->bits);
    //state_info_add_simple (ctx->local->seed, sizeof(int), &loc->seed_bits);

    loc->stack = dfs_stack_create (len);
}

void
ndfs_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    if (all_red)
        bitvector_free (&loc->stackbits);
    bitvector_free (&loc->color_map);
    dfs_stack_destroy (loc->stack);
    RTfree (loc);
    (void) run;
}

int
ndfs_state_seen (void *ptr, transition_info_t *ti, ref_t ref, int seen)
{
    wctx_t             *ctx = (wctx_t *) ptr;
    return !nn_color_eq(nn_get_color(&ctx->local->color_map, ref), NNWHITE);
    (void) seen; (void) ti;
}

void
ndfs_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ndfs_global_deinit   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ndfs_print_state_stats (run_t* run, wctx_t* ctx, int index, float waittime)
{
    size_t db_elts = global->stats.elts;
    size_t explored = run->total.explored;
    //size_t trans = run->total.trans;
    size_t rtrans = run->reduced->red_work.trans;
    size_t rexplored = run->reduced->red_work.explored;
    size_t bogus = run->reduced->red.bogus_red;
    size_t waits = run->reduced->red.waits;
    size_t allred = run->reduced->blue.allred;
    size_t rallred = run->reduced->red.allred;

    Warning (info, "%s_%d (permutation: %s) stats:",
            key_search(strategies, ctx->local->strat & ~Strat_TA), index,
            key_search(permutations, permutation));
    Warning (info, "blue states: %zu (%.2f%%), transitions: %zu (per worker)",
            explored, ((double)explored/db_elts)*100, rtrans);
    Warning (info, "red states: %zu (%.2f%%), bogus: %zu  (%.2f%%), "
                   "transitions: %zu, waits: %zu (%.2f sec)",
            rexplored, ((double)rexplored/db_elts)*100, bogus,
            ((double)bogus/db_elts), rtrans, waits, waittime);
    if (PINS_POR != 0 && proviso == Proviso_CNDFS) {
        Warning (infoLong, "Ignoring states: %zu", run->reduced->blue.ignoring);
    }

    if ( all_red )
        Warning (info, "all-red states: %zu (%.2f%%), bogus %zu (%.2f%%)",
                 allred, ((double)allred/db_elts)*100,
                 rallred, ((double)rallred/db_elts)*100);
}

void
ndfs_print_stats   (run_t *run, wctx_t *ctx)
{
    size_t              db_elts = global->stats.elts;
    size_t              accepting = run->reduced->blue.accepting / W;

    run_report_total (run);

    Warning (info, " ");
    Warning (info, "State space has %zu states, %zu are accepting", db_elts, accepting);

    run->total.explored /= W;
    run->total.trans /= W;
    run->reduced->red_work.trans /= W;
    run->reduced->red_work.explored /= W;
    run->reduced->red.bogus_red /= W;
    run->reduced->blue.allred /= W;
    run->reduced->red.allred /= W;
    ndfs_print_state_stats (run, ctx, 0, 0);
}

void
ndfs_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (alg_reduced_t));
    }
    alg_reduced_t          *reduced = run->reduced;
    counter_t              *blue = &ctx->local->counters;
    counter_t              *red = &ctx->local->red;
    work_counter_t         *blue_work = ctx->counters;
    work_counter_t         *red_work = &ctx->local->red_work;

    add_results (&reduced->blue, blue);
    add_results (&reduced->red, red);
    work_add_results (&reduced->red_work, red_work);

    // publish local memory statistics for run class
    run->total.local_states += blue_work->level_max + red_work->level_max;

    if (W >= 4 || !log_active(infoLong)) return;

    // print some local info
    float                   runtime = RTrealTime(ctx->timer);
    Warning (info, "Nested depth-first search worker ran %.3f sec", runtime);
    work_report ("[Blue]", blue_work);
    work_report ("[Red ]", red_work);
}

void
ndfs_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, ndfs_local_init);
    set_alg_global_init (run->alg, ndfs_global_init);
    set_alg_global_deinit (run->alg, ndfs_global_deinit);
    set_alg_local_deinit (run->alg, ndfs_local_deinit);
    set_alg_print_stats (run->alg, ndfs_print_stats);
    set_alg_run (run->alg, ndfs_blue);
    set_alg_state_seen (run->alg, ndfs_state_seen);
    set_alg_reduce (run->alg, ndfs_reduce);

    init_threshold = THRESHOLD; // Non-distributed counting (searches completely overlap)
}
