/**
 *
 */

#include <hre/config.h>

#include <popt.h>

#include <hre/stringindex.h>
#include <ltsmin-lib/ltsmin-standard.h>
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
    {"no-ecd", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &ecd, 0,
     "turn off early cycle detection (NNDFS/MCNDFS)", NULL},
    POPT_TABLEEND
};

static void
add_results (counter_t *res, counter_t *cnt)
{
    res->explored += cnt->explored;
    res->trans += cnt->trans;
    res->level_cur += cnt->level_cur;
    res->level_max += cnt->level_max;

    res->accepting += cnt->accepting;
    res->allred += cnt->allred;
    res->waits += cnt->waits;
    res->bogus_red += cnt->bogus_red;
    res->rec += cnt->rec;
    res->exit += cnt->exit;
}

static void *
get_stack_state (ref_t ref, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    ta_cndfs_state_t   *state = (ta_cndfs_state_t *) SIget(loc->si, ref);
    state_data_t        data  = get_state (state->val.ref, ctx);
    Debug ("Trace %zu (%zu,%zu)", ref, state->val.ref, state->val.lattice);
    if (strategy[0] & Strat_TA) {
        memcpy (ctx->store, data, D<<2);
        ((lattice_t*)(ctx->store + D))[0] = state->val.lattice;
        data = ctx->store;
    }
    return data;
}

static inline void
new_state (ta_cndfs_state_t *out, state_info_t *si)
{
    out->val.ref = si->ref;
    out->val.lattice = si->lattice;
}

void
find_and_write_dfs_stack_trace (wctx_t *ctx, int level)
{
    alg_local_t        *loc = ctx->local;
    ref_t          *trace = RTmalloc (sizeof(ref_t) * level);
    loc->si = SIcreate();
    for (int i = level - 1; i >= 0; i--) {
        state_data_t data = dfs_stack_peek_top (loc->stack, i);
        state_info_deserialize_cheap (&ctx->state, data);
        ta_cndfs_state_t state;
        new_state(&state, &ctx->state);
        if (!(strategy[0] & Strat_TA)) state.val.lattice = 0;
        int val = SIputC (loc->si, state.data, sizeof(struct val_s));
        trace[level - i - 1] = (ref_t) val;
    }
    trc_env_t          *trace_env = trc_create (ctx->model, get_stack_state, ctx);
    Warning (info, "Writing trace to %s", trc_output);
    trc_write_trace (trace_env, trc_output, trace, level);
    SIdestroy (&loc->si);
    RTfree (trace);
}

void
ndfs_report_cycle (wctx_t *ctx, state_info_t *cycle_closing_state)
{
    alg_local_t        *loc = ctx->local;
    /* Stop other workers, exit if some other worker was first here */
    if ( !lb_stop(global->lb) )
        return;
    size_t              level = dfs_stack_nframes (loc->stack) + 1;
    Warning (info, " ");
    Warning (info, "Accepting cycle FOUND at depth %zu!", level);
    Warning (info, " ");
    if (trc_output) {
        double uw = cct_finalize (global->tables, "BOGUS, you should not see this string.");
        Warning (infoLong, "Parallel chunk tables under-water mark: %.2f", uw);
        /* Write last state to stack to close cycle */
        state_data_t data = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (cycle_closing_state, data);
        find_and_write_dfs_stack_trace (ctx, level);
    }
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
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
        ndfs_report_cycle(ctx, successor);
    } else if ( nn_color_eq(color, NNBLUE) && (loc->strat != Strat_LNDFS ||
            !global_has_color(ctx->state.ref, GRED, loc->rec_bits)) ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static void
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
            (GBbuchiIsAccepting(ctx->model, ctx->state.data) ||
             GBbuchiIsAccepting(ctx->model, get_state(successor->ref, ctx))) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, successor);
    } else if ((loc->strat == Strat_LNDFS && !global_has_color(ctx->state.ref, GRED, loc->rec_bits)) ||
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
    counter_t          *cnt = &loc->red;
    dfs_stack_enter (loc->stack);
    increase_level (&cnt->level_cur, &cnt->level_max);
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_red_handle, ctx);
    maybe_report (cnt->explored, cnt->trans, cnt->level_max, "[R] ");
}

void
ndfs_explore_state_blue (wctx_t *ctx)
{
    counter_t *cnt = &ctx->local->counters;
    dfs_stack_enter (ctx->local->stack);
    increase_level (&cnt->level_cur, &cnt->level_max);
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_blue_handle, ctx);
    cnt->explored++;
    maybe_report (cnt->explored, cnt->trans, cnt->level_max, "[B] ");
}

/* NNDFS dfs_red */
void
ndfs_red (wctx_t *ctx, ref_t seed)
{
    alg_local_t        *loc = ctx->local;
    loc->counters.accepting++; //count accepting states
    ndfs_explore_state_red (ctx);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNBLUE) ) {
                nn_set_color (&loc->color_map, ctx->state.ref, NNPINK);
                ndfs_explore_state_red (ctx);
                loc->red.explored++;
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
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed == ctx->state.ref)
                break;
            dfs_stack_pop (loc->stack);
        }
    }
}

