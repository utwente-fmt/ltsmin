
#include <hre/config.h>

#include <pins2lts-mc/algorithm/cndfs.h>

struct alg_global_s {
    wctx_t             *rec;
    ref_t               work;           // ENDFS work for loadbalancer
    int                 done;           // ENDFS done for loadbalancer
};

typedef enum cndfs_proviso_e {
    UNKNOWN     = 0,
    VOLATILE    = 1,
    INVOLATILE  = 2,
} cndfs_proviso_t;

typedef enum cndfs_stack_e {
    REDALL  = 0, // all red
    NOCYCLE = 1, // volatile state
    INVOL   = 2, // involatile state
} cndfs_stack_t;

typedef enum cndfs_color_e {
    CWHITE  = 0,
    CCYAN   = 1,
    CBLUE   = 2,
    CRED    = 3
} cndfs_color_t;

static int
update_color (wctx_t *ctx, ref_t ref, uint32_t color, int check)
{
    alg_local_t        *loc = ctx->local;
    uint32_t old = state_store_get_colors (ref) & 3;
    if (old >= color)
        return 0;
    int success = state_store_try_set_colors (ref, 2 + loc->rec_bits, old, color);
    if (!success && check)
        return update_color (ctx, ref, color, check); // tail-call: can happen max 3 times
    return success;
}

static inline void
set_all_red2 (wctx_t *ctx, state_info_t *state)
{
    if (update_color(ctx, state->ref, CRED, 1)) {
        ctx->local->counters.allred++;
        if ( pins_state_is_accepting(ctx->model, state_info_state(state)) )
            ctx->local->counters.accepting++; /* count accepting states */
    } else {
        ctx->local->red.allred++;
    }
}

static inline size_t
pred (wctx_t *ctx, cndfs_stack_t idx) { if (ctx->counters->level_cur == 0) return idx;
                                 return ((ctx->counters->level_cur - 1) << 2) | idx; }
static inline size_t
cur (wctx_t *ctx, cndfs_stack_t idx) { return (ctx->counters->level_cur << 2) | idx; }

extern void rec_ndfs_call (wctx_t *ctx, ref_t state);

static void
endfs_lb (wctx_t *ctx)
{
    alg_global_t           *sm = ctx->global;
    atomic_write (&sm->done, 1);
    size_t workers[W];
    int idle_count = W-1;
    for (size_t i = 0; i<((size_t)W); i++)
        workers[i] = (i==ctx->id ? 0 : 1);
    while (0 != idle_count) {
        for (size_t i = 0; i < W; i++) {
            if (0==workers[i])
                continue;
            alg_global_t           *remote = ctx->run->contexts[i]->global;
            if (1 == atomic_read(&remote->done)) {
                workers[i] = 0;
                idle_count--;
                continue;
            }
            ref_t work = atomic_read (&remote->work);
            if (SIZE_MAX == work)
                continue;
            rec_ndfs_call (ctx, work);
        }
    }
}

