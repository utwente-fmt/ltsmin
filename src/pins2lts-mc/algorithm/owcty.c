/**
 *
 */

#include <hre/config.h>

#include <stdbool.h>

#include <mc-lib/lb.h>
#include <pins-lib/pins-util.h>
#include <pins2lts-mc/algorithm/owcty.h>

// TODO: reuse reachability algorithms with CNDFS recursive run structure

typedef struct owcty_ext_s {
    uint32_t                count : 30;
    uint32_t                bit : 1;
    uint32_t                acc : 1;
} __attribute ((packed)) owcty_pre_t;

struct alg_shared_s {
    owcty_pre_t        *pre;
    lb_t               *lb;
    ref_t              *parent_ref;     // predecessor counts
};

struct alg_global_s {
    dfs_stack_t         stack;          // Successor stack
    dfs_stack_t         in_stack;       // Input stack
    dfs_stack_t         out_stack;      // Output stack
    work_counter_t      ecd;            //
};

struct alg_local_s {
    int                 flip;           // OWCTY invert state space bit
    ssize_t             iteration;      // OWCTY: 0=init, uneven=reach, even=elim
    fset_t             *cyan;           // OWCTY: cyan ECD set
    rt_timer_t          timer;
};

static int              owcty_do_reset = 0;
static int              owcty_ecd_all = 0;

struct poptOption owcty_options[] = {
    {"owcty-reset", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &owcty_do_reset, 1,
     "turn on reset in OWCTY algorithm", NULL},
    {"all-ecd", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &owcty_ecd_all, 1,
     "turn on ECD during all iterations (normally only during initialization)", NULL},
    POPT_TABLEEND
};

static inline owcty_pre_t
owcty_pre_read (run_t *run, ref_t ref)
{
    return *(owcty_pre_t *)&atomic_read (&run->shared->pre[ref]);
}

static inline void
owcty_pre_write (run_t *run, ref_t ref, owcty_pre_t val)
{
    uint32_t           *v = (uint32_t *)&val;
    atomic_write (((uint32_t *)run->shared->pre) + ref, *v);
}

static inline int
owcty_pre_cas (run_t *run, ref_t ref, owcty_pre_t now, owcty_pre_t val)
{
    uint32_t           *n = (uint32_t *)&now;
    uint32_t           *v = (uint32_t *)&val;
    return cas (((uint32_t *)run->shared->pre) + ref, *n, *v);
}

static inline uint32_t
owcty_pre_inc (run_t *run, ref_t ref, uint32_t val)
{
    uint32_t            x = add_fetch (&((uint32_t *)run->shared->pre)[ref], val);
    owcty_pre_t        *y = (owcty_pre_t *)&x;
    return y->count;
}

static inline uint32_t
owcty_pre_count (run_t *run, ref_t ref)
{
    return owcty_pre_read (run, ref).count;
}

static inline void
owcty_pre_reset (run_t *run, ref_t ref, int bit, int acc)
{
    owcty_pre_t             pre;
    pre.acc = acc;
    pre.bit = bit;
    pre.count = 0;
    owcty_pre_write (run, ref, pre);
}

static bool
owcty_pre_try_reset (run_t *run, ref_t ref, uint32_t reset_val, int bit,
                     bool check_zero)
{
    owcty_pre_t             pre, orig;
    do {
        orig = owcty_pre_read (run, ref);
        if (orig.bit == bit || (check_zero && orig.count == 0))
            return false;
        pre.acc = orig.acc;
        pre.bit = bit;
        pre.count = reset_val;
    } while (!owcty_pre_cas(run, ref, orig, pre));
    return true;
}

/**
 * A reset can be skipped, because the in_stack is also checked during the
 * owcty_reachability call. However, this inlined reset costs more cas operations,
 * while an explicit reset costs an additional synchronization but is not
 * load balanced.
 */
