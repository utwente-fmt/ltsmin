#include <hre/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/trace.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <pins2lts-mc/algorithm/ltl.h> // ecd_* TODO
#include <pins2lts-mc/algorithm/reach.h>
#include <util-lib/util.h>

// TODO: split

int              dlk_detect = 0;
char            *act_detect = NULL;
char            *inv_detect = NULL;
int              no_exit = 0;
size_t           max_level = SIZE_MAX;

struct poptOption reach_options[] = {
    {"deadlock", 'd', POPT_ARG_VAL, &dlk_detect, 1, "detect deadlocks", NULL },
    {"action", 'a', POPT_ARG_STRING, &act_detect, 0, "detect error action", NULL },
    {"invariant", 'i', POPT_ARG_STRING, &inv_detect, 0, "detect invariant violations", NULL },
    {"no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
    {"max", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &max_level, 0, "maximum search depth", "<int>"},
    POPT_TABLEEND
};

void
handle_error_trace (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t        *sm = ctx->global;
    size_t              level = loc->counters.level_cur;
    if (trc_output) {
        double uw = cct_finalize (global->tables, "BOGUS, you should not see this string.");
        Warning (infoLong, "Parallel chunk tables under-water mark: %.2f", uw);
        if (strategy[0] & Strat_TA) {
            if (W != 1 || strategy[0] != Strat_TA_DFS)
                Abort("Opaal error traces only supported with a single thread and DFS order");
            dfs_stack_leave (sm->stack);
            level = dfs_stack_nframes (sm->stack) + 1;
            find_and_write_dfs_stack_trace (ctx, level);
        } else {
            trc_env_t  *trace_env = trc_create (ctx->model, get_state, ctx);
            Warning (info, "Writing trace to %s", trc_output);
            trc_find_and_write (trace_env, trc_output, ctx->state.ref, level,
                                global->parent_ref, ctx->initial.ref);
        }
    }
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
}

size_t
sbfs_level (wctx_t *ctx, size_t local_size)
{
    alg_local_t        *loc = ctx->local;
    counter_t          *cnt = &loc->counters;
    size_t              next_level_size;
    HREreduce (HREglobal(), 1, &local_size, &next_level_size, SizeT, Sum);
    increase_level (&cnt->level_cur, &cnt->level_max);
    if (0 == ctx->id) {
        if (next_level_size > max_level_size)
            max_level_size = next_level_size;
        Warning(infoLong, "BFS level %zu has %zu states %zu total", loc->counters.level_cur, next_level_size, loc->counters.visited);
    }
    return next_level_size;
}

ssize_t
split_bfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    dfs_stack_t         source_stack = source->global->in_stack;
    size_t              in_size = dfs_stack_size (source_stack);
    if (in_size < 2) {
        in_size = dfs_stack_size (source->global->out_stack);
        source_stack = source->global->out_stack;
    }
    handoff = min (in_size >> 1 , handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_pop (source_stack);
        HREassert (NULL != one);
        dfs_stack_push (target->global->in_stack, one);
    }
    source->local->counters.splits++;
    source->local->counters.transfer += handoff;
    return handoff;
}

ssize_t
split_sbfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    size_t              in_size = dfs_stack_size (source->global->in_stack);
    handoff = min (in_size >> 1, handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_pop (source->global->in_stack);
        HREassert (NULL != one);
        dfs_stack_push (target->global->in_stack, one);
    }
    source->local->counters.splits++;
    source->local->counters.transfer += handoff;
    return handoff;
}

size_t
in_load (alg_global_t *sm)
{
    return dfs_stack_frame_size(sm->in_stack);
}

size_t
bfs_load (alg_global_t *sm)
{
    return dfs_stack_frame_size(sm->in_stack) + dfs_stack_frame_size(sm->out_stack);
}