static void
endfs_handle_dangerous (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    while ( dfs_stack_size(cloc->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (cloc->in_stack);
        state_info_deserialize (ctx->state, state_data);
        if ( !state_store_has_color(ctx->state->ref, GDANGEROUS, loc->rec_bits) &&
              ctx->state->ref != loc->seed->ref )
            if (update_color(ctx, ctx->state->ref, CRED, 1))
                loc->red_work.explored++;
    }
    if (update_color(ctx, ctx->state->ref, CRED, 1)) {
        loc->red_work.explored++;
        loc->counters.accepting++;
    }
    if ( state_store_has_color(loc->seed->ref, GDANGEROUS, loc->rec_bits) ) {
        rec_ndfs_call (ctx, loc->seed->ref);
    }
}

static void
cndfs_handle_nonseed_accepting (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    size_t nonred, accs;
    nonred = accs = dfs_stack_size(cloc->out_stack);

    if (nonred) {
        loc->counters.waits++;
        cloc->counters.rec += accs;
        RTstartTimer (cloc->timer);
        while ( nonred && !run_is_stopped(ctx->run) ) {
            nonred = 0;
            for (size_t i = 0; i < accs; i++) {
                raw_data_t state_data = dfs_stack_peek (cloc->out_stack, i);
                state_info_deserialize (ctx->state, state_data);
                if (state_store_get_colors (ctx->state->ref) != CRED)
                    nonred++;
            }
        }
        RTstopTimer (cloc->timer);
    }
    for (size_t i = 0; i < accs; i++)
        dfs_stack_pop (cloc->out_stack);
    while ( dfs_stack_size(cloc->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (cloc->in_stack);
        state_info_deserialize (ctx->state, state_data);
        if (update_color(ctx, ctx->state->ref, CRED, 1))
            loc->red_work.explored++;
    }
}

static void
endfs_handle_red (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    int                 onstack;

    /* Find cycle back to the seed */
    HREassert (cloc->accepting_depth > 0);
    size_t             *level;
    onstack = ctx->state->ref == loc->seed->ref;
    if (!onstack) {
        onstack = fset_find (cloc->fset, NULL, &successor->ref, (void**)&level, false);
        HREassert (onstack != FSET_FULL);
    }

    if ( onstack && *level < cloc->accepting_depth )
        ndfs_report_cycle (ctx->run, ctx->model, loc->stack, successor);
    /* Mark states dangerous if necessary */
    if ( Strat_ENDFS == loc->strat &&
         pins_state_is_accepting(ctx->model, state_info_state(successor)) &&
         state_store_get_colors (successor->ref) != CRED )
        state_store_try_color(successor->ref, GDANGEROUS, loc->rec_bits);

    if ( !onstack && state_store_get_colors (successor->ref) != CRED ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }

    // check proviso
    if (PINS_POR && proviso == Proviso_CNDFS && cloc->successors == NONEC) {
        if (ti->por_proviso != 0) { // state already fully expanded
            cloc->successors = SRCINV;
        } else if (onstack) { // cycle check
            if (onstack) cloc->successors = CYCLE;
        }
        // avoid full exploration (proviso is enforced later in backtrack)
        ti->por_proviso = 1; // avoid full exploration
    }
    (void) seen;
}

static void
endfs_handle_blue (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    size_t             *level;
    int onstack = fset_find (cloc->fset, NULL, &successor->ref, (void**)&level, false);
    HREassert (onstack != FSET_FULL);

    /**
     * The following lines bear little resemblance to the algorithms in the
     * respective papers (Evangelista et al./ Laarman et al.), but we must
     * store all non-red states on the stack in order to calculate
     * all-red correctly later. Red states are also stored as optimization.
     */
    if ( ecd && onstack && *level < cloc->accepting_depth) {
        /* Found cycle in blue search */
        ndfs_report_cycle (ctx->run, ctx->model, loc->stack, successor);
    } else if ( all_red || (!onstack &&
                         state_store_get_colors (successor->ref) != CBLUE) ) {
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }

    // check proviso
    if (PINS_POR && proviso == Proviso_CNDFS && cloc->successors == NONEC) {
        if (ti->por_proviso != 0) { // state already fully expanded
            cloc->successors = SRCINV;
        } else if (onstack) { // check cycle
            cloc->successors = CYCLE;
        }
        // avoid full exploration (proviso is enforced later in backtrack)
        ti->por_proviso = 1;
    }
    (void) seen;
}

static inline void
set_proviso_stack (wctx_t* ctx, alg_local_t* loc, cndfs_alg_local_t* cloc)
{
    switch (cloc->successors) {
    case NONEC: bitvector_set (&loc->stackbits, pred(ctx, NOCYCLE)); break;
    case CYCLE: bitvector_unset (&loc->stackbits, pred(ctx, NOCYCLE)); break;
    case SRCINV: bitvector_set (&loc->stackbits, pred(ctx, INVOL)); break;
    default: HREassert (false);
    }
}

static inline void
endfs_explore_state_red (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    work_counter_t     *cnt = &loc->red_work;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    cloc->successors = NONEC;

    dfs_stack_enter (loc->stack);
    increase_level (ctx->counters);
    cnt->trans += permute_trans (ctx->permute, ctx->state, endfs_handle_red, ctx);
    cnt->explored++;
    run_maybe_report (ctx->run, cnt, "[Red ] ");

    set_proviso_stack (ctx, loc, cloc);
}

static inline void
endfs_explore_state_blue (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    work_counter_t     *cnt = ctx->counters;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    cloc->successors = NONEC;

    dfs_stack_enter (loc->stack);
    increase_level (ctx->counters);
    cnt->trans += permute_trans (ctx->permute, ctx->state, endfs_handle_blue, ctx);
    cnt->explored++;
    run_maybe_report1 (ctx->run, cnt, "[Blue] ");

    set_proviso_stack (ctx, loc, cloc);
}

static void
endfs_handle_all (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;
    raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
    state_info_serialize (successor, stack_loc);
    ti->por_proviso = 0;
    (void) seen;
}

void
reach_explore_all (wctx_t *ctx, state_info_t *state)
{
    alg_local_t        *loc = ctx->local;
    
    permute_set_por (ctx->permute, 0);

    dfs_stack_enter (loc->stack);
    increase_level (ctx->counters);
    permute_trans (ctx->permute, state, endfs_handle_all, ctx);

    permute_set_por (ctx->permute, 1);
}

static bool
check_cndfs_proviso (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    if (PINS_POR == 0 || proviso != Proviso_CNDFS) return false;

    // Only check proviso if the state is volatile. It may be that:
    // - the reduced successor set is already involatile, or
    // - the proviso was checked before for this state, i.e. it is backtracked for the second time
    // Both imply that locally this thread visited all involatile successors
    if (bitvector_is_set(&loc->stackbits, cur(ctx,INVOL))) return false;

    bool no_cycle = bitvector_is_set (&loc->stackbits, cur(ctx,NOCYCLE));
    cndfs_proviso_t prov = no_cycle ? VOLATILE : INVOLATILE;

    int success = state_store_try_set_counters (ctx->state->ref, 2, UNKNOWN, prov);
    if (( success && prov == INVOLATILE) ||
        (!success && state_store_get_wip(ctx->state->ref) == INVOLATILE)) {
        bitvector_set (&loc->stackbits, cur(ctx,INVOL));
        return true;
    }
    return false;
}

static inline void
accepting_down (wctx_t* ctx, state_info_t *state, int accepting)
{
    alg_local_t            *loc = ctx->local;
    cndfs_alg_local_t      *cloc = (cndfs_alg_local_t *) ctx->local;
    size_t                 *depth = NULL;
    int success = fset_delete_get_data (cloc->fset, NULL, &state->ref, (void**)&depth);
    Debug ("Delled state %zu %s with depth %zu.\t\tCurrent accepting depth: %zu",
           state->ref, (accepting ? "(accepting)" : ""), *depth, cloc->accepting_depth);
    HREassert (success, "Not cyan: %zu??", loc->seed->ref);
    HREassert (accepting == (*depth != cloc->accepting_depth));
    HREassert (!accepting || *depth == cloc->accepting_depth - 1,
               "Wrong level: %zu, depth=%zu, accepting depth=%zu",
               loc->seed->ref, *depth, cloc->accepting_depth);
    cloc->accepting_depth -= accepting;
}

static inline int
on_stack_accepting_up (wctx_t *ctx, int *accepting)
{
    cndfs_alg_local_t      *cloc = (cndfs_alg_local_t *) ctx->local;
    size_t                 *depth;
    bool                    on_stack;
    on_stack = fset_find (cloc->fset, NULL, &ctx->state->ref, (void**)&depth, true);
    HREassert (on_stack != FSET_FULL);
    if (!on_stack) {
        *accepting = pins_state_is_accepting(ctx->model, state_info_state(ctx->state)) != 0;
        Debug ("Added state %zu %s with depth %zu accepting depth",
               ctx->state->ref, (*accepting ? "(accepting)" : ""), cloc->accepting_depth);
        *depth = cloc->accepting_depth; // write currect accepting depth
        cloc->accepting_depth += *accepting;
    }
    return on_stack;
}

/* ENDFS dfs_red */
static void
endfs_red (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    size_t              seed_level = dfs_stack_nframes (loc->stack);
    int                 accepting = 0;
    int                 on_stack;
    size_t              count = fset_count(cloc->fset);

    size_t               *level;
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);

            // seed is only state on both cyan and pink stack
            on_stack = ctx->state->ref == loc->seed->ref;
            if (!on_stack) {
                on_stack = fset_find (cloc->fset, NULL, &ctx->state->ref, (void**)&level, false);
                HREassert (on_stack != FSET_FULL);
            }

            if (!on_stack && state_store_get_colors(ctx->state->ref) != CRED) {
                on_stack_accepting_up (ctx, &accepting); //add to stack

                bitvector_unset (&loc->stackbits, cur(ctx,INVOL));
                dfs_stack_push (cloc->in_stack, state_data);
                if ( Strat_CNDFS == loc->strat && ctx->state->ref != loc->seed->ref && accepting)
                    dfs_stack_push (cloc->out_stack, state_data);
                endfs_explore_state_red (ctx);
            } else {
                if (seed_level == dfs_stack_nframes (loc->stack))
                    break;
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes(loc->stack))
                break;

            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (ctx->state, state_data);

            if (check_cndfs_proviso(ctx)) {
                reach_explore_all (ctx, ctx->state);
                continue;
            }

            accepting = pins_state_is_accepting (ctx->model, state_info_state(ctx->state)) != 0;
            accepting_down (ctx, ctx->state, accepting);

            dfs_stack_pop (loc->stack);
        }
    }
    if (!run_is_stopped(ctx->run)) {
        HREassert (fset_count(cloc->fset) == count);
    }
}