static size_t
owcty_reset (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    state_data_t            data;
    if (loc->iteration == 0) {
        while ((data = dfs_stack_pop (sm->in_stack)) && !run_is_stopped(ctx->run)) {
            state_info_deserialize (ctx->state, data);
            owcty_pre_reset (ctx->run, ctx->state->ref, loc->flip, 1);
            dfs_stack_push (sm->stack, data);
        }
    } else {
        while ((data = dfs_stack_pop (sm->in_stack)) && !run_is_stopped(ctx->run)) {
            state_info_deserialize (ctx->state, data);
            if ( 0 != owcty_pre_count(ctx->run, ctx->state->ref) ) {
                dfs_stack_push (sm->stack, data);
                owcty_pre_reset (ctx->run, ctx->state->ref, loc->flip, 1);
            }
        }
    }

    size_t size = dfs_stack_size (sm->stack);
    HREreduce (HREglobal(), 1, &size, &size, SizeT, Sum);
    return size;
}

static void
owcty_CE (run_t *run)
{
    /* Stop other workers, exit if some other worker was first here */
    if ( !run_stop(run) )
        return;
    int                 level = -1;
    Warning (info, " ");
    Warning (info, "Accepting cycle FOUND at depth %d!", level);
    Warning (info, " ");
}

static inline void
owcty_map (wctx_t *ctx, state_info_t *successor)
{
    alg_shared_t       *shared = ctx->run->shared;
    ref_t               map_pred = atomic_read (shared->parent_ref+ctx->state->ref);
    if ( pins_state_is_accepting(ctx->model, state_info_state(successor)) ) {
        if (successor->ref == ctx->state->ref || map_pred == successor->ref) {
            //ndfs_report_cycle (ctx, successor);
            owcty_CE(ctx->run);
            //Abort ("cycle found!");
        }
        size_t              num = successor->ref + 1;
        map_pred = max (num, map_pred);
    }
    atomic_write (shared->parent_ref+successor->ref, map_pred);
}

static inline void
owcty_ecd (wctx_t *ctx, state_info_t *successor)
{
    alg_local_t        *loc = ctx->local;
    uint32_t acc_level = ecd_get_state (loc->cyan, successor);
    if (acc_level < ctx->global->ecd.level_cur) {
        //ndfs_report_cycle (ctx, successor);
        owcty_CE(ctx->run);
        //Abort ("Cycle found!");
    }
}

static ssize_t
owcty_split (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    alg_local_t        *src_loc = source->local;
    alg_global_t       *src_sm = source->global;
    alg_global_t       *tgt_sm = target->global;
    HREassert (tgt_sm->ecd.level_cur == 0, "Target accepting level counter is off");
    size_t              in_size = dfs_stack_size (src_sm->stack);
    size_t              todo = min (in_size >> 1, handoff);
    for (size_t i = 0; i < todo; i++) {
        state_data_t        one = dfs_stack_top (src_sm->stack);
        if (!one) { // drop the state as it already explored!!!
            dfs_stack_leave (src_sm->stack);
            source->counters->level_cur--;
            one = dfs_stack_pop (src_sm->stack);
            if (((src_loc->iteration & 1) == 1 || src_loc->iteration == 0) && // only in the initialization / reachability phase
                    Strat_ECD == strategy[1]) {
                state_info_deserialize (source->state, one);
                if (pins_state_is_accepting(source->model, state_info_state(source->state))) {
                    HREassert (src_sm->ecd.level_cur != 0, "Source accepting level counter is off");
                    src_sm->ecd.level_cur--;
                }
                ecd_remove_state (src_loc->cyan, source->state);
            }
        } else {
            dfs_stack_push (tgt_sm->stack, one);
            dfs_stack_pop (src_sm->stack);
        }
    }
    if ( (src_loc->iteration & 1) == 1 ) { // only in the reachability phase
        size_t              in_size = dfs_stack_size (src_sm->in_stack);
        size_t              todo2 = min (in_size >> 1, handoff);
        for (size_t i = 0; i < todo2; i++) {
            state_data_t        one = dfs_stack_pop (src_sm->in_stack);
            dfs_stack_push (tgt_sm->in_stack, one);
        }
        todo += todo2;
    }
    return todo;
}

static void
owcty_initialize_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                         int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_global_t       *sm = ctx->global;
    if (!seen) {
        raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, stack_loc);
    } else if (strategy[1] == Strat_ECD)
        owcty_ecd (ctx, successor);
    if (strategy[1] == Strat_MAP)
        owcty_map (ctx, successor);
    ctx->counters->trans++;
    (void) ti;
}

