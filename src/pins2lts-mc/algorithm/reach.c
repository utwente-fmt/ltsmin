#include <hre/config.h>

#include <pins-lib/pins-util.h>

#include <pins2lts-mc/algorithm/ltl.h> // ecd_* && dfs_stack_trace
#include <pins2lts-mc/algorithm/reach.h>
#include <mc-lib/lmap.h>

// TODO: split into separate files

int              dlk_detect = 0;
char            *act_detect = NULL;
char            *inv_detect = NULL;
size_t           max_level = SIZE_MAX;

struct poptOption reach_options[] = {
    {"deadlock", 'd', POPT_ARG_VAL, &dlk_detect, 1, "detect deadlocks", NULL },
    {"action", 'a', POPT_ARG_STRING, &act_detect, 0,"detect error action", NULL },
    {"invariant", 'i', POPT_ARG_STRING, &inv_detect, 0,
     "detect invariant violations", NULL },
    {"no-exit", 'n', POPT_ARG_VAL, &no_exit, 1,
     "no exit on error, just count (for error counters use -v)", NULL },
    {"max", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &max_level, 0,
     "maximum search depth", "<int>"},
    {"gran", 'g', POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARGFLAG_DOC_HIDDEN, &G, 0,
     "subproblem granularity ( T( work(P,g) )=min( T(P), g ) )", NULL},
    {"handoff", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARGFLAG_DOC_HIDDEN, &H, 0,
     "maximum balancing handoff (handoff=min(max, stack_size/2))", NULL},
    POPT_TABLEEND
};

void *
get_state (ref_t state_no, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_set (ctx->state, state_no, LM_NULL_LATTICE);
    return state_info_pins_state (ctx->state);
}

void
handle_error_trace (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    alg_shared_t       *shared = ctx->run->shared;
    size_t              level = ctx->counters->level_cur;

    if (trc_output) {
        if (strategy[0] & Strat_TA) {
            dfs_stack_leave (sm->stack);
            find_and_write_dfs_stack_trace (ctx->model, sm->stack, false);
        } else {
            trc_env_t  *trace_env = trc_create (ctx->model, get_state, ctx);
            Warning (info, "Writing trace to %s", trc_output);
            trc_find_and_write (trace_env, trc_output, ctx->state->ref, level,
                                shared->parent_ref, ctx->initial->ref);
        }
    }
}

size_t
sbfs_level (wctx_t *ctx, size_t local_size)
{
    work_counter_t     *cnt = ctx->counters;
    size_t              next_level_size;
    HREreduce (HREglobal(), 1, &local_size, &next_level_size, SizeT, Sum);
    increase_level (cnt);
    if (0 == ctx->id) {
        if (next_level_size > ctx->run->shared->max_level_size) {
            ctx->run->shared->max_level_size = next_level_size;
        }
        ctx->run->shared->total_explored += next_level_size;
        Warning(infoLong, "BFS level %zu has %zu states %zu total",
                cnt->level_cur, next_level_size,
                ctx->run->shared->total_explored);
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
            source->counters->level_cur--;
            one = dfs_stack_pop (source->global->stack);
            dfs_stack_push (target->global->stack, one);
            dfs_stack_enter (target->global->stack);
            target->counters->level_cur++; // Dangerous to touch remote non-global (see allocation in worker.c)
        } else {
            dfs_stack_push (target->global->stack, one);
            dfs_stack_pop (source->global->stack);
        }
    }
    source->local->counters.splits++;
    source->local->counters.transfer += handoff;
    return handoff;
}

static inline void
reach_queue (void *arg, state_info_t *successor, transition_info_t *ti, int new)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    alg_shared_t       *shared = ctx->run->shared;

    if (new) {
        raw_data_t stack_loc = dfs_stack_push (sm->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        if (EXPECT_FALSE( trc_output && successor->ref != ctx->state->ref &&
                          ti != &GB_NO_TRANSITION )) // race, but ok:
            atomic_write (&shared->parent_ref[successor->ref], ctx->state->ref);
        loc->proviso |= proviso == Proviso_ClosedSet;
    } else if (proviso == Proviso_Stack) {
        loc->proviso |= !ecd_has_state (loc->cyan, successor);
    }

    action_detect (ctx, ti, successor);

    loc->proviso |= ti->por_proviso;
    ti->por_proviso = 1; // inform POR layer that everything is a-ok.
    // We will call next-state again if loc->proviso is not set.
}