ssize_t
split_dfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    size_t              in_size = dfs_stack_size (source->global->stack);
    handoff = min (in_size >> 1, handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_top (source->global->stack);
        if (!one) {
            dfs_stack_leave (source->global->stack);
            source->local->counters.level_cur--;
            one = dfs_stack_pop (source->global->stack);
            dfs_stack_push (target->global->stack, one);
            dfs_stack_enter (target->global->stack);
            target->local->counters.level_cur++;
        } else {
            dfs_stack_push (target->global->stack, one);
            dfs_stack_pop (source->global->stack);
        }
    }
    source->local->counters.splits++;
    source->local->counters.transfer += handoff;
    return handoff;
}

static void
reach_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    action_detect (ctx, ti, successor);
    ti->por_proviso = 1;
    if (!seen) {
        raw_data_t stack_loc = dfs_stack_push (sm->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        if (EXPECT_FALSE( trc_output &&
                          successor->ref != ctx->state.ref &&
                          global->parent_ref[successor->ref] == 0 &&
                          ti != &GB_NO_TRANSITION )) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        loc->counters.visited++;
    } else if (proviso == Proviso_Stack) {
        ti->por_proviso = !ecd_has_state (loc->cyan, successor);
    }
    loc->counters.trans++;
    (void) ti;
}

static inline void
explore_state (wctx_t *ctx)
{
    size_t              count;
    size_t              i = K;
    if (ctx->local->counters.level_cur >= max_level)
        return;
    count = permute_trans (ctx->permute, &ctx->state, reach_handle, ctx);
    deadlock_detect (ctx, count);
    counter_t          *cnt = &ctx->local->counters;
    maybe_report (cnt->explored, cnt->trans, cnt->level_max, "");
}

void
dfs_proviso (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    counter_t          *cnt = &loc->counters;
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(sm->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        // strict DFS (use extra bit because the permutor already adds successors to V)
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if (global_try_color(ctx->state.ref, GRED, 0)) {
                dfs_stack_enter (sm->stack);
                increase_level (&cnt->level_cur, &cnt->level_max);
                ecd_add_state (loc->cyan, &ctx->state, NULL);
                explore_state (ctx);
                loc->counters.explored++;
            } else {
                dfs_stack_pop (sm->stack);
            }
        } else {
            if (0 == dfs_stack_nframes (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            loc->counters.level_cur--;
            state_data = dfs_stack_pop (sm->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            ecd_remove_state (loc->cyan, &ctx->state);
        }
    }
    HREassert (lb_is_stopped(global->lb) || fset_count(loc->cyan) == 0);
}

void
dfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    counter_t          *cnt = &loc->counters;
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(sm->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            dfs_stack_enter (sm->stack);
            increase_level (&cnt->level_cur, &cnt->level_max);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            explore_state (ctx);
            loc->counters.explored++;
        } else {
            if (0 == dfs_stack_nframes (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            loc->counters.level_cur--;
            dfs_stack_pop (sm->stack);
        }
    }
}

void
bfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    counter_t          *cnt = &loc->counters;
    while (lb_balance(global->lb, ctx->id, bfs_load(sm), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            explore_state (ctx);
            loc->counters.explored++;
        } else {
            swap (sm->out_stack, sm->in_stack);
            sm->stack = sm->out_stack;
            increase_level (&cnt->level_cur, &cnt->level_max);
        }
    }
}

void
sbfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    size_t              next_level_size, local_next_size;
    do {
        while (lb_balance (global->lb, ctx->id, in_load(sm), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
            if (NULL != state_data) {
                state_info_deserialize (&ctx->state, state_data, ctx->store);
                explore_state (ctx);
                loc->counters.explored++;
            }
        }
        local_next_size = dfs_stack_frame_size (sm->out_stack);
        next_level_size = sbfs_level (ctx, local_next_size);
        lb_reinit (global->lb, ctx->id);
        swap (sm->out_stack, sm->in_stack);
        sm->stack = sm->out_stack;
    } while (next_level_size > 0 && !lb_is_stopped(global->lb));
}

void
pbfs_queue_state (wctx_t *ctx, state_info_t *successor)
{
    alg_local_t        *loc = ctx->local;
    hash64_t            h = ref_hash (successor->ref);
    alg_global_t       *remote = global->contexts[h % W]->global;
    size_t              local_next = (ctx->id << 1) + (1 - loc->flip);
    raw_data_t stack_loc = isba_push_int (remote->queues[local_next], NULL); // communicate
    state_info_serialize (successor, stack_loc);
    loc->counters.level_size++;
}

static void
pbfs_handle (void *arg, state_info_t *successor, transition_info_t *ti,
             int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    action_detect (ctx, ti, successor);
    if (!seen) {
        pbfs_queue_state (ctx, successor);
        if (EXPECT_FALSE( trc_output &&
                          successor->ref != ctx->state.ref &&
                          global->parent_ref[successor->ref] == 0) ) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        loc->counters.visited++;
    }
    if (EXPECT_FALSE(loc->lts != NULL)) {
        int             src = loc->counters.explored;
        int            *tgt = successor->data;
        int             tgt_owner = ref_hash (successor->ref) % W;
        lts_write_edge (loc->lts, ctx->id, &src, tgt_owner, tgt, ti->labels);
    }
    loc->counters.trans++;
    (void) ti;
}

void
pbfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    size_t              count;
    raw_data_t          state_data;
    int                 labels[SL];
    do {
        loc->counters.level_size = 0;     // count states in next level
        loc->flip = 1 - loc->flip; // switch in;out stacks
        for (size_t i = 0; i < W; i++) {
            size_t          current = (i << 1) + loc->flip;
            while ((state_data = isba_pop_int (sm->queues[current])) &&
                    !lb_is_stopped(global->lb)) {
                state_info_deserialize (&ctx->state, state_data, ctx->store);
                invariant_detect (ctx, ctx->state.data);
                count = permute_trans (ctx->permute, &ctx->state, pbfs_handle, loc);
                deadlock_detect (ctx, count);
                counter_t          *cnt = &loc->counters;
                maybe_report (cnt->explored, cnt->trans, cnt->level_max, "");
                if (EXPECT_FALSE(loc->lts && write_state)){
                    if (SL > 0)
                        GBgetStateLabelsAll (ctx->model, ctx->state.data, labels);
                    lts_write_state (loc->lts, ctx->id, ctx->state.data, labels);
                }
                loc->counters.explored++;
            }
        }
        count = sbfs_level (ctx, loc->counters.level_size);
    } while (count && !lb_is_stopped(global->lb));
}