static void
owcty_reachability_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                           int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    if (owcty_pre_try_reset(ctx->run, successor->ref, 1, loc->flip, false)) {
        raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, stack_loc);
    } else {
        if (strategy[1] == Strat_ECD)
            owcty_ecd (ctx, successor);
        uint32_t num = owcty_pre_inc (ctx->run, successor->ref, 1);
        HREassert (num < (1UL<<30)-1, "Overflow in accepting predecessor counter");
    }
    if (strategy[1] == Strat_MAP)
        owcty_map (ctx, successor);
    ctx->counters->trans++;
    (void) ti; (void) seen;
}

static inline size_t
owcty_load (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    return dfs_stack_size(sm->stack) + dfs_stack_size(sm->in_stack);
}

/**
 * bit == flip (visited by other workers reachability)
 *    ignore
 * bit != flip
 *    count > 0:    reset(0) & explore
 *    count == 0:   ignore (eliminated)
 */
static size_t
owcty_reachability (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    size_t              visited = 0; // number of visited states
    perm_cb_f handle = loc->iteration == 0 ? owcty_initialize_handle : owcty_reachability_handle;

    while (lb_balance(ctx->run->shared->lb, ctx->id, owcty_load(ctx), owcty_split)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);
            dfs_stack_enter (sm->stack);
            increase_level (ctx->counters);
            bool accepting = pins_state_is_accepting(ctx->model, state_info_state(ctx->state));
            if (strategy[1] == Strat_ECD) {
                ecd_add_state (loc->cyan, ctx->state, &sm->ecd.level_cur);
                sm->ecd.level_cur += accepting;
            }
            if ( accepting )
                dfs_stack_push (sm->out_stack, state_data);
            permute_trans (ctx->permute, ctx->state, handle, ctx);
            visited++;
            ctx->counters->explored++;
            run_maybe_report1 (ctx->run, ctx->counters, "");
        } else {
            if (0 == dfs_stack_nframes (sm->stack)) {
                while ((state_data = dfs_stack_pop (sm->in_stack))) {
                    state_info_deserialize (ctx->state, state_data);
                    if (owcty_pre_try_reset(ctx->run, ctx->state->ref, 0, loc->flip,
                                            loc->iteration > 1)) { // grab & reset
                        dfs_stack_push (sm->stack, state_data);
                        break;
                    }
                }
                continue;
            }
            dfs_stack_leave (sm->stack);
            ctx->counters->level_cur--;
            if (strategy[1] == Strat_ECD) {
                state_data = dfs_stack_top (sm->stack);
                state_info_deserialize (ctx->state, state_data);
                if (pins_state_is_accepting(ctx->model, state_info_state(ctx->state))) {
                    HREassert (sm->ecd.level_cur != 0, "Accepting level counter is off");
                    sm->ecd.level_cur--;
                }
                ecd_remove_state (loc->cyan, ctx->state);
            }
            dfs_stack_pop (sm->stack);
        }
    }

    if (strategy[1] == Strat_ECD && !run_is_stopped(ctx->run))
        HREassert (fset_count(loc->cyan) == 0 && sm->ecd.level_cur == 0,
                   "ECD stack not empty, size: %zu, depth: %zu",
                   fset_count(loc->cyan), sm->ecd.level_cur);

    size_t size[2] = { visited, dfs_stack_size(sm->out_stack) };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return loc->iteration == 0 ? size[1] : size[0];
}

/**
 * We could also avoid this procedure by grabbing states as in the reachability
 * phase, but it may be the case that the entire candidate set is already set
 * to zero, in that case we avoid exploration
 */
static size_t
owcty_elimination_pre (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    state_data_t            data;
    while ((data = dfs_stack_pop (sm->out_stack)) && !run_is_stopped(ctx->run)) {
        state_info_deserialize (ctx->state, data);
        if (0 == owcty_pre_count(ctx->run, ctx->state->ref) ) {
            dfs_stack_push (sm->stack, data); // to eliminate
        } else {
            dfs_stack_push (sm->in_stack, data); // to reachability (maybe eliminated)
        }
    }

    size_t size[2] = { dfs_stack_size(sm->stack), dfs_stack_size(sm->in_stack) };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return size[0];
}

