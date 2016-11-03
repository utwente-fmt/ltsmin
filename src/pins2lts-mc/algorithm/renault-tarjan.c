/**
 *
 */

#include <hre/config.h>

#include <mc-lib/renault-unionfind.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins2lts-mc/algorithm/renault-tarjan.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>

#ifdef HAVE_PROFILER
#include <gperftools/profiler.h>
#endif


/**
 * additional information to be stored in the stack entries
 */
typedef struct tarjan_state_s {
    uint32_t            index;
    uint32_t            lowlink;
    uint32_t            acc_set;
} tarjan_state_t;


/**
 * local counters
 */
typedef struct counter_s {
    uint32_t            unique_states;
    uint32_t            unique_trans;
    uint32_t            scc_count;
    uint32_t            tarjan_counter;       // monotonically increasing
                                              //   counter for index
} counter_t;


/**
 * local SCC information (for each worker)
 */
struct alg_local_s {
    dfs_stack_t         search_stack;
    dfs_stack_t         tarjan_stack;
    fset_t             *visited_states;       // tracks visited LIVE states
                                              // and what their stack addresses
                                              // are for either search_stack
                                              // or tarjan_stack
    counter_t           cnt;
    tarjan_state_t      state_tarjan;         // tarjan info for ctx->state
    state_info_t       *target;               // auxiliary state
    tarjan_state_t      target_tarjan;        // tarjan info for target
};


/**
 * shared SCC information (between workers)
 */
typedef struct uf_alg_shared_s {
    r_uf_t             *uf;                   // shared union-find structure
    bool                ltl;                  // LTL property present?
} r_uf_alg_shared_t;


void
renault_global_init (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
renault_global_deinit (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
renault_local_init (run_t *run, wctx_t *ctx)
{
    ctx->local         = RTmallocZero (sizeof (alg_local_t));
    r_uf_alg_shared_t    *shared = (r_uf_alg_shared_t*) ctx->run->shared;
    ctx->local->target = state_info_create ();

    // extend state_info with tarjan_state information
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_tarjan.index);
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_tarjan.lowlink);
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_tarjan.acc_set);

    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_tarjan.index);
    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_tarjan.lowlink);
    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_tarjan.acc_set);

    size_t len               = state_info_serialize_int_size (ctx->state);
    ctx->local->search_stack = dfs_stack_create (len);
    ctx->local->tarjan_stack = dfs_stack_create (len);

    ctx->local->cnt.scc_count       = 0;
    ctx->local->cnt.tarjan_counter  = 0;
    ctx->local->cnt.unique_states   = 0;
    ctx->local->cnt.unique_trans    = 0;
    ctx->local->state_tarjan.acc_set  = 0;
    ctx->local->target_tarjan.acc_set = 0;

    shared->ltl = pins_get_accepting_state_label_index(ctx->model) != -1 ||
                  pins_get_accepting_set_edge_label_index(ctx->model) - 1;

    ctx->local->visited_states =
            fset_create (sizeof (ref_t), sizeof (raw_data_t), 10, dbs_size);

    (void) run; 
}


void
renault_local_deinit (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    dfs_stack_destroy (loc->search_stack);
    dfs_stack_destroy (loc->tarjan_stack);
    fset_free (loc->visited_states);
    RTfree (loc);
    (void) run;
}


