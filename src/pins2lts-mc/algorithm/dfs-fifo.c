/**
 *
 */

#include <hre/config.h>

#include <stdbool.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins-util.h>
#include <pins2lts-mc/algorithm/dfs-fifo.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

static int              strict_dfsfifo = 0;

struct poptOption dfs_fifo_options[] = {
    {"strict", 0, POPT_ARG_VAL, &strict_dfsfifo, 1, "turn on struct BFS in DFS_FIFO", NULL},
    POPT_TABLEEND
};

typedef struct df_counter_s {
    size_t              progress;       // counter: progress states
    size_t              bfs_level_cur;
    size_t              bfs_level_max;
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

static void
dfs_fifo_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                 int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    ctx->local->counters.trans++;
    bool is_progress = loc->progress_trans > 0 ? loc->progress[ti->group] :  // ! progress transition
            GBstateIsProgress(ctx->model, get_state(successor->ref, ctx)); // ! progress state

    if (!is_progress && seen && ecd_has_state(loc->cyan, successor))
         //ndfs_report_cycle (ctx, successor); //TODO alg_local
        Abort ("cycle found!");

    // dfs_fifo_dfs/dfs_fifo_bfs also check this, but we want a clean stack for LB!
    if (global_has_color(ctx->state.ref, setV, 0))
        return;
    if (!is_progress) {
        raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->local->counters.visited++;
    } else if (global_try_color(successor->ref, setF, 0)) { // new F state
        raw_data_t stack_loc = dfs_stack_push (sm->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        loc->df_counters.progress += !seen;
    }
    (void) ti;
}

typedef size_t (*lb_load_f)(alg_global_t *);

static void
dfs_fifo_dfs (wctx_t *ctx, ref_t seed,
              lb_load_f load, // TODO unify functions
              lb_split_problem_f split)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    counter_t          *cnt = &ctx->local->counters;
    while (!lb_is_stopped(global->lb)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if (!global_has_color(ctx->state.ref, setV, 0)) {
                if (ctx->state.ref != seed && ctx->state.ref != loc->seed)
                    ecd_add_state (loc->cyan, &ctx->state, NULL);
                dfs_stack_enter (sm->stack);
                increase_level (&cnt->level_cur, &cnt->level_max);
                permute_trans (ctx->permute, &ctx->state, dfs_fifo_handle, ctx);
                maybe_report1 (cnt->explored, cnt->trans, cnt->level_max, "");
                ctx->local->counters.explored++;
            } else {
                dfs_stack_pop (sm->stack);
            }
        } else {
            if (dfs_stack_nframes(sm->stack) == 0)
                break;
            dfs_stack_leave (sm->stack);
            state_data = dfs_stack_pop (sm->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            ctx->local->counters.level_cur--;
            if (ctx->state.ref != seed && ctx->state.ref != loc->seed) {
                ecd_remove_state (loc->cyan, &ctx->state);
            }
            global_try_color (ctx->state.ref, setV, 0);
        }
        // load balance the FIFO queue (this is a synchronizing affair)
        lb_balance (global->lb, ctx->id, load(sm)+1, split); //never report 0 load!
    }
    HREassert (lb_is_stopped(global->lb) || fset_count(loc->cyan) == 0,
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
        while (lb_balance (global->lb, ctx->id, in_load(sm), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
            if (NULL != state_data) {
                state_info_deserialize_cheap (&ctx->state, state_data);
                //if (!global_has_color(ctx->state.ref, setV, 0)) { //checked in dfs_fifo_dfs
                    dfs_stack_push (sm->stack, state_data);
                    dfs_fifo_dfs (ctx, ctx->state.ref, in_load, split_sbfs);
                //}
            }
        }
        size = dfs_stack_frame_size (sm->out_stack);
        HREreduce (HREglobal(), 1, &size, &out_size, SizeT, Sum);
        lb_reinit (global->lb, ctx->id);
        increase_level (&df_cnt->bfs_level_cur, &df_cnt->bfs_level_max);
        if (ctx->id == 0) {
            if (out_size > max_level_size) max_level_size = out_size;
            total += out_size;
            Warning(infoLong, "DFS-FIFO level %zu has %zu states %zu total",
                              df_cnt->bfs_level_cur, out_size, total);
        }
        swap (sm->out_stack, sm->in_stack);
    } while (out_size > 0 && !lb_is_stopped(global->lb));
}

void
dfs_fifo_bfs (wctx_t *ctx)
{
    df_alg_local_t     *loc = (df_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    df_counter_t       *cnt = &loc->df_counters;
    while (lb_balance (global->lb, ctx->id, bfs_load(sm), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
        if (NULL != state_data) {
            state_info_deserialize_cheap (&ctx->state, state_data);
            //if (!global_has_color(ctx->state.ref, GRED, 0)) { //checked in dfs_fifo_dfs
                dfs_stack_push (sm->stack, state_data);
                dfs_fifo_dfs (ctx, ctx->state.ref, bfs_load, split_bfs);
            //}
        } else {
            size_t out_size = dfs_stack_frame_size (sm->out_stack);
            if (out_size > atomic_read(&max_level_size))
                atomic_write(&max_level_size, out_size * W); // over-estimation
            increase_level (&cnt->bfs_level_cur, &cnt->bfs_level_max);
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
    // find progress transitions
    lts_type_t      ltstype = GBgetLTStype (ctx->model);
    int             statement_label = lts_type_find_edge_label (
                                     ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
    if (statement_label != -1) {
        int             statement_type = lts_type_get_edge_label_typeno (
                                                 ltstype, statement_label);
        size_t          count = GBchunkCount (ctx->model, statement_type);
        if (count >= K) {
            loc->progress = RTmallocZero (sizeof(int[K]));
            for (size_t i = 0; i < K; i++) {
                chunk c = GBchunkGet (ctx->model, statement_type, i);
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
           int progress_sl = GBgetProgressStateLabelIndex (ctx->model);
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
    ctx->global->out_stack = dfs_stack_create (state_info_int_size());
}

void
dfs_fifo_destroy_local   (run_t *run, wctx_t *ctx)
{
    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    if (loc->progress)
        RTfree (loc->progress);
    fset_free (loc->cyan);
    RTfree (loc);
}

void
dfs_fifo_run  (run_t *run, wctx_t *ctx)
{
    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    alg_local_t            *df_loc = (alg_local_t *) loc;
    raw_data_t stack_loc = dfs_stack_push (ctx->global->in_stack, NULL);
    state_info_serialize (&ctx->initial, stack_loc);
    int acc = GBbuchiIsAccepting (ctx->model, ctx->initial.data);
    loc->seed = acc ? (ref_t)-1 : ctx->initial.ref;
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
    res->bfs_level_max += cnt->bfs_level_max;
}

void
dfs_fifo_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (dfs_fifo_reduced_t));
    }

    reach_reduce (run, ctx);

    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    df_counter_t           *cnt = &loc->df_counters;
    dfs_fifo_reduced_t     *reduced = (dfs_fifo_reduced_t *) run->reduced;
    add_results (&reduced->df_counter, cnt);
}

void
dfs_fifo_print_stats   (run_t *run, wctx_t *ctx)
{
    reach_print_stats (run, ctx); // TODO: NDFS?

    df_alg_local_t         *loc = (df_alg_local_t *) ctx->local;
    Warning (info, "Progress states detected: %zu", loc->df_counters.progress);
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
}

