/**
 *
 */

#include <hre/config.h>

#include <pins-lib/pins-util.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins2lts-mc/algorithm/dfs-fifo.h>

static int              strict_dfsfifo = 0;
static int              force_progress_states = 0;
//TODO: reuse reach

struct poptOption dfs_fifo_options[] = {
    {"strict", 0, POPT_ARG_VAL, &strict_dfsfifo, 1, "turn on strict BFS in DFS_FIFO", NULL},
    {"progress-states", 0, POPT_ARG_VAL, &force_progress_states, 1, "Use progress states", NULL},
    POPT_TABLEEND
};

typedef struct df_counter_s {
    size_t              progress;       // counter: progress states
    size_t              bfs_level_cur;
    size_t              max_level_size;
} df_counter_t;

typedef struct df_alg_local_s {
    alg_local_t         reach;

    fset_t             *cyan;           // Proviso stack
    ref_t               seed;
    int                *progress;       // progress transitions
    size_t              progress_trans; // progress transitions
    df_counter_t        df_counters;
} df_alg_local_t;

typedef struct dfs_fifo_reduced_s {
    alg_reduced_t       reach;
    df_counter_t        df_counter;
} dfs_fifo_reduced_t;

/**
 * DFS-FIFO for non-progress detection
 */

#define setV GRED
#define setF GGREEN

static inline bool
has_progress (wctx_t *ctx, transition_info_t *ti, state_info_t *successor)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    if (PINS_LTL) {
        return pins_state_is_weak_ltl_progress (ctx->model, state_info_state (successor));
    } else if (loc->progress_trans > 0) {
        return loc->progress[ti->group] ; // ! progress transition
    } else {
        return pins_state_is_progress (ctx->model, state_info_state (successor));
    }
}

static void
construct_np_lasso (wctx_t* ctx, state_info_t* successor)
{
    alg_global_t       *sm = ctx->global;
    alg_shared_t       *shared = ctx->run->shared;
    work_counter_t     *cnt = ctx->counters;
    ref_t              *trace;
    size_t              length;
    size_t              level = cnt->level_cur;

    Warning (info, " ");
    Warning (info, "Non-progress cycle FOUND at depth %zu!", cnt->level_cur);
    Warning (info, " ");

    trace = trc_find_trace (successor->ref, level, shared->parent_ref,
                            ctx->initial->ref, &length);
    dfs_stack_t s = dfs_stack_create (state_info_serialize_int_size (ctx->state));
    for (int i = 0; i < length; i++) {
        raw_data_t d = dfs_stack_push (s, NULL);
        state_info_set (ctx->state, trace[i], LM_NULL_LATTICE);
        state_info_serialize (ctx->state, d);
        dfs_stack_enter (s);
    }
    int idx = -1;
    for (int i = 1; i < dfs_stack_nframes (sm->stack); i++) {
        raw_data_t d = dfs_stack_peek_top (sm->stack, i);
        state_info_deserialize (ctx->state, d);
        if (ctx->state->ref == successor->ref) {
            idx = i;
            break;
        }
    }
    HREassert(idx != -1, "Could not find successor %zu on DFS-FIFO stack.", successor->ref);
    for (int i = idx - 1; i >= 1; i--) {
        raw_data_t d = dfs_stack_peek_top (sm->stack, i);
        dfs_stack_push (s, d);
        dfs_stack_enter (s);
    }
    raw_data_t d = dfs_stack_push (s, NULL);
    state_info_serialize (successor, d);
    find_and_write_dfs_stack_trace (ctx->model, s, true);
    dfs_stack_destroy (s);
}

static void
dfs_fifo_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                 int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    alg_shared_t       *shared = ctx->run->shared;
    ctx->counters->trans++;
    bool                is_progress = has_progress (ctx, ti, successor);

    if (EXPECT_FALSE( trc_output && !seen && successor->ref != ctx->state->ref &&
                      ti != &GB_NO_TRANSITION )) // race, but ok:
        atomic_write (&shared->parent_ref[successor->ref], ctx->state->ref);

    if (!is_progress && seen && ecd_has_state(loc->cyan, successor)) {
        global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
        if (run_stop(ctx->run) && trc_output) {
            construct_np_lasso (ctx, successor);
        }
    }

    // dfs_fifo_dfs/dfs_fifo_bfs also check this, but we want a clean stack for LB!
    if (state_store_has_color(ctx->state->ref, setV, 0))
        return;
    if (!is_progress) {
        raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, stack_loc);
    } else if (state_store_try_color(successor->ref, setF, 0)) { // new F state
        raw_data_t stack_loc = dfs_stack_push (sm->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        loc->df_counters.progress += !seen;
    }
    (void) ti;
}