static void
renault_handle (void *arg, state_info_t *successor, transition_info_t *ti,
        int seen)
{
    // parent state is ctx->state

    wctx_t             *ctx    = (wctx_t *) arg;
    alg_local_t        *loc    = ctx->local;
    r_uf_alg_shared_t  *shared = (r_uf_alg_shared_t*) ctx->run->shared;
    raw_data_t         *addr;
    hash32_t            hash;
    int                 found;
    uint32_t            acc_set   = 0;

    // TGBA acceptance
    if (ti->labels != NULL && PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        acc_set = ti->labels[pins_get_accepting_set_edge_label_index(ctx->model)];
    }

    ctx->counters->trans++;

    // self-loop
    if (ctx->state->ref == successor->ref) {
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
            uint32_t acc = r_uf_add_acc (shared->uf, successor->ref, acc_set);
            if (GBgetAcceptingSet() == acc) {
                ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, successor);
            }
        } if (shared->ltl && pins_state_is_accepting(ctx->model, state_info_state(successor)) ) {
            // TODO: this cycle report won't work correctly
            ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, successor);
        }
        return;
    }

    // completed SCC
    if (r_uf_is_dead (shared->uf, successor->ref))
        return;

    hash  = ref_hash (successor->ref);
    found = fset_find (loc->visited_states, &hash, &successor->ref,
                       (void **)&addr, false);
    HREassert (found != FSET_FULL);

    if (found) {
        // previously visited state ==> update lowlink and unite state
        r_uf_union (shared->uf, ctx->state->ref, successor->ref);

        // TODO: this cycle report won't work correctly
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
            uint32_t acc = r_uf_add_acc (shared->uf, successor->ref, acc_set);
            if (GBgetAcceptingSet() == acc) {
                ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, successor);
            }
        } 
        if (shared->ltl && pins_state_is_accepting(ctx->model, state_info_state(ctx->state)))
            ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, ctx->state);
        if (shared->ltl && pins_state_is_accepting(ctx->model, state_info_state(successor)))
            ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, successor);

        state_info_deserialize (loc->target, *addr);
        if (loc->state_tarjan.lowlink > loc->target_tarjan.lowlink)
            loc->state_tarjan.lowlink = loc->target_tarjan.lowlink;

        // add acceptance set to the state
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA)
            loc->target_tarjan.acc_set |= acc_set;

    } else {
        // unseen state ==> push to search_stack
        raw_data_t stack_loc = dfs_stack_push (loc->search_stack, NULL);
        state_info_serialize (successor, stack_loc);

        // add acceptance set to the state
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
            state_info_deserialize (loc->target, stack_loc); // search_stack TOP
            loc->target_tarjan.acc_set = acc_set;
            state_info_serialize (loc->target, stack_loc);
        }
    }


    (void) ti; (void) seen;
}


/**
 * make a stackframe on search_stack and handle the successors of ctx->state
 */
static inline int
explore_state (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    int                 trans;

    dfs_stack_enter (loc->search_stack);
    increase_level (ctx->counters);

    trans = permute_trans (ctx->permute, ctx->state, renault_handle, ctx);

    ctx->counters->explored++;
    run_maybe_report1 (ctx->run, (work_counter_t *) ctx->counters, "");

    return trans;
}


/**
 * put the initial state on the search_stack
 */
static inline void
renault_init (wctx_t *ctx)
{
    transition_info_t   ti = GB_NO_TRANSITION;

    renault_handle (ctx, ctx->initial, &ti, 0);

    // reset explored and transition count
    ctx->counters->explored = 0;
    ctx->counters->trans    = 0;
}


/**
 * update lowlink for the parent (top of previous stackframe in search_stack)
 */
static void
update_parent (wctx_t *ctx, uint32_t low_child)
{
    alg_local_t        *loc        = ctx->local;
    raw_data_t          state_data;

    // the initial state has no parent
    if (dfs_stack_nframes (loc->search_stack) == 0)
        return;

    // store the top of the previous stackframe to loc->target
    state_data = dfs_stack_peek_top (loc->search_stack, 1);
    state_info_deserialize (loc->target, state_data);

    if (loc->target_tarjan.lowlink > low_child) {
        Debug ("Updating %zu from low %d --> %d", loc->target->ref,
               loc->target_tarjan.lowlink, low_child);

        loc->target_tarjan.lowlink = low_child;
        state_info_serialize (loc->target, state_data);
    }
}


/**
 * move a search_stack state to the tarjan_stack
 */
static void
move_tarjan (wctx_t *ctx, state_info_t *state, raw_data_t state_data)
{
    alg_local_t        *loc   = ctx->local;
    raw_data_t         *addr;
    hash32_t            hash;
    int                 found;

    // add state to tarjan_stack
    raw_data_t tarjan_loc = dfs_stack_push (loc->tarjan_stack, NULL);
    state_info_serialize (state, tarjan_loc);

    // Update reference to the new stack
    hash  = ref_hash (state->ref);
    found = fset_find (loc->visited_states, &hash, &state->ref,
                       (void**) &addr, false);
    HREassert (found != FSET_FULL);
    HREassert (*addr == state_data, "Wrong addr?");
    HREassert (found, "Could not find key in set");
    *addr = tarjan_loc;
}