void // just for checking correctness of all-red implementation. Unused.
check (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    HREassert (state_store_get_colors (successor->ref) != CRED);
    (void) ti; (void) seen; (void) arg;
}

/* ENDFS dfs_blue */
void
endfs_blue (run_t *run, wctx_t *ctx)
{
    HREassert (ecd, "CNDFS's correctness depends crucially on ECD");
    alg_local_t            *loc = ctx->local;
    cndfs_alg_local_t      *cloc = (cndfs_alg_local_t *) ctx->local;
    transition_info_t       ti = GB_NO_TRANSITION;
    uint32_t                global_color;
    int                     accepting;

    cloc->successors = NONEC;
    endfs_handle_blue (ctx, ctx->initial, &ti, 0);
    ctx->counters->trans = 0; //reset trans count
    cloc->accepting_depth = 0;

    alg_global_t           *sm = ctx->global;
    while ( !run_is_stopped(ctx->run) ) {
        raw_data_t          state_data = dfs_stack_top (loc->stack);
        if (NULL != state_data) {
            state_info_deserialize (ctx->state, state_data);

            global_color = state_store_get_colors (ctx->state->ref);
            if (global_color < CBLUE && !on_stack_accepting_up(ctx, &accepting)) {
                if (global_color == CWHITE)
                    update_color (ctx, ctx->state->ref, CCYAN, 0);
                if (all_red)
                    bitvector_set (&loc->stackbits, cur(ctx,REDALL));
                bitvector_unset (&loc->stackbits, cur(ctx,INVOL));
                endfs_explore_state_blue (ctx);
            } else {
                if ( all_red && ctx->counters->level_cur != 0 && global_color != CRED )
                    bitvector_unset (&loc->stackbits, pred(ctx,REDALL));
                dfs_stack_pop (loc->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes(loc->stack))
                break;
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;
            /* call red DFS for accepting states */
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (loc->seed, state_data);

            if (check_cndfs_proviso(ctx)) {
                reach_explore_all (ctx, loc->seed);
                continue;
            }

            accepting = pins_state_is_accepting(ctx->model, state_info_state(loc->seed)) != 0;

            /* Mark state GGREEN on backtrack */
            update_color (ctx, loc->seed->ref, CBLUE, 1);
            if ( all_red && bitvector_is_set(&loc->stackbits, cur(ctx,REDALL)) ) {
                /* all successors are red */
                //permute_trans (loc->permute, ctx->state, check, ctx);
                set_all_red2 (ctx, loc->seed);
            } else if ( accepting ) {
                sm->work = loc->seed->ref;
                endfs_red (ctx);
                if (Strat_ENDFS == loc->strat)
                    endfs_handle_dangerous (ctx);
                else
                    cndfs_handle_nonseed_accepting (ctx);
                sm->work = SIZE_MAX;
            } else if (all_red && ctx->counters->level_cur > 0 &&
                        state_store_get_colors (loc->seed->ref) != CRED) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&loc->stackbits, pred(ctx,REDALL));
            }

            accepting_down (ctx, loc->seed, accepting);

            dfs_stack_pop (loc->stack);
        }
    }

    HREassert (run_is_stopped(ctx->run) || fset_count(cloc->fset) == 0);

    // if the recursive strategy uses global bits (global pruning)
    // then do simple load balancing (only for the top-level strategy)
    if ( Strat_ENDFS == loc->strat &&
         run == run->shared->top_level &&
         (Strat_LTLG & sm->rec->local->strat) ) {
        endfs_lb (ctx);
    }
}