typedef size_t (*lb_load_f)(alg_global_t *);

static void
dfs_fifo_dfs (wctx_t *ctx, ref_t seed,
              lb_load_f load,
              lb_split_problem_f split)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    work_counter_t     *cnt = ctx->counters;
    while (!run_is_stopped(ctx->run)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            if (!state_store_has_color(ctx->state->ref, setV, 0)) {
                if (ctx->state->ref != seed && ctx->state->ref != loc->seed)
                    ecd_add_state (loc->cyan, ctx->state, NULL);
                dfs_stack_enter (sm->stack);
                increase_level (cnt);
                permute_trans (ctx->permute, ctx->state, dfs_fifo_handle, ctx);
                cnt->explored++;
                run_maybe_report1 (ctx->run, cnt, "");
            } else {
                dfs_stack_pop (sm->stack);
            }
        } else {
            if (dfs_stack_nframes(sm->stack) == 0)
                break;
            dfs_stack_leave (sm->stack);
            state_data = dfs_stack_pop (sm->stack);
            state_info_deserialize (ctx->state, state_data);
            cnt->level_cur--;
            if (ctx->state->ref != seed && ctx->state->ref != loc->seed) {
                ecd_remove_state (loc->cyan, ctx->state);
            }
            state_store_try_color (ctx->state->ref, setV, 0);
        }
        // load balance the FIFO queue (this is a synchronizing affair)
        lb_balance (ctx->run->shared->lb, ctx->id, load(sm)+1, split); //never report 0 load!
    }
    HREassert (run_is_stopped(ctx->run) || fset_count(loc->cyan) == 0,
               "DFS stack not empty, size: %zu", fset_count(loc->cyan));
}

void
dfs_fifo_sbfs (wctx_t *ctx)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    df_counter_t       *df_cnt = &loc->df_counters;
    size_t              total = 0;
    size_t              out_size, size;
    do {
        while (lb_balance (ctx->run->shared->lb, ctx->id, in_load(sm), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
            if (NULL != state_data) {
                state_info_deserialize (ctx->state, state_data);
                //if (!global_has_color(ctx->state->ref, setV, 0)) { //checked in dfs_fifo_dfs
                    dfs_stack_push (sm->stack, state_data);
                    dfs_fifo_dfs (ctx, ctx->state->ref, in_load, split_sbfs);
                //}
            }
        }
        size = dfs_stack_frame_size (sm->out_stack);
        HREreduce (HREglobal(), 1, &size, &out_size, SizeT, Sum);
        lb_reinit (ctx->run->shared->lb, ctx->id);
        df_cnt->bfs_level_cur++;
        if (ctx->id == 0) {
            if (out_size > ctx->run->shared->max_level_size)
                ctx->run->shared->max_level_size = out_size;
            total += out_size;
            Warning(infoLong, "DFS-FIFO level %zu has %zu states %zu total",
                              df_cnt->bfs_level_cur, out_size, total);
        }
        swap (sm->out_stack, sm->in_stack);
    } while (out_size > 0 && !run_is_stopped(ctx->run));
}