static void
reach_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    reach_queue (arg, successor, ti, !seen);
}

static void
reach_handle_dfs (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    (void) seen;
    int             red = state_store_has_color(successor->ref, GRED, 0);
    reach_queue (arg, successor, ti, !red);
}

static inline size_t
reach_fulfil_ignoring_proviso (wctx_t *ctx, size_t successors, perm_cb_f cb)
{
    alg_local_t        *loc = ctx->local;
    counter_t          *cnt = &ctx->local->counters;
    if (proviso != Proviso_None && !loc->proviso && successors > 0) {
        // proviso does not hold, explore all:
        permute_set_por (ctx->permute, 0); // force all in permutor
        successors = permute_trans (ctx->permute, ctx->state, cb, ctx);
        permute_set_por (ctx->permute, 1); // back to default
        cnt->ignoring++;
    }
    return successors;
}

static inline void
explore_state (wctx_t *ctx, perm_cb_f cb)
{
    alg_local_t        *loc = ctx->local;
    size_t              count;
    if (ctx->counters->level_cur >= max_level)
        return;

    invariant_detect (ctx);
    loc->proviso = 0;
    count = permute_trans (ctx->permute, ctx->state, cb, ctx);
    count = reach_fulfil_ignoring_proviso (ctx, count, cb);

    ctx->counters->trans += count;
    ctx->counters->explored++;
    deadlock_detect (ctx, count);
    work_counter_t     *cnt = ctx->counters;
    run_maybe_report1 (ctx->run, cnt, "");
}