void
rec_ndfs_call (wctx_t *ctx, ref_t state)
{
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    alg_global_t       *sm = ctx->global;
    strategy_t          rec_strat = get_strategy (ctx->run->shared->rec->alg);
    dfs_stack_push (sm->rec->local->stack, (int*)&state);
    cloc->counters.rec++;
    switch (rec_strat) {
    case Strat_ENDFS:
       endfs_blue (sm->rec->run, sm->rec); break;
    case Strat_LNDFS:
       lndfs_blue (sm->rec->run, sm->rec); break;
    case Strat_NDFS:
       ndfs_blue (sm->rec->run, sm->rec); break;
    default:
       Abort ("Invalid recursive strategy.");
    }
}


void
cndfs_global_init   (run_t *run, wctx_t *ctx)
{
    ctx->global = RTmallocZero (sizeof(alg_global_t));
    ctx->global->work = SIZE_MAX;
    (void) run;
}

void
cndfs_global_deinit   (run_t *run, wctx_t *ctx)
{
    if (run->shared->rec != NULL) {
        alg_global_deinit (run->shared->rec, ctx->global->rec);
        wctx_destroy (ctx->global->rec);
    }
    RTfree (ctx->global);
}

void
cndfs_local_setup   (run_t *run, wctx_t *ctx)
{
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    cloc->timer = RTcreateTimer ();
    ndfs_local_setup (run, ctx);
    size_t len = state_info_serialize_int_size (ctx->state);
    cloc->in_stack = dfs_stack_create (len);
    cloc->out_stack = dfs_stack_create (len);

    if (get_strategy(run->alg) & Strat_CNDFS) return;

    if (run->shared->rec == NULL) {
        Abort ("Missing recursive strategy for %s!",
               key_search(strategies, get_strategy(run->alg)));
        return;
    }

    HREassert (ctx->global != NULL, "Run global before local init");

    // We also need to finalize the worker initialization:
    ctx->global->rec = run_init (run->shared->rec, ctx->model);

    // Recursive strategy maybe unaware of its caller, so here we update its
    // recursive bits (top-level strategy always has rec_bits == 0, which
    // is ensured by ndfs_local_setup):
    ctx->global->rec->local->rec_bits = run->shared->color_bit_shift;
    cloc->rec = ctx->global->rec->local;
}