/* NDFS dfs_blue */
void
ndfs_blue (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    transition_info_t       ti = GB_NO_TRANSITION;
    ndfs_blue_handle (ctx, &ctx->initial, &ti, 0);
    ctx->local->counters.trans = 0; //reset trans count


    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&loc->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) ) {
                bitvector_set ( &loc->all_red, loc->counters.level_cur );
                nn_set_color (&loc->color_map, ctx->state.ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( loc->counters.level_cur != 0 && !nn_color_eq(color, NNPINK) )
                    bitvector_unset ( &loc->all_red, loc->counters.level_cur - 1);
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
                /* exit if backtrack hits seed, leave stack the way it was */
                nn_set_color (&loc->color_map, seed.ref, NNPINK);
                loc->counters.allred++;
                if ( GBbuchiIsAccepting(ctx->model, seed.data) )
                    loc->counters.accepting++;
            } else if ( GBbuchiIsAccepting(ctx->model, seed.data) ) {
                /* call red DFS for accepting states */
                ndfs_red (ctx, seed.ref);
                nn_set_color (&loc->color_map, seed.ref, NNPINK);
            } else {
                if (loc->counters.level_cur > 0)
                    bitvector_unset (&loc->all_red, loc->counters.level_cur - 1);
                nn_set_color (&loc->color_map, seed.ref, NNBLUE);
            }
            dfs_stack_pop (loc->stack);
        }
    }
    (void) run;
}

void
ndfs_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (alg_reduced_t));
        run->reduced->runtime = 0;
    }
    alg_reduced_t          *reduced = run->reduced;
    counter_t              *cnt = &ctx->local->counters;
    counter_t              *red = &ctx->local->red;
    float                   runtime = RTrealTime(ctx->timer);

    reduced->runtime += runtime;
    reduced->maxtime = max (runtime, reduced->maxtime);
    add_results (&reduced->blue, cnt);
    add_results (&reduced->red, red);

    if (W >= 4 || !log_active(infoLong)) return;

    // print some local info
    Warning (info, "[Blue] saw in %.3f sec %zu levels %zu states %zu transitions",
             runtime, cnt->level_max, cnt->explored, cnt->trans);

    Warning (info, "[Red ] saw in %.3f sec %zu levels %zu states %zu transitions",
             runtime, red->level_max, red->explored, red->trans);

    if (Strat_TA & strategy[0]) {
        fset_print_statistics (ctx->local->cyan, "Cyan set");
        fset_print_statistics (ctx->local->pink, "Pink set");
    }
}

void
ndfs_local_init   (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof(alg_local_t));
    ndfs_local_setup (run, ctx);
}

void
ndfs_local_setup   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    size_t local_bits = 2;
    int res = bitvector_create (&loc->color_map, local_bits<<dbs_size);
    HREassert (res != -1, "Failure to allocate a color_map bitvector.");
    if (all_red)
        res = bitvector_create (&loc->all_red, MAX_STACK);
    HREassert (res != -1, "Failure to allocate a all_red bitvector.");
    loc->rec_bits = 0;
    loc->strat = get_strategy (run->alg);
    loc->stack = dfs_stack_create (state_info_int_size());
}

int
ndfs_state_seen (void *ptr, ref_t ref, int seen)
{
    wctx_t             *ctx = (wctx_t *) ptr;
    return nn_color_eq(nn_get_color(&ctx->local->color_map, ref), NNWHITE);
    (void) seen;
}

void
ndfs_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ndfs_destroy   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    if (all_red)
        bitvector_free (&loc->all_red);
    bitvector_free (&loc->color_map);
    dfs_stack_destroy (loc->stack);
    RTfree (loc);
}

void
ndfs_print_state_stats (run_t* run, wctx_t* ctx, int index, float waittime)
{
    size_t db_elts = global->stats.elts;
    size_t explored = run->reduced->blue.explored;
    size_t trans = run->reduced->blue.trans;
    size_t rtrans = run->reduced->red.trans;
    size_t rexplored = run->reduced->red.explored;
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

    //RTprintTimer (info, ctx->timer, "Total exploration time ");
    Warning (info, "Total exploration time %5.3f real", run->reduced->maxtime);

    Warning (info, " ");
    Warning (info, "State space has %zu states, %zu are accepting", db_elts, accepting);

    run->reduced->blue.explored /= W;
    run->reduced->blue.trans /= W;
    run->reduced->red.trans /= W;
    run->reduced->red.explored /= W;
    run->reduced->red.bogus_red /= W;
    run->reduced->blue.allred /= W;
    run->reduced->red.allred /= W;
    ndfs_print_state_stats (run, ctx, 0, 0);

    size_t mem3 = ((double)(((((size_t)local_bits)<<dbs_size))/8*W)) / (1UL<<20);
    Warning (info, " ");
    Warning (info, "Total memory used for local state coloring: %.1fMB", mem3);
}

void
ndfs_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, ndfs_local_init);
    set_alg_global_init (run->alg, ndfs_global_init);
    set_alg_destroy (run->alg, ndfs_destroy);
    set_alg_print_stats (run->alg, ndfs_print_stats);
    set_alg_run (run->alg, ndfs_blue);
    set_alg_state_seen (run->alg, ndfs_state_seen);
    set_alg_reduce (run->alg, ndfs_reduce);
}
