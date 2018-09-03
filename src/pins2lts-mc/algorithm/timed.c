/**
 *
 */

#include <hre/config.h>

#include <popt.h>

#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/parallel/stream-serializer.h>

// TODO: merge with reach algorithms

int              LATTICE_BLOCK_SIZE = (1ULL<<CACHE_LINE) / sizeof(lattice_t);
int              UPDATE = TA_UPDATE_WAITING;
int              NONBLOCKING = 0;

struct poptOption timed_options[] = {
    {"lattice-blocks", 'l', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARGFLAG_DOC_HIDDEN, &LATTICE_BLOCK_SIZE, 0,
     "Size of blocks preallocated for lattices (> 1). "
     "Small blocks save memory when most states few lattices (< 4). "
     "Larger blocks save memory in case a few states have many lattices. "
     "For the best performance set this to: cache line size (usually 64) divided by lattice size of 8 byte.", NULL},
    {"update", 'u', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &UPDATE,
      0,"cover update strategy: 0 = simple, 1 = update waiting, 2 = update passed (may break traces)", NULL},
    {"non-blocking", 0, POPT_ARG_VAL, &NONBLOCKING, 1, "Non-blocking TA reachability", NULL},
    POPT_TABLEEND
};

typedef struct ta_alg_global_s {
    alg_global_t         reach;

    lm_loc_t            lloc;       // lattice location (serialized by state-info)
} ta_alg_global_t;

typedef struct ta_alg_local_s {
    alg_local_t         reach;

    int                 done;       // done flag
    int                 subsumes;   // successor subsumes a state in LMAP
    lm_loc_t            added_at;   // successor is added at location
    lm_loc_t            last;       // last tombstone location
    size_t              work;       //
    state_info_t       *successor;  // current successor state

    ta_counter_t        counters;   // statistic counters
} ta_alg_local_t;

typedef struct ta_reduced_s {
    alg_reduced_t       reach;
    ta_counter_t        counters;
} ta_reduced_t;

typedef struct ta_alg_shared_s {
    alg_shared_t        reach;
    lm_t               *lmap;
} ta_alg_shared_t;

typedef enum ta_set_e {
    TA_WAITING = 0,
    TA_PASSED  = 1,
} ta_set_e_t;

lm_cb_t
ta_covered (void *arg, lattice_t l, lm_status_t status, lm_loc_t lm)
{
    wctx_t             *ctx = (wctx_t*) arg;
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    lattice_t           lattice = ta_loc->successor->lattice;
    int *succ_l = (int*)&lattice;
    if (UPDATE == TA_UPDATE_NONE
            ? lattice == l
            : !ta_loc->subsumes && GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
        ta_loc->done = 1;
        return LM_CB_STOP; //A l' : (E (s,l)eL : l>=l')=>(A (s,l)eL : l>=l')
    } else if (TA_UPDATE_NONE != UPDATE &&
            (TA_UPDATE_PASSED == UPDATE || TA_WAITING == (ta_set_e_t)status) &&
            GBisCoveredByShort(ctx->model, (int*)&l, succ_l)) {
        ta_loc->subsumes = 1;
        lm_delete (shared->lmap, lm);
        ta_loc->last = (LM_NULL_LOC == ta_loc->last ? lm : ta_loc->last);
        ta_loc->counters.deletes++;
    }
    ta_loc->work++;
    return LM_CB_NEXT;
}

lm_cb_t
ta_covered_nb (void *arg, lattice_t l, lm_status_t status, lm_loc_t lm)
{
    wctx_t             *ctx = (wctx_t*) arg;
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    lattice_t           lattice = ta_loc->successor->lattice;
    int *succ_l = (int*)&lattice;
    if (UPDATE == TA_UPDATE_NONE
            ? lattice == l
            : GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
        ta_loc->done = 1;
        return LM_CB_STOP; //A l' : (E (s,l)eL : l>=l')=>(A (s,l)eL : l>=l')
    } else if (TA_UPDATE_NONE != UPDATE &&
            (TA_UPDATE_PASSED == UPDATE || TA_WAITING == (ta_set_e_t)status) &&
            GBisCoveredByShort(ctx->model, (int*)&l, succ_l)) {
        if (LM_NULL_LOC == ta_loc->added_at) { // replace first waiting, will be added to waiting set
            if (!lm_cas_update (shared->lmap, lm, l, status, lattice, (lm_status_t)TA_WAITING)) {
                lattice_t n = lm_get (shared->lmap, lm);
                if (n == LM_NULL_LATTICE) // deleted
                    return LM_CB_NEXT;
                lm_status_t s = lm_get_status (shared->lmap, lm);
                return ta_covered_nb (arg, n, s, lm); // retry
            } else {
                ta_loc->added_at = lm;
            }
        } else {                            // delete second etc
            lm_cas_delete (shared->lmap, lm, l, status);
        }
        ta_loc->last = (LM_NULL_LOC == ta_loc->last ? lm : ta_loc->last);
        ta_loc->counters.deletes++;
    }
    ta_loc->work++;
    return LM_CB_NEXT;
}