static void
add_results (counter_t *res, counter_t *cnt)
{
    res->visited += cnt->visited;
    res->explored += cnt->explored;
    res->trans += cnt->trans;
    res->level_cur += cnt->level_cur;
    res->level_max += cnt->level_max;
    res->level_size += cnt->level_size;

    res->splits += cnt->splits;
    res->transfer += cnt->transfer;
    res->deadlocks += cnt->deadlocks;
    res->violations += cnt->violations;
    res->errors += cnt->errors;
}

void
reach_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (alg_reduced_t));
        run->reduced->runtime = 0;
        statistics_init (&run->reduced->state_stats);
        statistics_init (&run->reduced->trans_stats);
    }
    alg_reduced_t          *reduced = run->reduced;
    counter_t              *cnt = &ctx->local->counters;
    float                   runtime = RTrealTime(ctx->timer);

    statistics_record (&reduced->state_stats, cnt->explored);
    statistics_record (&reduced->trans_stats, cnt->trans);
    reduced->runtime += runtime;
    reduced->maxtime = max (runtime, reduced->maxtime);
    add_results (&reduced->counters, cnt);

    if (W >= 4 || !log_active(infoLong)) return;

    // print some local info
    Warning (info, "saw in %.3f sec %zu levels %zu states %zu transitions",
             runtime, cnt->level_max, cnt->explored, cnt->trans);

    if (Strat_ECD & strategy[1]) {
        fset_print_statistics (ctx->local->cyan, "ECD set");
    }
}