/**
 * move a stack state to the completed SCC set
 */
static void
move_scc (wctx_t *ctx, ref_t state)
{
    alg_local_t        *loc     = ctx->local;
    hash32_t            hash;
    int                 success;

    Debug ("Marking %zu as SCC", state);

    // remove reference to stack state
    hash    = ref_hash (state);
    success = fset_delete (loc->visited_states, &hash, &state);
    HREassert (success, "Could not remove SCC state from set");
}


/**
 * remove and mark a completed SCC (pops the states from tarjan_stack)
 */
static void
pop_scc (wctx_t *ctx, ref_t root, uint32_t root_low)
{
    alg_local_t        *loc        = ctx->local;
    r_uf_alg_shared_t  *shared     = (r_uf_alg_shared_t*) ctx->run->shared;
    raw_data_t          state_data;
    ref_t               accepting  = DUMMY_IDX;
    uint32_t            acc_set    = 0;

    Debug ("Found SCC with root %zu", root);

    // loop and remove states until tarjan_stack.top has lowlink < root_low
    //   and put the states in the same UF set
    state_data = dfs_stack_top (loc->tarjan_stack);
    while ( state_data != NULL ) {
        // check if state_data belongs to a different SCC
        state_info_deserialize (loc->target, state_data);
        if (loc->target_tarjan.lowlink < root_low) break;

        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
            // add the acceptance set from the previous root, not the current one
            // otherwise we could add the acceptance set for the edge
            // betweem two SCCs (which cannot be part of a cycle)
            r_uf_add_acc (shared->uf, loc->target->ref, acc_set);
            acc_set = loc->target_tarjan.acc_set;
        } else if (shared->ltl && pins_state_is_accepting(ctx->model, state_info_state(loc->target))) {
            accepting = loc->target->ref;
        }

        // unite the root of the SCC with the current state
        r_uf_union (shared->uf, root, loc->target->ref);

        move_scc (ctx, loc->target->ref);
        dfs_stack_pop (loc->tarjan_stack);

        state_data = dfs_stack_top (loc->tarjan_stack);
    }

    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
        acc_set = r_uf_get_acc (shared->uf, root);
        if (GBgetAcceptingSet() == acc_set) {
            state_info_set (loc->target, root, LM_NULL_LATTICE);
            ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, loc->target);
        }
    } else if (accepting != DUMMY_IDX) {
        state_info_set (loc->target, accepting, LM_NULL_LATTICE);
        ndfs_report_cycle (ctx->run, ctx->model, loc->search_stack, loc->target);
    }

    // move the root of the SCC (since it is not on tarjan_stack)
    move_scc (ctx, root);

    // mark the SCC globally dead in the UF structure
    if (r_uf_mark_dead (shared->uf, root))
        loc->cnt.scc_count++;
}