static void
owcty_elimination_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                          int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_global_t       *sm = ctx->global;
    size_t num = owcty_pre_inc (ctx->run, successor->ref, -1);
    HREassert (num < 1ULL<<28); // overflow
    if (0 == num) {
        raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    ctx->counters->trans++;
    (void) ti; (void) seen;
}

/**
 * returns explored - initial
 */
size_t
owcty_elimination (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    size_t before = ctx->counters->explored + dfs_stack_size(sm->stack);

    raw_data_t          state_data;
    while (lb_balance(ctx->run->shared->lb, ctx->id, dfs_stack_size(sm->stack), owcty_split)) {
        state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            dfs_stack_enter (sm->stack);
            increase_level (ctx->counters);
            state_info_deserialize (ctx->state, state_data);
            permute_trans (ctx->permute, ctx->state, owcty_elimination_handle, ctx);
            ctx->counters->explored++;
            run_maybe_report1 (ctx->run, ctx->counters, "");
        } else {
            if (0 == dfs_stack_nframes (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            ctx->counters->level_cur--;
            dfs_stack_pop (sm->stack);
        }
    }

    size_t size[2] = { ctx->counters->explored, before };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return size[0] - size[1];
}

static void
owcty_do (wctx_t *ctx, size_t *size, size_t (*phase)(wctx_t *ctx), char *name,
          bool reinit_explore)
{
    alg_local_t        *loc = ctx->local;
    if (!owcty_do_reset && phase == owcty_reset) return;
    if (reinit_explore) {
        loc->iteration++;
        lb_reinit (ctx->run->shared->lb, ctx->id); // barrier
        if (owcty_ecd_all) {
            permute_free (ctx->permute); // reinitialize random exploration order:
            ctx->permute = permute_create (permutation, ctx->model,
                                     alg_state_new_default, ctx->id, ctx->run);
        }
        //ctx->run->threshold = init_threshold;
    }
    if (0 == ctx->id) RTrestartTimer (loc->timer);

    size_t              new_size = phase (ctx);
    *size = phase == owcty_elimination || phase == owcty_elimination_pre ?
                    *size - new_size : new_size;
    if (0 != ctx->id || run_is_stopped(ctx->run)) return;
    RTstopTimer (loc->timer);
    Warning (info, "candidates after %s(%zd):\t%12zu (%4.2f sec)", name,
             (loc->iteration + 1) / 2, *size, RTrealTime(loc->timer));
}

static void
owcty (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    alg_shared_t       *shared = ctx->run->shared;

    if (0 == ctx->id) {
        transition_info_t       ti = GB_NO_TRANSITION;
        owcty_initialize_handle (ctx, ctx->initial, &ti, 0);
        ctx->counters->trans = 0; //reset trans count
    }

    HREbarrier (HREglobal());

    if (strategy[1] == Strat_MAP && 0 == ctx->id &&
            pins_state_is_accepting(ctx->model, state_info_state(ctx->initial)))
        atomic_write (shared->parent_ref+ctx->initial->ref, ctx->initial->ref + 1);
    owcty_pre_t             reset = { .bit = 0, .acc = 0, .count = 1 };
    uint32_t               *r32 = (uint32_t *) &reset;
    HREassert (1 == *r32); fetch_add (r32, -1); HREassert (0 == reset.count);
    loc->timer = RTcreateTimer();

    // collect first reachable accepting states
    size_t              size, old_size = 0;
    loc->iteration = loc->flip = 0;
    owcty_do (ctx, &size, owcty_reachability,       "initialization", false);
    swap (sm->in_stack, sm->out_stack);
    if (!owcty_ecd_all) strategy[1] = Strat_None;

    while (size != 0 && old_size != size && !run_is_stopped(ctx->run)) {
        loc->flip = 1 - loc->flip;
        owcty_do (ctx, &size, owcty_reset,          "reset\t",        false);
        owcty_do (ctx, &size, owcty_reachability,   "reachability",   true);
        old_size = size;
        owcty_do (ctx, &size, owcty_elimination_pre,"pre_elimination",false);
        if (size == 0 || size == old_size) break; // early exit
        owcty_do (ctx, &size, owcty_elimination,    "elimination",    true);
    }

    if (0 == ctx->id && !run_is_stopped(ctx->run) && (size == 0 || old_size == size)) {
        Warning (info, " ");
        Warning (info, "Accepting cycle %s after %zu iteration(s)!",
                 (size > 0 ? "FOUND" : "NOT found"), (loc->iteration + 1) / 2);
    }
    HREbarrier(HREglobal()); // print result before (local) statistics
    (void) run;
}

struct alg_reduced_s {
    work_counter_t          counters;
};

void
owcty_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (alg_reduced_t));
    }
    alg_reduced_t          *reduced = run->reduced;
    work_counter_t         *ecd = &ctx->global->ecd;

    work_add_results (&reduced->counters, ecd);

    // publish local memory statistics for run class
    run->total.local_states += ecd->level_max;

    if (W >= 4 || !log_active(infoLong)) return;

    // print some local info
    work_counter_t         *cnt = ctx->counters;
    float                   runtime = RTrealTime(ctx->timer);
    Warning (info, "saw in %.3f sec %zu levels %zu states %zu transitions",
             runtime, cnt->level_max, cnt->explored, cnt->trans);

    if (Strat_ECD & strategy[1]) {
        fset_print_statistics (ctx->local->cyan, "ECD set");
    }
}

