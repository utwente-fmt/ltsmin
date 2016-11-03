/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/tarjan-scc.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>

#ifdef SEARCH_COMPLETE_GRAPH
#include <mc-lib/dlopen_extra.h>
#endif

#ifdef HAVE_PROFILER
#include <gperftools/profiler.h>
#endif


/**
 * color for global state storage (SCC is complete <==> color == SCC_STATE)
 */
#define SCC_STATE GRED


/**
 * additional information to be stored in the stack entries
 */
typedef struct tarjan_state_s {
    uint32_t            index;
    uint32_t            lowlink;
} tarjan_state_t;


/**
 * local counters
 */
typedef struct counter_s {
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


void
tarjan_global_init (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
tarjan_global_deinit (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
tarjan_local_init (run_t *run, wctx_t *ctx)
{
    ctx->local         = RTmallocZero (sizeof (alg_local_t));
    ctx->local->target = state_info_create ();

    // extend state_info with tarjan_state information
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_tarjan.index);
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_tarjan.lowlink);

    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_tarjan.index);
    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_tarjan.lowlink);

    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->local->search_stack = dfs_stack_create (len);
    ctx->local->tarjan_stack = dfs_stack_create (len);

    ctx->local->cnt.scc_count       = 0;
    ctx->local->cnt.tarjan_counter  = 0;

    ctx->local->visited_states =
            fset_create (sizeof (ref_t), sizeof (raw_data_t), 10, dbs_size);

#ifdef SEARCH_COMPLETE_GRAPH
    // provide the input file name to dlopen_setup
    dlopen_setup (files[0]);
#endif

    (void) run; 
}


void
tarjan_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    dfs_stack_destroy (loc->search_stack);
    dfs_stack_destroy (loc->tarjan_stack);
    fset_free (loc->visited_states);
    RTfree (loc);
    (void) run;
}


static void
tarjan_handle (void *arg, state_info_t *successor, transition_info_t *ti,
               int seen)
{
    // parent state is ctx->state

    wctx_t             *ctx   = (wctx_t *) arg;
    alg_local_t        *loc   = ctx->local;
    raw_data_t         *addr;
    hash32_t            hash;
    int                 found;

    ctx->counters->trans++;

    // self-loop
    if (ctx->state->ref == successor->ref)
        return;

    // completed SCC
    if (state_store_has_color (successor->ref, SCC_STATE, 0))
        return;

    hash  = ref_hash (successor->ref);
    found = fset_find (loc->visited_states, &hash, &successor->ref,
                       (void **)&addr, false);
    HREassert (found != FSET_FULL);

    if (found) {
        // previously visited state ==> update lowlink

        state_info_deserialize (loc->target, *addr);
        if (loc->state_tarjan.lowlink > loc->target_tarjan.lowlink)
            loc->state_tarjan.lowlink = loc->target_tarjan.lowlink;

    } else {
        // unseen state ==> push to search_stack

        raw_data_t stack_loc = dfs_stack_push (loc->search_stack, NULL);
        state_info_serialize (successor, stack_loc);
    }

    (void) ti; (void) seen;
}


#ifdef SEARCH_COMPLETE_GRAPH
/**
 * bypasses pins to directly handle the successor
 * assumes that we only require state->ref
 */
static inline void
permute_complete (void *arg, transition_info_t *ti, state_data_t dst, int *cpy)
{
    wctx_t             *ctx = (wctx_t *) arg;
    alg_local_t        *loc = ctx->local;

    loc->target->ref = (ref_t) dst[0];
    tarjan_handle (ctx, loc->target, ti, 0);

    (void) cpy;
}
#endif


/**
 * make a stackframe on search_stack and handle the successors of ctx->state
 */
static inline void
explore_state (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    dfs_stack_enter (loc->search_stack);
    increase_level (ctx->counters);

#ifdef SEARCH_COMPLETE_GRAPH
    // bypass the pins interface by directly handling the successors
    int                 ref_arr[2];
    ref_arr[0] = (int) ctx->state->ref;
    dlopen_next_state (NULL, 0, ref_arr, permute_complete, ctx);
#else
    permute_trans (ctx->permute, ctx->state, tarjan_handle, ctx);
#endif

    ctx->counters->explored++;
    run_maybe_report1 (ctx->run, (work_counter_t *) ctx->counters, "");
}


/**
 * put the initial state on the search_stack
 */