void
renault_run  (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc        = ctx->local;
    r_uf_alg_shared_t  *shared     = (r_uf_alg_shared_t*) ctx->run->shared;
    raw_data_t         *addr;
    raw_data_t          state_data;
    int                 on_stack;
    hash32_t            hash;
    char                claim;
    int                 transitions;

#ifdef HAVE_PROFILER
    if (ctx->id == 0)
        Warning (info, "Using the profiler");
    ProfilerStart ("renault.perf");
#endif

    renault_init (ctx);

    // continue until we are done exploring the graph
    while ( !run_is_stopped (run) ) {

        state_data = dfs_stack_top (loc->search_stack);

        if (state_data != NULL) {
            // there is a state on the current stackframe ==> explore it

            state_info_deserialize (ctx->state, state_data);

            // make claim:
            // - CLAIM_FIRST   (initialized)
            // - CLAIM_SUCCESS (LIVE state)
            // - CLAIM_DEAD    (DEAD state)
            claim = r_uf_make_claim (shared->uf, ctx->state->ref);

            // pop the state and continue if it is part of a completed SCC
            if (claim == CLAIM_DEAD) {
                dfs_stack_pop (loc->search_stack);
                continue;
            }

            hash     = ref_hash (ctx->state->ref);
            on_stack = fset_find (loc->visited_states, &hash,
                                  &ctx->state->ref, (void **) &addr, true);

            HREassert (on_stack != FSET_FULL);
            if (!on_stack) {
                // unseen state ==> initialize and explore

                HREassert (loc->cnt.tarjan_counter != UINT32_MAX);
                loc->cnt.tarjan_counter ++;
                loc->state_tarjan.index   = loc->cnt.tarjan_counter;
                loc->state_tarjan.lowlink = loc->cnt.tarjan_counter;
                *addr = state_data; // point visited_states data to stack

                transitions = explore_state (ctx);

                // count the number of unique states
                if (claim == CLAIM_FIRST) {
                    loc->cnt.unique_states ++;
                    loc->cnt.unique_trans  += transitions;
                }

                state_info_serialize (ctx->state, state_data);

            } else {
                // previously visited state ==> update parent
                // NB: state is on tarjan_stack

                state_info_deserialize (ctx->state, *addr);
                update_parent (ctx, loc->state_tarjan.lowlink);
                dfs_stack_pop (loc->search_stack);
            }
        } else {
            // there is no state on the current stackframe ==> backtrack

            // we are done if we backtrack from the initial state
            if (0 == dfs_stack_nframes (loc->search_stack))
                break;

            // leave the stackframe
            dfs_stack_leave (loc->search_stack);
            ctx->counters->level_cur--;

            // retrieve the parent state from search_stack (to be removed)
            state_data = dfs_stack_top (loc->search_stack);
            state_info_deserialize (ctx->state, state_data);

            Debug ("Backtracking %zu (%d, %d)", ctx->state->ref,
                   loc->state_tarjan.index, loc->state_tarjan.lowlink);

            if (loc->state_tarjan.index == loc->state_tarjan.lowlink) {
                // index == lowlink ==> root of the SCC ==> report the SCC
                pop_scc (ctx, ctx->state->ref, loc->state_tarjan.lowlink);

            } else {
                // lowlink < index ==> LIVE SCC ==> move to tarjan_stack
                loc->target_tarjan.acc_set = loc->state_tarjan.acc_set;
                move_tarjan (ctx, ctx->state, state_data);
                update_parent (ctx, loc->state_tarjan.lowlink);
            }

            dfs_stack_pop (loc->search_stack);
        }
    }

#ifdef HAVE_PROFILER
    if (ctx->id == 0)
        Warning(info, "Done profiling");
    ProfilerStop();
#endif

    if (!run_is_stopped(run) && dfs_stack_size(loc->tarjan_stack) != 0)
        Warning (info, "Tarjan stack not empty: %zu (stack %zu)",
                 dfs_stack_size(loc->tarjan_stack),
                 dfs_stack_size(loc->search_stack));
    if (!run_is_stopped(run) && fset_count(loc->visited_states) != 0)
        Warning (info, "Stack-set not empty: %zu",
                 fset_count(loc->visited_states));
}


void
renault_reduce (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t          *reduced = (counter_t *) run->reduced;
    counter_t          *cnt     = &ctx->local->cnt;

    reduced->unique_states += cnt->unique_states;
    reduced->unique_trans  += cnt->unique_trans;
    reduced->scc_count     += cnt->scc_count;
}


void
renault_print_stats (run_t *run, wctx_t *ctx)
{
    counter_t          *reduced = (counter_t *) run->reduced;

    // print SCC statistics
    Warning(info, "unique states count:        %d", reduced->unique_states);
    Warning(info, "unique transitions count:   %d", reduced->unique_trans);
    Warning(info, "total scc count:            %d", reduced->scc_count);
    Warning(info, " ");

    run_report_total (run);

    (void) ctx;
}


void
renault_shared_init (run_t *run)
{
    r_uf_alg_shared_t  *shared;

    set_alg_local_init    (run->alg, renault_local_init);
    set_alg_global_init   (run->alg, renault_global_init);
    set_alg_global_deinit (run->alg, renault_global_deinit);
    set_alg_local_deinit  (run->alg, renault_local_deinit);
    set_alg_print_stats   (run->alg, renault_print_stats);
    set_alg_run           (run->alg, renault_run);
    set_alg_reduce        (run->alg, renault_reduce);

    run->shared = RTmallocZero (sizeof (r_uf_alg_shared_t));
    shared      = (r_uf_alg_shared_t*) run->shared;
    shared->uf  = r_uf_create ();
}