void
owcty_print_stats   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    run_report_total (run);

    //TODO: detailed OWCTY stats

    if (loc->cyan != NULL) {
        fset_print_statistics (loc->cyan, "ECD set");
    }
    (void) run;
}

void
owcty_local_init   (run_t *run, wctx_t *ctx)
{
    if (PINS_POR) Abort ("OWCTY with POR not implemented.");

    alg_local_t        *loc = RTmallocZero (sizeof(alg_local_t));
    ctx->local = loc;
    if (ecd && (strategy[1] & Strat_ECD)) {
        loc->cyan = fset_create (sizeof(ref_t), sizeof(uint32_t), 10, 20);
    }
    (void) run;
}

void
owcty_global_init   (run_t *run, wctx_t *ctx)
{
    ctx->global = RTmallocZero (sizeof(alg_global_t));
    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->global->stack = dfs_stack_create (len);
    ctx->global->in_stack = dfs_stack_create (len);
    ctx->global->out_stack = dfs_stack_create (len);

    lb_local_init (run->shared->lb, ctx->id, ctx); // Barrier
    (void) run;
}

void
owcty_destroy   (run_t *run, wctx_t *ctx)
{
    dfs_stack_destroy (ctx->global->stack);
    dfs_stack_destroy (ctx->global->in_stack);
    dfs_stack_destroy (ctx->global->out_stack);
    RTfree (ctx->global);
    (void) run;
}

void
owcty_destroy_local   (run_t *run, wctx_t *ctx)
{
    if (ctx->local->cyan != NULL) {
        fset_free (ctx->local->cyan);
    }
    RTfree (ctx->local);
    (void) run;
}

static int
owcty_stop (run_t *run)
{
    return lb_stop (run->shared->lb);
}

static int
owcty_is_stopped (run_t *run)
{
    return lb_is_stopped (run->shared->lb);
}

void
owcty_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, owcty_local_init);
    set_alg_global_init (run->alg, owcty_global_init);
    set_alg_global_deinit (run->alg, owcty_destroy);
    set_alg_local_deinit (run->alg, owcty_destroy_local);
    set_alg_print_stats (run->alg, owcty_print_stats);
    set_alg_run (run->alg, owcty);
    set_alg_reduce (run->alg, owcty_reduce);

    run->shared = RTmallocZero (sizeof(alg_shared_t));
    run->shared->pre = RTalignZero (CACHE_LINE_SIZE,
                                    sizeof(owcty_pre_t[1UL << dbs_size]));
    run->shared->lb = lb_create_max (W, G, H);
    run_set_is_stopped (run, owcty_is_stopped);
    run_set_stop (run, owcty_stop);

    if (strategy[1] == Strat_MAP) {
        run->shared->parent_ref = RTmalloc (sizeof(ref_t[1UL<<dbs_size]));
    }
}