static inline void
tarjan_init  (wctx_t *ctx)
{
    transition_info_t   ti = GB_NO_TRANSITION;

#ifdef SEARCH_COMPLETE_GRAPH
    alg_local_t        *loc = ctx->local;
    tarjan_handle (ctx, loc->target, &ti, 0);
#else
    tarjan_handle (ctx, ctx->initial, &ti, 0);

    // reset explored and transition count
    ctx->counters->explored = 0;
    ctx->counters->trans    = 0;
#endif
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

    // set SCC globally
    state_store_try_color (state, SCC_STATE, 0);
}


/**
 * remove and mark a completed SCC (pops the states from tarjan_stack)
 */
static void
pop_scc (wctx_t *ctx, ref_t root, uint32_t root_low)
{
    alg_local_t        *loc        = ctx->local;
    raw_data_t          state_data;

    Debug ("Found SCC with root %zu", root);

    loc->cnt.scc_count++;

    // loop and remove states until tarjan_stack.top has lowlink < root_low
    state_data = dfs_stack_top (loc->tarjan_stack);
    while ( state_data != NULL ) {

        // check if state_data belongs to a different SCC
        state_info_deserialize (loc->target, state_data);
        if (loc->target_tarjan.lowlink < root_low) break;

        move_scc (ctx, loc->target->ref);
        dfs_stack_pop (loc->tarjan_stack);

        state_data = dfs_stack_top (loc->tarjan_stack);
    }

    // move the root of the SCC (since it is not on tarjan_stack)
    move_scc (ctx, root);
}


void
tarjan_run (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc        = ctx->local;
    raw_data_t         *addr;
    raw_data_t          state_data;
    int                 on_stack;
    hash32_t            hash;

#ifdef HAVE_PROFILER
    Warning (info, "Using the profiler");
    ProfilerStart ("tarjan.perf");
#endif

#ifdef SEARCH_COMPLETE_GRAPH
    int              init_state = dlopen_get_worker_initial_state (ctx->id, W);
    int              inits      = 0;

    // loop until every state of the graph has been visited
    while ( 1 )
    {
        inits ++;
        // use loc->target as a dummy for the initial state
        loc->target->ref = init_state;
#endif

    tarjan_init (ctx);
    
    // continue until we are done exploring the graph
    while ( !run_is_stopped (run) ) {

        state_data = dfs_stack_top (loc->search_stack);

        if (state_data != NULL) {
            // there is a state on the current stackframe ==> explore it

            state_info_deserialize (ctx->state, state_data);

            // pop the state and continue if it is part of a completed SCC
            if (state_store_has_color (ctx->state->ref, SCC_STATE, 0)) {
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

                // point visited_states data to stack
                *addr = state_data;

                explore_state (ctx);

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
                move_tarjan (ctx, ctx->state, state_data);
                update_parent (ctx, loc->state_tarjan.lowlink);
            }

            dfs_stack_pop (loc->search_stack);
        }
    }

#ifdef SEARCH_COMPLETE_GRAPH
        init_state = dlopen_get_new_initial_state (init_state);
        if (init_state == -1) {
            Warning(info, "Number of inits : %d", inits);
            break;
        }
    }
#endif

#ifdef HAVE_PROFILER
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
tarjan_reduce (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t          *reduced = (counter_t *) run->reduced;
    counter_t          *cnt     = &ctx->local->cnt;

    reduced->scc_count += cnt->scc_count;
}


void
tarjan_print_stats (run_t *run, wctx_t *ctx)
{
    counter_t          *reduced = (counter_t *) run->reduced;

    // print SCC statistics
    Warning(info, "unique states count:        %zu", ctx->counters->explored);
    Warning(info, "unique transitions count:   %zu", ctx->counters->trans);
    Warning(info, "total scc count:            %d", reduced->scc_count);
    Warning(info, " ");

    run_report_total (run);
}


void
tarjan_shared_init (run_t *run)
{
    HREassert (SCC_STATE.g == 0);
    set_alg_local_init    (run->alg, tarjan_local_init);
    set_alg_global_init   (run->alg, tarjan_global_init);
    set_alg_global_deinit (run->alg, tarjan_global_deinit);
    set_alg_local_deinit  (run->alg, tarjan_local_deinit);
    set_alg_print_stats   (run->alg, tarjan_print_stats);
    set_alg_run           (run->alg, tarjan_run);
    set_alg_reduce        (run->alg, tarjan_reduce);
}
