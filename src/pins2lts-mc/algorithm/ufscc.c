/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <mc-lib/unionfind.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins.h>
#include <util-lib/fast_set.h>
#include <pins2lts-mc/algorithm/ltl.h>

#if SEARCH_COMPLETE_GRAPH
#include <mc-lib/dlopen_extra.h>
#endif

#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif


/**
 * local counters
 */
typedef struct counter_s {
    uint32_t            scc_count;
    uint32_t            unique_states;
    uint32_t            unique_trans;
} counter_t;


/**
 * local SCC information (for each worker)
 */
struct alg_local_s {
    dfs_stack_t         search_stack;         // search stack (D)
    dfs_stack_t         roots_stack;          // roots stack (R)
    counter_t           cnt;
    state_info_t       *target;               // auxiliary state
    state_info_t       *root;                 // auxiliary state
};


/**
 * shared SCC information (between workers)
 */
typedef struct uf_alg_shared_s {
    uf_t               *uf;                   // shared union-find structure
} uf_alg_shared_t;


void
ufscc_global_init (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
ufscc_global_deinit (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
ufscc_local_init (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof (alg_local_t) );

    ctx->local->target = state_info_create ();
    ctx->local->root   = state_info_create ();

    size_t len               = state_info_serialize_int_size (ctx->state);
    ctx->local->search_stack = dfs_stack_create (len);
    ctx->local->roots_stack  = dfs_stack_create (len);

    ctx->local->cnt.scc_count               = 0;
    ctx->local->cnt.unique_states           = 0;
    ctx->local->cnt.unique_trans            = 0;

#if SEARCH_COMPLETE_GRAPH
    dlopen_setup (files[0]);
#endif

    (void) run; 
}


void
ufscc_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    dfs_stack_destroy (loc->search_stack);
    dfs_stack_destroy (loc->roots_stack);
    RTfree (loc);
    (void) run;
}


static void
ufscc_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    wctx_t             *ctx       = (wctx_t *) arg;
    alg_local_t        *loc       = ctx->local;
    raw_data_t          stack_loc;

    ctx->counters->trans++;

    // self-loop
    if (ctx->state->ref == successor->ref)
        return;

    // TODO: better to do sameset/dead check here?
    // - for now, just put all successors on the stack

    stack_loc = dfs_stack_push (loc->search_stack, NULL);
    state_info_serialize (successor, stack_loc);

    (void) arg; (void) ti; (void) seen;
}


#if SEARCH_COMPLETE_GRAPH
/**
 * bypasses pins to directly handle the successor
 * assumes that we only require state->ref
 */
static inline void
permute_complete (void *arg, transition_info_t *ti, state_data_t dst, int *cpy)
{
    wctx_t             *ctx        = (wctx_t *) arg;
    alg_local_t        *loc        = ctx->local;

    loc->target->ref = (ref_t) dst[0];
    ufscc_handle (ctx, loc->target, ti, 0);

    (void) cpy;
}
#endif


/**
 * make a stackframe on search_stack and handle the successors of ctx->state
 * also pushes ctx->state on the roots stack
 */
static inline size_t
explore_state (wctx_t *ctx)
{
    alg_local_t        *loc       = ctx->local;
    raw_data_t          stack_loc;
    size_t              trans;

    // push the state on the roots stack
    state_info_set (loc->root,  ctx->state->ref, LM_NULL_LATTICE);
    stack_loc = dfs_stack_push (loc->roots_stack, NULL);
    state_info_serialize (loc->root, stack_loc);

    increase_level (ctx->counters);
    dfs_stack_enter (loc->search_stack);

#if SEARCH_COMPLETE_GRAPH
    // bypass the pins interface by directly handling the successors
    int                 ref_arr[2];
    ref_arr[0] =  (int) ctx->state->ref;
    trans = dlopen_next_state (NULL, 0, ref_arr, permute_complete, ctx);
#else
    trans = permute_trans (ctx->permute, ctx->state, ufscc_handle, ctx);
#endif

    ctx->counters->explored ++;
    run_maybe_report1 (ctx->run, ctx->counters, "");

    return trans;
}


static void
ufscc_init  (wctx_t *ctx)
{
    alg_local_t        *loc    = ctx->local;
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) ctx->run->shared;
    transition_info_t   ti     = GB_NO_TRANSITION;
    char                claim;
    size_t              transitions;
    raw_data_t          state_data;

#if SEARCH_COMPLETE_GRAPH
    ufscc_handle (ctx, loc->target, &ti, 0);
    claim = uf_make_claim (shared->uf, loc->target->ref, ctx->id);
#else
    ufscc_handle (ctx, ctx->initial, &ti, 0);
    claim = uf_make_claim (shared->uf, ctx->initial->ref, ctx->id);
#endif
    
    // explore the initial state
    state_data = dfs_stack_top (loc->search_stack);
    state_info_deserialize (ctx->state, state_data); // search_stack TOP
    transitions = explore_state(ctx);

    if (claim == CLAIM_FIRST) {
        loc->cnt.unique_states ++;
        loc->cnt.unique_trans += transitions;
    }
}