void
dfs_fifo_bfs (wctx_t *ctx)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    df_counter_t       *cnt = &loc->df_counters;
    df_counter_t       *df_cnt = &loc->df_counters;
    while (lb_balance (ctx->run->shared->lb, ctx->id, bfs_load(sm), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            //if (!global_has_color(ctx->state->ref, GRED, 0)) { //checked in dfs_fifo_dfs
                dfs_stack_push (sm->stack, state_data);
                dfs_fifo_dfs (ctx, ctx->state->ref, bfs_load, split_bfs);
            //}
        } else {
            size_t out_size = dfs_stack_frame_size (sm->out_stack);
            if (out_size > cnt->max_level_size)
                cnt->max_level_size = out_size; // over-estimation
            df_cnt->bfs_level_cur++;
            swap (sm->out_stack, sm->in_stack);
        }
    }
}
void
dfs_fifo_local_init   (run_t *run, wctx_t *ctx)
{
    if (proviso != Proviso_None)
        Abort ("DFS_FIFO does not require a proviso.");

    ctx->local = RTmallocZero (sizeof(df_alg_local_t));
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;

    loc->cyan = fset_create (sizeof(ref_t), 0, 10, 20);

    if (PINS_LTL) {
        int     label = pins_get_weak_ltl_progress_state_label_index(ctx->model);
        HREassert (label != -1, "DFS-FIFO with LTL layer, but no special progress label found!");
        Print1 (info, "DFS-FIFO for weak LTL, using special progress label %d", label);
        HREassert (!PINS_POR, "DFS-FIFO for weak LTL is not supported yet in combination with POR.");
        return;
    }

    // find progress transitions
    lts_type_t      ltstype = GBgetLTStype (ctx->model);
    int             statement_label = lts_type_find_edge_label (
                                     ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (statement_label != -1 && !force_progress_states) {
        int             statement_type = lts_type_get_edge_label_typeno (
                                                 ltstype, statement_label);
        size_t          count = pins_chunk_count (ctx->model, statement_type);
        if (count >= K) {
            loc->progress = RTmallocZero (sizeof(int[K]));
            for (size_t i = 0; i < K; i++) {
                chunk c = pins_chunk_get  (ctx->model, statement_type, i);
                loc->progress[i] = strstr(c.data, LTSMIN_VALUE_STATEMENT_PROGRESS) != NULL;
                loc->progress_trans += loc->progress[i];
            }
        }
    }

    if (PINS_POR) {
        if (loc->progress_trans > 0) {
            // Progress transitions available
           int *visibles = GBgetPorGroupVisibility(ctx->model);
           for (size_t i = 0; i < K; i++) {
               if (loc->progress[i]) visibles[i] = 1;
           }
           if (strategy[0] & Strat_DFSFIFO)
               Print1 (info, "Found %zu progress transitions.", loc->progress_trans);
       } else {
           // Use progress states
           Print1 (info, "No progress transitions defined for DFS_FIFO, "
                         "using progress states via progress state label")
           int progress_sl = pins_get_progress_state_label_index (ctx->model);
           HREassert (progress_sl >= 0, "No progress labels defined for DFS_FIFO");
           pins_add_state_label_visible (ctx->model, progress_sl);
       }
    }

    (void) run;
}

void
dfs_fifo_global_init   (run_t *run, wctx_t *ctx)
{
    ctx->global = RTmallocZero (sizeof(alg_global_t));
    reach_global_setup (run, ctx);
    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->global->out_stack = dfs_stack_create (len);
}

void
dfs_fifo_destroy_local   (run_t *run, wctx_t *ctx)
{
    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    if (loc->progress)
        RTfree (loc->progress);
    fset_free (loc->cyan);
    RTfree (loc);
    (void) run;
}

void
dfs_fifo_run  (run_t *run, wctx_t *ctx)
{
    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    raw_data_t stack_loc = dfs_stack_push (ctx->global->in_stack, NULL);
    state_info_serialize (ctx->initial, stack_loc);
    int acc = pins_state_is_progress (ctx->model, state_info_state(ctx->initial));
    loc->seed = acc ? (ref_t)-1 : ctx->initial->ref;
    loc->df_counters.progress += acc;

    if (strict_dfsfifo) {
        dfs_fifo_sbfs (ctx);
    } else {
        dfs_fifo_bfs (ctx);
    }

    (void) run;
}

static void
add_results (df_counter_t *res, df_counter_t *cnt)
{
    res->progress += cnt->progress;
    res->bfs_level_cur += cnt->bfs_level_cur;
}

void
dfs_fifo_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (dfs_fifo_reduced_t));
    }

    reach_reduce (run, ctx);

    // publish memory statistics to run class:
    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    df_counter_t           *cnt = &loc->df_counters;
    run->total.local_states += ctx->counters->level_max;  // DFS stacks
    if (!strict_dfsfifo) {
        run->total.local_states += cnt->max_level_size;   // BFS queues
    }

    dfs_fifo_reduced_t     *reduced = (dfs_fifo_reduced_t *) run->reduced;
    add_results (&reduced->df_counter, cnt);
}

void
dfs_fifo_print_stats   (run_t *run, wctx_t *ctx)
{
    reach_print_stats (run, ctx);

    // part of reduce (should happen only once), publishes mem stats for the run class
    // run->local_mem += run->shared->max_level_size;  // DONE BY REACH (SBFS queues)

    dfs_fifo_reduced_t     *reduced = (dfs_fifo_reduced_t *) run->reduced;
    Warning (info, " ");
    Warning (info, "Progress states detected: %zu", reduced->df_counter.progress);
    Warning (info, "Redundant explorations: %.4f", ((double)100 *
                                                   run->total.explored) /
                                                   global->stats.elts - 100);
}

void
dfs_fifo_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, dfs_fifo_local_init);
    set_alg_global_init (run->alg, dfs_fifo_global_init);
    set_alg_global_deinit (run->alg, reach_destroy);
    set_alg_local_deinit (run->alg, dfs_fifo_destroy_local);
    set_alg_print_stats (run->alg, dfs_fifo_print_stats);
    set_alg_run (run->alg, dfs_fifo_run);
    set_alg_reduce (run->alg, dfs_fifo_reduce);

    reach_init_shared (run);
}