void
cndfs_local_init   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = RTmallocZero (sizeof(cndfs_alg_local_t));
    ctx->local = loc;

    cndfs_local_setup (run, ctx);

    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;
    cloc->fset = fset_create (sizeof(ref_t), sizeof(size_t), 4, 24);
}

void
cndfs_local_deinit   (run_t *run, wctx_t *ctx)
{
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    if (run->shared->rec != NULL) {
        alg_local_deinit (run->shared->rec, ctx->global->rec);
        wctx_deinit (ctx->global->rec); // see cndfs_local_init
    }

    dfs_stack_destroy (cloc->in_stack);
    dfs_stack_destroy (cloc->out_stack);
    RTdeleteTimer (cloc->timer);
    ndfs_local_deinit (run, ctx);
}

void
cndfs_print_stats   (run_t *run, wctx_t *ctx)
{
    size_t              db_elts = global->stats.elts;
    size_t              accepting = run->reduced->blue.accepting;

    run_report_total (run);

    Warning (info, " ");
    Warning (info, "State space has %zu states, %zu are accepting", db_elts, accepting);

    wctx_t             *cur = ctx;
    int                 index = 1;
    while (cur != NULL) {
        cndfs_reduced_t        *creduced = (cndfs_reduced_t *) cur->run->reduced;
        alg_reduced_t          *reduced = cur->run->reduced;

        ctx->counters->explored /= W;
        ctx->counters->trans /= W;
        reduced->red_work.explored /= W;
        reduced->red_work.trans /= W;
        reduced->red.bogus_red /= W;
        ndfs_print_state_stats (cur->run, cur, index, creduced->waittime);

        if (cur->local->strat & Strat_ENDFS) {
            Warning (infoLong, " ");
            Warning (infoLong, "ENDFS recursive calls:");
            Warning (infoLong, "Calls: %zu",  creduced->rec);
            Warning (infoLong, "Waits: %zu",  reduced->blue.waits);
        }

        cur = cur->global != NULL ? cur->global->rec : NULL;
        index++;
    }

    cndfs_reduced_t        *cred = (cndfs_reduced_t *) run->reduced;
    double mem3 = ((double)((cred->max_load * sizeof(ref_t[2])))) / (1UL<<20);
    Warning (info, " ");
    Warning (info, "Total memory used for local state coloring: %.1fMB", mem3);
}