void
reach_print_stats   (run_t *run, wctx_t *ctx)
{
    alg_reduced_t          *reduced = run->reduced;
    size_t                  max_load =
            (Strat_SBFS & strategy[0] ? max_level_size : lb_max_load(global->lb));
    counter_t              *cnt = &reduced->counters;

    if (W > 1)
        Warning (info, "mean standard work distribution: %.1f%% (states) %.1f%% (transitions)",
                 (100 * statistics_stdev(&reduced->state_stats) /
                        statistics_mean(&reduced->state_stats)),
                 (100 * statistics_stdev(&reduced->trans_stats) /
                        statistics_mean(&reduced->trans_stats)));
    Warning (info, " ");
    cnt->level_max /= W;

    Warning (info, "State space has %zu states, %zu transitions",
             cnt->explored, cnt->trans);
    Warning (info, "Total exploration time %5.3f real", reduced->maxtime);
    //RTprintTimer (info, timer, "Total exploration time");
    Warning(info, "States per second: %.0f, Transitions per second: %.0f",
            cnt->explored/reduced->maxtime, cnt->trans/reduced->maxtime);
    Warning(info, " ");

    if (no_exit || log_active(infoLong))
        HREprintf (info, "\nDeadlocks: %zu\nInvariant/valid-end state violations: %zu\n"
                 "Error actions: %zu\n\n", cnt->deadlocks, cnt->violations,
                 cnt->errors);

/*
    HREprintf (infoLong, "\nInternal statistics:\n\n"
             "Algorithm:\nWork time: %.2f sec\nUser time: %.2f sec\nExplored: %zu\n"
                 "Transitions: %zu\nWaits: %zu\nRec. calls: %zu\n\n"
             "Database:\nElements: %zu\nNodes: %zu\nMisses: %zu\nEq. tests: %zu\nRehashes: %zu\n\n"
             "Memory:\nQueue: %.1f MB\nDB: %.1f MB\nDB alloc.: %.1f MB\nColors: %.1f MB\n\n"
             "Load balancer:\nSplits: %zu\nLoad transfer: %zu\n\n"
             "Lattice MAP:\nRatio: %.2f\nInserts: %zu\nUpdates: %zu\nDeletes: %zu\n"
             "Red subsumed: %zu\nCyan is subsumed: %zu\n",
             tot, reach->runtime, reach->explored, reach->trans, red->waits,
             reach->rec, db_elts, db_nodes, stats->misses, stats->tests,
             stats->rehashes, mem1, mem4, mem2, mem3,
             reach->splits, reach->transfer,
             ((double)lattices/db_elts), reach->inserts, reach->updates,
             reach->deletes, red->updates, red->deletes);
*/
}

void
reach_local_setup   (run_t *run, wctx_t *ctx)
{
    if ((strategy[0] & Strat_DFS) && Proviso_Stack) {
        ctx->local->cyan = fset_create (sizeof(ref_t), 0, 10, 20);
    }

    ctx->local->inv_expr = NULL;
    if (inv_detect) { // local parsing
        ctx->local->env = LTSminParseEnvCreate();
        ctx->local->inv_expr = parse_file_env (inv_detect, pred_parse_file,
                                               ctx->model, ctx->local->env);
    }

    if (PINS_POR) {
        if (ctx->local->inv_expr) {
            mark_visible (ctx->model, ctx->local->inv_expr, ctx->local->env);
        }
        if (act_detect) {
            pins_add_edge_label_visible (ctx->model, act_label, act_index);
        }
    }

    if (files[1]) {
        lts_type_t ltstype = GBgetLTStype (ctx->model);
        lts_file_t          template = lts_vset_template ();
        if (label_filter != NULL) {
            string_set_t label_set = SSMcreateSWPset(label_filter);
            Print1 (info, "label filter is \"%s\"", label_filter);
            ctx->local->lts = lts_file_create_filter (files[1], ltstype, label_set, W, template);
            write_state=1;
        } else {
            ctx->local->lts = lts_file_create (files[1], ltstype, W, template);
            if (SL > 0) write_state = 1;
        }
        int T = lts_type_get_type_count (ltstype);
        for (int i = 0; i < T; i++)
            lts_file_set_table (ctx->local->lts, i, GBgetChunkMap(ctx->model,i));
        HREbarrier (HREglobal()); // opening is sometimes a collaborative operation. (e.g. *.dir)
    } else {
        ctx->local->lts = NULL;
    }
    (void) run;
}