static void
ta_queue_state_normal (wctx_t *ctx, state_info_t *successor)
{
    alg_global_t       *sm = ctx->global;
    raw_data_t stack_loc = dfs_stack_push (sm->stack, NULL);
    state_info_serialize (successor, stack_loc);
}

static void (*ta_queue_state)(wctx_t *, state_info_t *) = ta_queue_state_normal;

static void
ta_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t*) arg;
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    alg_local_t        *loc = ctx->local;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    ta_alg_global_t    *sm = (ta_alg_global_t *) ctx->global;
    ta_loc->done = 0;
    ta_loc->subsumes = 0; // if a successor subsumes one in the set, it cannot be subsumed itself (see invariant paper)
    ta_loc->work = 0;
    ta_loc->added_at = LM_NULL_LOC;
    ta_loc->last = LM_NULL_LOC;
    ta_loc->successor = successor;
    lm_lock (shared->lmap, successor->ref);
    lm_loc_t last = lm_iterate (shared->lmap, successor->ref, ta_covered, ctx);
    if (!ta_loc->done) {
        last = (LM_NULL_LOC == ta_loc->last ? last : ta_loc->last);
        sm->lloc = lm_insert_from (shared->lmap, successor->ref,
                                   successor->lattice, TA_WAITING, &last);
        lm_unlock (shared->lmap, successor->ref);
        ta_loc->counters.inserts++;
        if (0) { // quite costly: flops
            if (ta_loc->work > 0)
                statistics_unrecord (&ta_loc->counters.lattice_ratio, ta_loc->work);
            statistics_record (&ta_loc->counters.lattice_ratio, ta_loc->work+1);
        }
        ta_queue_state (ctx, successor);
        ta_loc->counters.updates += LM_NULL_LOC != ta_loc->last;
        loc->counters.level_size++;
    } else {
        lm_unlock (shared->lmap, successor->ref);
    }
    action_detect (ctx, ti, successor);
    if (EXPECT_FALSE(loc->lts != NULL)) {
        int             src = ctx->counters->explored;
        int            *tgt = state_info_state (successor);
        int             tgt_owner = ref_hash (successor->ref) % W;
        lts_write_edge (loc->lts, ctx->id, &src, tgt_owner, tgt, ti->labels);
    }
    ctx->counters->trans++;
    (void) seen;
}

static void
ta_handle_nb (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t*) arg;
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    alg_local_t        *loc = ctx->local;
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) loc;
    ta_alg_global_t    *sm = (ta_alg_global_t *) ctx->global;
    ta_loc->done = 0;
    ta_loc->subsumes = 0;
    ta_loc->work = 0;
    ta_loc->added_at = LM_NULL_LOC;
    ta_loc->last = LM_NULL_LOC;
    ta_loc->successor = successor;
    lm_loc_t last = lm_iterate (shared->lmap, successor->ref, ta_covered_nb, ctx);
    if (!ta_loc->done) {
        if (LM_NULL_LOC == ta_loc->added_at) {
            last = (LM_NULL_LOC == ta_loc->last ? last : ta_loc->last);
            sm->lloc = lm_insert_from_cas (shared->lmap, successor->ref,
                                    successor->lattice, TA_WAITING, &last);
            ta_loc->counters.inserts++;
        } else {
            sm->lloc = ta_loc->added_at;
        }
        ta_queue_state (ctx, successor);
        ta_loc->counters.updates += LM_NULL_LOC != ta_loc->last;
        loc->counters.level_size++;
    }
    ctx->counters->trans++;
    (void) ti; (void) seen;
}

static inline void
ta_explore_state (wctx_t *ctx)
{
    size_t              count = 0;
    if (ctx->counters->level_cur >= max_level)
        return;
    invariant_detect (ctx);
    count = permute_trans (ctx->permute, ctx->state,
                           NONBLOCKING ? ta_handle_nb : ta_handle, ctx);
    deadlock_detect (ctx, count);
    run_maybe_report1 (ctx->run, ctx->counters, "");
    ctx->counters->explored++;
}