void
dfs_proviso (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    while (lb_balance(ctx->run->shared->lb, ctx->id, dfs_stack_size(sm->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        // strict DFS (use extra bit because the permutor already adds successors to V)
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            if (state_store_try_color(ctx->state->ref, GRED, 0)) {
                dfs_stack_enter (sm->stack);
                increase_level (ctx->counters);
                ecd_add_state (loc->cyan, ctx->state, NULL);
                explore_state (ctx, reach_handle_dfs);
            } else {
                dfs_stack_pop (sm->stack);
            }
        } else {
            if (0 == dfs_stack_nframes (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            ctx->counters->level_cur--;
            state_data = dfs_stack_pop (sm->stack);
            state_info_deserialize (ctx->state, state_data);
            ecd_remove_state (loc->cyan, ctx->state);
        }
    }
    HREassert (run_is_stopped(ctx->run) || fset_count(loc->cyan) == 0);
}

void
dfs (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    while (lb_balance(ctx->run->shared->lb, ctx->id, dfs_stack_size(sm->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            dfs_stack_enter (sm->stack);
            increase_level (ctx->counters);
            state_info_deserialize (ctx->state, state_data);
            explore_state (ctx, reach_handle);
        } else {
            if (0 == dfs_stack_nframes (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            ctx->counters->level_cur--;
            dfs_stack_pop (sm->stack);
        }
    }
}

void
bfs (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    while (lb_balance(ctx->run->shared->lb, ctx->id, bfs_load(sm), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            explore_state (ctx, reach_handle);
        } else {
            swap (sm->out_stack, sm->in_stack);
            sm->stack = sm->out_stack;
            increase_level (ctx->counters);
        }
    }
}

void
sbfs (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    size_t              next_level_size, local_next_size;
    do {
        while (lb_balance (ctx->run->shared->lb, ctx->id, in_load(sm), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
            if (NULL != state_data) {
                state_info_deserialize (ctx->state, state_data);
                explore_state (ctx, reach_handle);
            }
        }
        local_next_size = dfs_stack_frame_size (sm->out_stack);
        next_level_size = sbfs_level (ctx, local_next_size);
        lb_reinit (ctx->run->shared->lb, ctx->id);
        swap (sm->out_stack, sm->in_stack);
        sm->stack = sm->out_stack;
    } while (next_level_size > 0 && !run_is_stopped(ctx->run));
}

void
pbfs_queue_state (wctx_t *ctx, state_info_t *successor)
{
    alg_local_t        *loc = ctx->local;
    hash64_t            h = ref_hash (successor->ref);
    alg_global_t       *remote = ctx->run->contexts[h % W]->global;
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
    alg_shared_t       *shared = ctx->run->shared;

    if (!seen) {
        pbfs_queue_state (ctx, successor);
        if (EXPECT_FALSE( trc_output && successor->ref != ctx->state->ref &&
                          ti != &GB_NO_TRANSITION )) // race, but ok:
            atomic_write (&shared->parent_ref[successor->ref], ctx->state->ref);
        loc->counters.level_size++;
        loc->proviso |= 1;
    }

    action_detect (ctx, ti, successor);

    if (EXPECT_FALSE(loc->lts != NULL)) {
        int             src = ctx->counters->explored;
        int            *tgt = state_info_state(successor);
        int             tgt_owner = ref_hash (successor->ref) % W;
        lts_write_edge (loc->lts, ctx->id, &src, tgt_owner, tgt, ti->labels);
    }
}

void
pbfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    size_t              count;
    size_t              successors;
    raw_data_t          state_data;
    int                 labels[SL];
    do {
        loc->counters.level_size = 0;       // count states in next level
        loc->flip = 1 - loc->flip;          // switch in/out stacks
        for (size_t i = 0; i < W; i++) {
            size_t          current = (i << 1) + loc->flip;
            while ((state_data = isba_pop_int (sm->queues[current])) &&
                    !run_is_stopped(ctx->run)) {
                state_info_deserialize (ctx->state, state_data);
                state_data_t        state_data = state_info_state(ctx->state);
                invariant_detect (ctx);
                loc->proviso = 0;
                successors = permute_trans (ctx->permute, ctx->state, pbfs_handle, ctx);
                successors = reach_fulfil_ignoring_proviso (ctx, successors, pbfs_handle);
                ctx->counters->trans += successors;
                deadlock_detect (ctx, successors);
                ctx->counters->explored++;

                run_maybe_report1 (ctx->run, ctx->counters, "");
                if (EXPECT_FALSE(loc->lts && write_state)){
                    if (SL > 0)
                        GBgetStateLabelsAll (ctx->model, state_data, labels);
                    lts_write_state (loc->lts, ctx->id, state_data, labels);
                }
            }
        }
        count = sbfs_level (ctx, loc->counters.level_size);
    } while (count && !run_is_stopped(ctx->run));
}

static void
add_results (counter_t *res, counter_t *cnt)
{
    res->level_size += cnt->level_size;
    res->splits += cnt->splits;
    res->transfer += cnt->transfer;
    res->deadlocks += cnt->deadlocks;
    res->violations += cnt->violations;
    res->errors += cnt->errors;
    res->ignoring += cnt->ignoring;
}

void
reach_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (alg_reduced_t));
        statistics_init (&run->reduced->state_stats);
        statistics_init (&run->reduced->trans_stats);
    }
    alg_reduced_t          *reduced = run->reduced;
    counter_t              *cnt = &ctx->local->counters;

    statistics_record (&reduced->state_stats, ctx->counters->explored);
    statistics_record (&reduced->trans_stats, ctx->counters->trans);
    add_results (&reduced->counters, cnt);

    if (W >= 4 || !log_active(infoLong)) return;

    // print some local info
    float                   runtime = RTrealTime(ctx->timer);
    Warning (info, "%s worker ran %.3f sec",
             strupper(key_search(strategies, get_strategy(ctx->run->alg))),
             runtime);
    work_report ("", ctx->counters);

    if (Strat_ECD & strategy[1]) {
        fset_print_statistics (ctx->local->cyan, "ECD set");
    }
}

void
reach_print_stats   (run_t *run, wctx_t *ctx)
{
    alg_reduced_t          *reduced = run->reduced;
    work_counter_t         *cnt_work = &run->total;

    if (W > 1)
        Warning (info, "mean standard work distribution: %.1f%% (states) %.1f%% (transitions)",
                 (100 * statistics_stdev(&reduced->state_stats) /
                        statistics_mean(&reduced->state_stats)),
                 (100 * statistics_stdev(&reduced->trans_stats) /
                        statistics_mean(&reduced->trans_stats)));
    Warning (info, " ");
    cnt_work->level_max /= W;

    run_report_total (run);

    counter_t          *cnt = &reduced->counters;
    if (no_exit) {
        Warning (info, " ");
        Warning (info, "Reachability properties:");
        Warning (info, "Deadlocks: %zu", cnt->deadlocks);
        Warning (info, "Invariant/valid-end state violations: %zu",
                       cnt->violations);
        Warning (info, "Error actions: %zu", cnt->errors);
    }
    if (proviso != Proviso_None) {
        Warning (info, "Ignoring proviso: %zu", cnt->ignoring);
    }

    Warning (infoLong, " ");
    Warning (infoLong, "Load balancer:");
    Warning (infoLong, "Splits: %zu", cnt->splits);
    Warning (infoLong, "Load transfer: %zu",  cnt->transfer);

    // part of reduce (should happen only once), publishes mem stats for the run class
    if (get_strategy(run->alg) & (Strat_SBFS | Strat_PBFS)) {
        cnt_work->local_states += run->shared->max_level_size;  // SBFS queues
    } else {
        cnt_work->local_states += lb_max_load(ctx->run->shared->lb);
    }
}

void
reach_local_setup   (run_t *run, wctx_t *ctx)
{
    if ((strategy[0] & Strat_DFS) & Proviso_Stack) {
        ctx->local->cyan = fset_create (sizeof(ref_t), 0, 10, 20);
    }

    ctx->local->inv_expr = NULL;
    if (inv_detect) { // local parsing
        ctx->local->env = LTSminParseEnvCreate();
        ctx->local->inv_expr = pred_parse_file (inv_detect, ctx->local->env, GBgetLTStype(ctx->model));
        set_pins_semantics (ctx->model, ctx->local->inv_expr, ctx->local->env, NULL, NULL);
    }

    if (PINS_POR && act_detect) {
        pins_add_edge_label_visible (ctx->model, act_label, act_index);
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
    size_t              len = state_info_serialize_int_size (ctx->state);
    if (get_strategy(run->alg) & Strat_PBFS) {
        ctx->global->queues = RTmalloc (sizeof(isb_allocator_t[2][W]));
        for (size_t i = 0; i < 2 * W; i++)
            ctx->global->queues[i] = isba_create (len);
    } else {
        ctx->global->out_stack = ctx->global->stack =
                dfs_stack_create (len);
        if (get_strategy(run->alg) & Strat_2Stacks) {
            ctx->global->in_stack = dfs_stack_create (len);
        }
    }

    lb_local_init (run->shared->lb, ctx->id, ctx); // Barrier
}

void
reach_global_init   (run_t *run, wctx_t *ctx)
{
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
                state_data_t        initial = state_info_state(ctx->initial);
                int             src_owner = ref_hash(ctx->initial->ref) % W;
                lts_write_init (ctx->local->lts, src_owner, initial);
            }
            pbfs_queue_state (ctx, ctx->initial);
        } else {
            reach_handle (ctx, ctx->initial, &ti, 0);
        }
        ctx->counters->trans = 0; //reset trans count
    }

    HREbarrier (HREglobal());

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
        if (PINS_POR && (proviso == Proviso_Stack || proviso == Proviso_ClosedSet)) {
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
    (void) run;
}

static int
reach_stop (run_t *run)
{
    return lb_stop (run->shared->lb);
}

static int
reach_is_stopped (run_t *run)
{
    return lb_is_stopped (run->shared->lb);
}

void
reach_init_shared (run_t *run)
{
    if (run->shared == NULL)
        run->shared = RTmallocZero (sizeof (alg_shared_t));
    run->shared->lb = lb_create_max (W, G, H);
    run_set_is_stopped (run, reach_is_stopped);
    run_set_stop (run, reach_stop);

    if (trc_output) {
        run->shared->parent_ref = RTmallocZero (sizeof(ref_t[1UL<<dbs_size]));
    }
}

void
reach_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, reach_local_init);
    set_alg_global_init (run->alg, reach_global_init);
    set_alg_global_deinit (run->alg, reach_destroy);
    set_alg_local_deinit (run->alg, reach_destroy_local);
    set_alg_print_stats (run->alg, reach_print_stats);
    set_alg_run (run->alg, reach_run);
    set_alg_reduce (run->alg, reach_reduce);

    reach_init_shared (run);
}