void
reach_local_init   (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof(alg_local_t));
    reach_local_setup (run, ctx);
}

void
reach_global_setup   (run_t *run, wctx_t *ctx)
{
    if (get_strategy(run->alg) & Strat_PBFS) {
        ctx->global->queues = RTmalloc (sizeof(isb_allocator_t[2][W]));
        for (size_t q = 0; q < 2; q++)
        for (size_t i = 0; i < W; i++)
            ctx->global->queues[(i << 1) + q] = isba_create (state_info_int_size());
    } else {
        ctx->global->out_stack = ctx->global->stack =
                dfs_stack_create (state_info_int_size());
        if (get_strategy(run->alg) & Strat_2Stacks) {
            ctx->global->in_stack = dfs_stack_create (state_info_int_size());
        }
    }
}

void
reach_global_init   (run_t *run, wctx_t *ctx)
{

    if (PINS_POR && (inv_detect || act_detect)) {
        if (W > 1)
            Abort ("Cycle proviso for safety properties with this parallel "
                    "search strategy is not yet implemented, use one thread or "
                    "sequential tool (*2lts-seq).");
        if (get_strategy(run->alg) != Strat_DFS)
            Abort ("Use DFS with ignoring proviso: --strategy=dfs --proviso=stack");
    }

    ctx->global = RTmallocZero (sizeof(alg_global_t));
    reach_global_setup (run, ctx);
}

void
reach_destroy   (run_t *run, wctx_t *ctx)
{
    alg_global_t        *global = ctx->global;

    if (get_strategy(run->alg) & Strat_PBFS) {
        for (size_t q = 0; q < 2; q++)
        for (size_t i = 0; i < W; i++)
            isba_destroy (global->queues[(i << 1) + q]);
        RTfree (global->queues);
    } else {
        dfs_stack_destroy (global->out_stack);
        if (get_strategy(run->alg) & (Strat_2Stacks)) {
            dfs_stack_destroy (global->in_stack);
        }
    }

    RTfree (global);
    (void) run;
}

void
reach_run (run_t *run, wctx_t *ctx)
{
    transition_info_t       ti = GB_NO_TRANSITION;
    if (0 == ctx->id) { // only w1 receives load, as it is propagated later
        if ( Strat_PBFS & strategy[0] ) {
            if (ctx->local->lts != NULL) {
                int             src_owner = ref_hash(ctx->initial.ref) % W;
                lts_write_init (ctx->local->lts, src_owner, ctx->initial.data);
            }
            pbfs_queue_state (ctx, &ctx->initial);
        } else {
            reach_handle (ctx, &ctx->initial, &ti, 0);
        }
    }
    ctx->local->counters.trans = 0; //reset trans count

    switch (get_strategy(run->alg)) {
    case Strat_SBFS:
        sbfs (ctx);
        break;
    case Strat_PBFS:
        pbfs (ctx);
        break;
    case Strat_BFS:
        bfs (ctx); break;
    case Strat_DFS:
        if (proviso == Proviso_Stack) {
            dfs_proviso (ctx);
        } else {
            dfs (ctx);
        }
        break;
    default: Abort ("Missing case in reach_run.");
    }
}

void
reach_destroy_local      (run_t *run, wctx_t *ctx)
{
    if (ctx->local->lts != NULL) {
        lts_file_close (ctx->local->lts);
    }
    RTfree (ctx->local);
}

void
reach_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, reach_local_init);
    set_alg_global_init (run->alg, reach_global_init);
    set_alg_destroy (run->alg, reach_destroy);
    set_alg_destroy_local (run->alg, reach_destroy_local);
    set_alg_print_stats (run->alg, reach_print_stats);
    set_alg_run (run->alg, reach_run);
    set_alg_reduce (run->alg, reach_reduce);
}