static inline int
grab_waiting (wctx_t *ctx, raw_data_t state_data)
{
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    ta_alg_global_t    *sm = (ta_alg_global_t *) ctx->global;
    state_info_deserialize (ctx->state, state_data);
    if ((UPDATE == TA_UPDATE_NONE) && !NONBLOCKING)
        return 1; // we don't need to update the global waiting info
    return lm_cas_update(shared->lmap, sm->lloc, ctx->state->lattice, TA_WAITING,
                                                 ctx->state->lattice, TA_PASSED);
    // lockless! May cause newly created passed state to be deleted by a,
    // waiting set update. However, this behavior is valid since it can be
    // simulated by swapping these two operations in the schedule.
}

void
ta_dfs (wctx_t *ctx)
{
    alg_shared_t       *shared = ctx->run->shared;
    alg_global_t       *sm = ctx->global;
    while (lb_balance(shared->lb, ctx->id, dfs_stack_size(sm->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (sm->stack);
        if (NULL != state_data) {
            if (grab_waiting(ctx, state_data)) {
                dfs_stack_enter (sm->stack);
                increase_level (ctx->counters);
                ta_explore_state (ctx);
            } else {
                dfs_stack_pop (sm->stack);
            }
        } else {
            if (0 == dfs_stack_size (sm->stack))
                continue;
            dfs_stack_leave (sm->stack);
            ctx->counters->level_cur--;
            dfs_stack_pop (sm->stack);
        }
    }
}

void
ta_bfs (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    while (lb_balance(ctx->run->shared->lb, ctx->id, bfs_load(sm), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
        if (NULL != state_data) {
            if (grab_waiting(ctx, state_data)) {
                ta_explore_state (ctx);
            }
        } else {
            swap (sm->in_stack, sm->out_stack);
            increase_level (ctx->counters);
        }
    }
}

void
ta_sbfs (wctx_t *ctx)
{
    alg_global_t       *sm = ctx->global;
    size_t              next_level_size, local_next_size;
    do {
        while (lb_balance(ctx->run->shared->lb, ctx->id, in_load(sm), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (sm->in_stack);
            if (NULL != state_data) {
                if (grab_waiting(ctx, state_data)) {
                    ta_explore_state (ctx);
                }
            }
        }
        local_next_size = dfs_stack_frame_size(sm->out_stack);
        next_level_size = sbfs_level (ctx, local_next_size);
        lb_reinit (ctx->run->shared->lb, ctx->id);
        swap (sm->out_stack, sm->in_stack);
        sm->stack = sm->out_stack;
    } while (next_level_size > 0 && !run_is_stopped(ctx->run));
}

void
ta_pbfs (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    size_t              count;
    raw_data_t          state_data;
    int                 labels[SL];
    do {
        loc->counters.level_size = 0; // count states in next level
        loc->flip = 1 - loc->flip;
        for (size_t i = 0; i < W; i++) {
            size_t          current = (i << 1) + loc->flip;
            while ((state_data = isba_pop_int (sm->queues[current])) &&
                    !run_is_stopped (ctx->run)) {
                if (grab_waiting(ctx, state_data)) {
                    ta_explore_state (ctx);
                    if (EXPECT_FALSE(loc->lts != NULL)){
                        state_data_t pins_state = state_info_pins_state (ctx->state);
                        if (SL > 0)
                            GBgetStateLabelsAll (ctx->model, pins_state, labels);
                        lts_write_state (loc->lts, ctx->id, pins_state, labels);
                    }
                }
            }
        }
        count = sbfs_level (ctx, loc->counters.level_size);
    } while (count && !run_is_stopped(ctx->run));
}

void
timed_local_init   (run_t *run, wctx_t *ctx)
{
    ta_alg_local_t     *loc = RTmallocZero (sizeof(ta_alg_local_t));

    ctx->local = (alg_local_t *) loc;
    reach_local_setup (run, ctx);
    statistics_init (&loc->counters.lattice_ratio);
}

void
timed_global_init   (run_t *run, wctx_t *ctx)
{
    if (PINS_POR)
        Abort ("POR not compatible with timed automata");
    ctx->global = RTmallocZero (sizeof(ta_alg_global_t));
    ta_alg_global_t    *sm = (ta_alg_global_t *) ctx->global;

    // Extend state info with a lattice location
    state_info_add_simple (ctx->state, sizeof(lm_loc_t), &sm->lloc);
    state_info_add_simple (ctx->initial, sizeof(lm_loc_t), &sm->lloc);
    state_info_t       *si_perm = permute_state_info(ctx->permute);
    state_info_add_simple (si_perm, sizeof(lm_loc_t), &sm->lloc);

    reach_global_setup (run, ctx);
}

void
timed_destroy   (run_t *run, wctx_t *ctx)
{
    reach_destroy (run, ctx);
}

void
timed_destroy_local (run_t *run, wctx_t *ctx)
{
    reach_destroy_local (run, ctx);
}

void
ta_print_stats   (lm_t *lmap, wctx_t *ctx)
{
    ta_alg_local_t     *ta_loc = (ta_alg_local_t *) ctx->local;
    size_t              lattices = ta_loc->counters.inserts -
                                   ta_loc->counters.updates;
    size_t              db_elts = global->stats.elts;
    size_t              alloc = lm_allocated (lmap);
    double              mem3 = ((double)(sizeof(lattice_t[alloc + db_elts]))) / (1<<20);
    double lm = ((double)(sizeof(lattice_t[alloc + (1ULL<<dbs_size)]))) / (1<<20);
    double redundancy = (((double)(db_elts + alloc)) / lattices - 1) * 100;
    Warning (info, "Lattice map: %.1fMB (~%.1fMB paged-in) "
                   "overhead: %.2f%%, allocated: %zu",
                   mem3, lm, redundancy, alloc);

    Warning (infoLong, " ");
    Warning (infoLong, "Lattice map statistics:");
    Warning (infoLong, "Memory usage: %.2f", mem3);
    Warning (infoLong, "Ratio: %.2f",  ((double)lattices/db_elts));
    Warning (infoLong, "Inserts: %zu", ta_loc->counters.inserts);
    Warning (infoLong, "Updates: %zu", ta_loc->counters.updates);
    Warning (infoLong, "Deletes: %zu", ta_loc->counters.deletes);
    //TODO: detailed lmap overhead stats
}

void
timed_print_stats   (run_t *run, wctx_t *ctx)
{
    reach_print_stats (run, ctx);

    ta_alg_shared_t    *shared = (ta_alg_shared_t *) ctx->run->shared;
    ta_print_stats (shared->lmap, ctx);
}

void
ta_add_results (ta_counter_t *res, ta_counter_t *cnt)
{
    res->deletes += cnt->deletes;
    res->inserts += cnt->inserts;
    res->updates += cnt->updates;
    statistics_union (&res->lattice_ratio, &res->lattice_ratio,
                      &cnt->lattice_ratio);
}

void
timed_reduce (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (ta_reduced_t) + CACHE_LINE_SIZE);
    }
    ta_reduced_t           *reduced = (ta_reduced_t *) run->reduced;
    ta_alg_local_t         *ta_loc = (ta_alg_local_t *) ctx->local;

    ta_add_results (&reduced->counters, &ta_loc->counters);

    reach_reduce (run, ctx);
}

void
timed_run (run_t *run, wctx_t *ctx)
{
    transition_info_t       ti = GB_NO_TRANSITION;

    if ( Strat_PBFS & strategy[0] ) {
        ta_queue_state = pbfs_queue_state;
    }

    if (0 == ctx->id) { // only w1 receives load, as it is propagated later
        if ( Strat_PBFS & strategy[0] ) {
            if (ctx->local->lts != NULL) {
                state_data_t    initial = state_info_state(ctx->initial);
                int             src_owner = ref_hash(ctx->initial->ref) % W;
                lts_write_init (ctx->local->lts, src_owner, initial);
            }
        }
        ta_handle (ctx, ctx->initial, &ti, 0);
        ctx->counters->trans = 0; //reset trans count
    }

    HREbarrier (HREglobal());

    switch (get_strategy(run->alg)) {
        case Strat_TA_PBFS:
            ta_pbfs (ctx);
            break;
        case Strat_TA_SBFS:
            ta_sbfs (ctx);
            break;
        case Strat_TA_BFS:
            ta_bfs (ctx);
            break;
        case Strat_TA_DFS:
            ta_dfs (ctx);
            break;
        default: Abort ("Missing case in timed_run");
    }
}

void
timed_shared_init      (run_t *run)
{
    set_alg_local_init (run->alg, timed_local_init);
    set_alg_global_init (run->alg, timed_global_init);
    set_alg_global_deinit (run->alg, timed_destroy);
    set_alg_local_deinit (run->alg, timed_destroy_local);
    set_alg_print_stats (run->alg, timed_print_stats);
    set_alg_run (run->alg, timed_run);
    set_alg_reduce (run->alg, timed_reduce);

    run->shared = RTmallocZero (sizeof (ta_alg_shared_t));
    reach_init_shared (run);
    ta_alg_shared_t    *shared = (ta_alg_shared_t *) run->shared;
    shared->lmap = state_store_lmap (global->store);
}