/**
 * uses the top state from search_stack (== ctx->state) and explores it
 */
static inline void
successor (wctx_t *ctx)
{
    alg_local_t        *loc        = ctx->local;
    uf_alg_shared_t    *shared     = (uf_alg_shared_t*) ctx->run->shared;
    raw_data_t          state_data, root_data;
    char                claim;
    size_t              trans;

    // get the parent state from the search_stack
    state_data = dfs_stack_peek_top (loc->search_stack, 1);
    state_info_deserialize (loc->target, state_data);

    // edge : FROM -> TO
    // FROM = parent    = loc->target
    // TO   = successor = ctx->state

    // early backtrack if parent is explored ==> all its children are explored
    if ( !uf_is_in_list (shared->uf, loc->target->ref) ) {
        dfs_stack_pop (loc->search_stack);
        return;
    }

    // make claim:
    // - CLAIM_FIRST   (initialized)
    // - CLAIM_SUCCESS (LIVE state and we have NOT yet visited its SCC)
    // - CLAIM_FOUND   (LIVE state and we have visited its SCC before)
    // - CLAIM_DEAD    (DEAD state)
    claim = uf_make_claim (shared->uf, ctx->state->ref, ctx->id);
    
    if (claim == CLAIM_DEAD) {
        // (TO == DEAD) ==> get next successor
        dfs_stack_pop (loc->search_stack);
        return;
    }

    else if (claim == CLAIM_SUCCESS || claim == CLAIM_FIRST) {
        // (TO == 'new' state) ==> 'recursively' explore

        trans = explore_state (ctx);

        if (claim == CLAIM_FIRST) {
            loc->cnt.unique_states ++;
            loc->cnt.unique_trans += trans;
        }
        return;
    }

    else  { // result == CLAIM_FOUND
        // (TO == state in previously visited SCC) ==> cycle found

        if (uf_sameset(shared->uf, loc->target->ref, ctx->state->ref))  {
            dfs_stack_pop (loc->search_stack);
            return;
        }

        // we have:  .. -> TO  -> .. -> FROM -> TO
        // where D = .. -> TO  -> .. -> FROM
        // and   R = .. -> TO* -> .. -> FROM  (may have fewer states than D)
        //                                    (TO* is a state in SCC(TO))
        // ==> unite and pop states from R until sameset (R.TOP, TO)

        root_data = dfs_stack_top (loc->roots_stack);
        state_info_deserialize (loc->root, root_data); // roots_stack TOP

        // while ( not sameset (FROM, TO) )
        //   R.POP
        //   Union (R.TOP, FROM)
        while ( !uf_sameset (shared->uf, ctx->state->ref, loc->target->ref) ) {
            
            dfs_stack_pop (loc->roots_stack); // UF Stack POP

            root_data = dfs_stack_top (loc->roots_stack);
            state_info_deserialize (loc->root, root_data); // roots_stack TOP

            uf_union (shared->uf, loc->root->ref, loc->target->ref);
        }

        // cycle is now merged (and DFS stack is unchanged)
        dfs_stack_pop (loc->search_stack);
        return;
    }
}


/**
 * there are no states on the current stackframe. We leave the stackframe and
 * check if the previous state (parent) is part of the same SCC. If this is not
 * the case, then we use pick_from_list to find a new state in the same SCC. If
 * we find one, it gets pushed on the search_stack (note that because of this,
 * the search_stack does not preserve the exact search paths) and it will be
 * addressed later. If no LIVE state is found in pick_from_list, then we have
 * that the SCC is complete (and marked DEAD). We then pop states from the
 * roots stack to ensure that the SCC is completely removed from the stacks.
 */