void
cndfs_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (cndfs_reduced_t));
        cndfs_reduced_t        *reduced = (cndfs_reduced_t *) run->reduced;
        reduced->waittime = 0;
    }
    cndfs_reduced_t        *reduced = (cndfs_reduced_t *) run->reduced;
    cndfs_alg_local_t      *cloc = (cndfs_alg_local_t *) ctx->local;
    float                   waittime = RTrealTime(cloc->timer);
    reduced->waittime   += waittime;
    reduced->rec        += cloc->counters.rec;
    reduced->max_load   += fset_max_load (cloc->fset);

    ndfs_reduce (run, ctx);

    if (log_active(infoLong)) {
        fset_print_statistics (cloc->fset, "Stack set: ");
    }

    if (run->shared->rec != NULL) {
        alg_global_t           *sm = ctx->global;
        alg_reduce (run->shared->rec, sm->rec);
    }
}

int
cndfs_state_seen (void *ptr, transition_info_t *ti, ref_t ref, int seen)
{
    wctx_t             *ctx = (wctx_t *) ptr;
    cndfs_alg_local_t  *cloc = (cndfs_alg_local_t *) ctx->local;

    void               *level;
    if (!seen) {
        seen = fset_find (cloc->fset, NULL, &ref, &level, false);
        HREassert (seen != FSET_FULL);
    }
    if (seen) return 1;

    uint32_t old = state_store_get_colors (ref) & 3;

    return -(old == CCYAN);
    (void) ti;
}

static int
cndfs_stop (run_t *run)
{
    run_t* top_run = run->shared->top_level;
    return top_run->shared->run_stop (top_run);
}

static int
cndfs_is_stopped (run_t *run)
{
    run_t* top_run = run->shared->top_level;
    return top_run->shared->run_is_stopped (top_run);
}

void
cndfs_shared_init   (run_t *run)
{
    HREassert (GRED.g == 0);
    HREassert (GGREEN.g == 1);
    HREassert (GDANGEROUS.g == 2);

    set_alg_local_init      (run->alg, cndfs_local_init);
    set_alg_global_init     (run->alg, cndfs_global_init);
    set_alg_global_deinit   (run->alg, cndfs_global_deinit);
    set_alg_local_deinit    (run->alg, cndfs_local_deinit);
    set_alg_print_stats     (run->alg, cndfs_print_stats);
    set_alg_run             (run->alg, endfs_blue);
    set_alg_state_seen      (run->alg, cndfs_state_seen);
    set_alg_reduce          (run->alg, cndfs_reduce);

    if (run->shared != NULL)
        return;

    run->shared = RTmallocZero (sizeof(alg_shared_t));
    run->shared->color_bit_shift = 0;
    run->shared->top_level = run;
    run->shared->run_is_stopped = run_get_is_stopped (run);
    run->shared->run_stop = run_get_stop (run);

    run_set_is_stopped (run, cndfs_is_stopped);
    run_set_stop (run, cndfs_stop);

    int             i = 1;
    run_t          *previous = run;
    run_t          *next = NULL;
    while (strategy[i] != Strat_None) {
        next = run_create (false);
        next->shared = RTmallocZero (sizeof(alg_shared_t));
        next->shared->previous = previous;
        next->shared->top_level = run;
        next->shared->rec = NULL;
        run_set_is_stopped (next, cndfs_is_stopped);
        run_set_stop (next, cndfs_stop);
        next->shared->color_bit_shift = previous->shared->color_bit_shift +
                                        num_global_bits (strategy[i]);

        alg_shared_init_strategy (next, strategy[i]);

        previous->shared->rec = next;
        previous = next;
        i++;
    }
}