static inline void
backtrack (wctx_t *ctx)
{
    alg_local_t        *loc           = ctx->local;
    uf_alg_shared_t    *shared        = (uf_alg_shared_t*) ctx->run->shared;
    raw_data_t          state_data;
    bool                is_last_state;
    ref_t               state_picked;
    char                pick;
    raw_data_t          root_data;

    // leave the stackframe
    dfs_stack_leave (loc->search_stack);
    ctx->counters->level_cur--;

    // get the new stack top (ctx->state), which is now fully explored
    state_data = dfs_stack_top (loc->search_stack);
    state_info_deserialize (ctx->state, state_data);

    // remove the fully explored state from the search_stack
    dfs_stack_pop (loc->search_stack);

    // remove ctx->state from the list
    // (no other workers have to explore this state anymore)
    uf_remove_from_list (shared->uf, ctx->state->ref);

    // store the previous state (from the removed one) in loc->target
    is_last_state = (0 == dfs_stack_nframes (loc->search_stack) );
    if (!is_last_state) {
        state_data = dfs_stack_peek_top (loc->search_stack, 1);
        state_info_deserialize (loc->target, state_data);
    }

    // check if previous state is part of the same SCC
    // - if so: standard backtrack (we don't need to report an SCC)
    // - else:  use pick_from_list to determine if the SCC is completed
    if ( !is_last_state
         && uf_sameset (shared->uf, loc->target->ref, ctx->state->ref) ) {
        return; // backtrack in same set
    }

    // ctx->state is the last KNOWN state in its SCC (according to this worker)
    // ==> check if we can find another one with pick_from_list
    pick = uf_pick_from_list (shared->uf, ctx->state->ref, &state_picked);

    if (pick != PICK_SUCCESS) {
        // list is empty ==> SCC is completely explored

        // were we the one that marked it dead?
        if (pick == PICK_MARK_DEAD) {
            loc->cnt.scc_count ++;
        }

        // the SCC of the initial state has been marked DEAD ==> we are done!
        if (is_last_state) {
            return;
        }

        // we may still have states on the stack of this SCC
        if ( uf_sameset (shared->uf, loc->target->ref, ctx->state->ref) ) {
            return; // backtrack in same set 
            // (the state got marked dead AFTER the previous sameset check)
        }

        // pop states from Roots until !sameset (v, Roots.TOP)
        //  since Roots.TOP might not be the actual root of the SCC
        root_data = dfs_stack_top (loc->roots_stack);
        state_info_deserialize (loc->root, root_data); // R.TOP
        // pop from Roots
        while (uf_sameset (shared->uf, ctx->state->ref, loc->root->ref) ) {
            dfs_stack_pop (loc->roots_stack); // R.POP
            root_data = dfs_stack_top (loc->roots_stack);
            state_info_deserialize (loc->root, root_data); // R.TOP
        }
    }
    else {
        // Found w in List(v) ==> push w on stack and search its successors
        state_info_set (ctx->state, state_picked, LM_NULL_LATTICE);
        state_data = dfs_stack_push (loc->search_stack, NULL);
        state_info_serialize (ctx->state, state_data);
        explore_state (ctx);
    }
}


void
ufscc_run  (run_t *run, wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;

#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning (info, "Using the profiler");
    ProfilerStart ("ufscc.perf");
#endif

#if SEARCH_COMPLETE_GRAPH
    int init_state = dlopen_get_worker_initial_state (ctx->id, W);
    int inits = 0;
    while (1)
    {
        inits ++;
        loc->target->ref = init_state;
#endif

    ufscc_init (ctx);

    // continue until we are done exploring the graph
    while ( !run_is_stopped(run)) {

        state_data = dfs_stack_top (loc->search_stack);

        if (state_data != NULL) {
            // there is a state on the current stackframe ==> explore it

            // store state in ctx->state
            state_info_deserialize (ctx->state, state_data);

            successor (ctx);
        }
        else {
            // there is no state on the current stackframe ==> backtrack

            // we are done if we backtrack from the initial state
            if (0 == dfs_stack_nframes (loc->search_stack))
                break;

            backtrack (ctx);
        }
    }

#if SEARCH_COMPLETE_GRAPH
        init_state = dlopen_get_new_initial_state (init_state);
        if (init_state == -1) {
            Warning(info, "Number of inits : %d", inits);
            break;
        }
    }
#endif

#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning(info, "Done profiling");
    ProfilerStop();
#endif

    (void) run;
}


void
ufscc_reduce (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t          *reduced = (counter_t *) run->reduced;
    counter_t          *cnt     = &ctx->local->cnt;

    reduced->unique_trans           += cnt->unique_trans;
    reduced->unique_states          += cnt->unique_states;
    reduced->scc_count              += cnt->scc_count;
}


void
print_worker_stats (wctx_t *ctx)
{
    counter_t          *cnt     = &ctx->local->cnt;

    Warning(info, "worker states count:        %d", cnt->unique_states);
    Warning(info, "worker transitions count:   %d", cnt->unique_trans);
    Warning(info, "worker scc count:           %d", cnt->scc_count);
}


void
ufscc_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;

    // SCC statistics
    Warning(info, "unique states count:        %d", reduced->unique_states);
    Warning(info, "unique transitions count:   %d", reduced->unique_trans);
    Warning(info, "total scc count:            %d", reduced->scc_count);
    Warning(info, " ");

    run_report_total (run);

    (void) ctx;
}


int
ufscc_state_seen (void *ptr, ref_t ref, int seen)
{
    wctx_t             *ctx    = (wctx_t *) ptr;
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) ctx->run->shared;

    return uf_owner (shared->uf, ref, ctx->id);
    (void) seen;
}


void
ufscc_shared_init   (run_t *run)
{
    uf_alg_shared_t    *shared;

    set_alg_local_init    (run->alg, ufscc_local_init);
    set_alg_global_init   (run->alg, ufscc_global_init);
    set_alg_global_deinit (run->alg, ufscc_global_deinit); 
    set_alg_local_deinit  (run->alg, ufscc_local_deinit);
    set_alg_print_stats   (run->alg, ufscc_print_stats);
    set_alg_run           (run->alg, ufscc_run);
    set_alg_reduce        (run->alg, ufscc_reduce);
    set_alg_state_seen    (run->alg, ufscc_state_seen);

    run->shared = RTmallocZero (sizeof (uf_alg_shared_t));
    shared      = (uf_alg_shared_t*) run->shared;
    shared->uf  = uf_create();
}
