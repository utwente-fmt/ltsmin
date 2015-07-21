/**
 * Parallel Tarjan SCC implementation.
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/renault-tarjan.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <mc-lib/renault-unionfind.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>


typedef enum tarjan_set_e {
    STACK_STATE,
    TARJAN_STATE
} tarjan_set_t;


// SCC state info struct
typedef struct tarjan_state_s {
    uint32_t            tarjan_index;
    uint32_t            tarjan_lowlink;
    tarjan_set_t        set;
} tarjan_state_t;


typedef struct counter_s {
    uint32_t            unique_states_count;
    uint32_t            cycle_count;          // Counts the number of simple cycles (backedges)
    uint32_t            self_loop_count;      // Counts the number of self-loops
    uint32_t            scc_count;            // Counts the number of SCCs
    uint32_t            tarjan_counter;       // Counter used for tarjan_index
} counter_t;


// SCC information for each worker (1 in sequential Tarjan)
struct alg_local_s {
    dfs_stack_t         stack;                // Successor stack
    dfs_stack_t         tarjan;               // Tarjan stack
    fset_t             *states;               // states point to stack entries
    counter_t           cnt;                  // Counter for SCC information
    tarjan_state_t      state_tarjan;         // Stores tarjan info for ctx->state
    state_info_t       *target;               // Stores the successor state
    tarjan_state_t      target_tarjan;        // Stores tarjan info for loc->target
};


typedef struct uf_alg_shared_s {
    r_uf_t               *uf;                  // Renault Union-Find structure
} r_uf_alg_shared_t;



void
renault_tarjan_scc_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
renault_tarjan_scc_global_deinit   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
renault_tarjan_scc_local_init   (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof(alg_local_t));

    ctx->local->target = state_info_create ();
    state_info_add_simple (ctx->local->target, sizeof(uint32_t), &ctx->local->target_tarjan.tarjan_index);
    state_info_add_simple (ctx->local->target, sizeof(uint32_t), &ctx->local->target_tarjan.tarjan_lowlink);
    state_info_add_simple (ctx->local->target, sizeof(tarjan_set_t), &ctx->local->target_tarjan.set);

    state_info_add_simple (ctx->state, sizeof(uint32_t), &ctx->local->state_tarjan.tarjan_index);
    state_info_add_simple (ctx->state, sizeof(uint32_t), &ctx->local->state_tarjan.tarjan_lowlink);
    state_info_add_simple (ctx->state, sizeof(tarjan_set_t), &ctx->local->state_tarjan.set);

    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->local->stack = dfs_stack_create (len);
    ctx->local->tarjan = dfs_stack_create (len);

    ctx->local->cnt.unique_states_count     = 0;
    ctx->local->cnt.cycle_count             = 0;
    ctx->local->cnt.self_loop_count         = 0;
    ctx->local->cnt.scc_count               = 0;
    ctx->local->cnt.tarjan_counter          = 0;

    // create set (ref_t -> pointer to stack item)
    ctx->local->states = fset_create (sizeof(ref_t), sizeof(raw_data_t), 10, dbs_size);

    (void) run; 
}


void
renault_tarjan_scc_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    dfs_stack_destroy (loc->stack);
    dfs_stack_destroy (loc->tarjan);
    fset_free (loc->states);
    RTfree (loc);
    (void) run;
}


static void
tarjan_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    // this method gets called by the permutor for every successor of ctx->state
    wctx_t              *ctx    = (wctx_t *) arg;
    alg_local_t         *loc    = ctx->local;
    r_uf_alg_shared_t   *shared = (r_uf_alg_shared_t*) ctx->run->shared;

    ctx->counters->trans++;

    // return if we encounter a self-loop
    if (ctx->state->ref == successor->ref) {
        loc->cnt.self_loop_count++;
        return;
    }

    // return if the successor is dead
    if (r_uf_is_dead(shared->uf, successor->ref))
        return;

    // search for the successor in the set of states (has this state been found before?)
    raw_data_t         *addr;
    hash32_t            hash = ref_hash (successor->ref);
    int found = fset_find (loc->states, &hash, &successor->ref, (void **)&addr, false);

    // cycle found : stack state or Tarjan stack state (handle immediately)
    if (found) {

        // unite explored state and its successor (because the successor lies on the stack)
        r_uf_union(shared->uf, ctx->state->ref, successor->ref);

        // Update the successor's tarjan info (its lowlink value)
        state_info_deserialize (loc->target, *addr);
        loc->cnt.cycle_count += loc->target_tarjan.set == STACK_STATE;
        if (loc->state_tarjan.tarjan_lowlink > loc->target_tarjan.tarjan_lowlink) {
            loc->state_tarjan.tarjan_lowlink = loc->target_tarjan.tarjan_lowlink;
        }
    } else {
        // 'new' state : push the state on the successor stack
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }

    (void) ti; (void) seen;
}


static inline void
explore_state (wctx_t *ctx)
{
    // this method gets called by the main run method when we encounter a 'new' state
    alg_local_t            *loc = ctx->local;

    // create a new stackframe (go one step deeper in the search as we explore ctx->state)
    dfs_stack_enter (loc->stack);
    increase_level (ctx->counters);

    Debug ("Exploring %zu (%d, %d)", ctx->state->ref, loc->cnt.tarjan_counter, loc->cnt.tarjan_counter);

    // add all successors of ctx->state to the new stackframe
    permute_trans (ctx->permute, ctx->state, tarjan_handle, ctx);

    work_counter_t     *cnt = ctx->counters;
    run_maybe_report1 (ctx->run, cnt, "");
}


static void
renault_tarjan_scc_init  (wctx_t *ctx)
{
    // put the initial state on the stack
    transition_info_t        ti = GB_NO_TRANSITION;
    tarjan_handle (ctx, ctx->initial, &ti, 0);

    // reset explored and transition count
    ctx->counters->explored     = 0;
    ctx->counters->trans        = 0;
}


static void
update_parent (wctx_t *ctx, uint32_t low_child)
{
    alg_local_t            *loc = ctx->local;

    // check if there actually is a parent
    if (dfs_stack_nframes(loc->stack) == 0) return;

    // update the lowlink of the predecessor state (so from the previous stackframe)
    raw_data_t state_data = dfs_stack_peek_top (loc->stack, 1);
    state_info_deserialize (loc->target, state_data);
    if (loc->target_tarjan.tarjan_lowlink > low_child) {
        Debug ("Updating %zu from low %d --> %d", loc->target->ref, loc->target_tarjan.tarjan_lowlink, low_child);
        loc->target_tarjan.tarjan_lowlink = low_child;
        state_info_serialize (loc->target, state_data);
    }
}


static void
move_tarjan (wctx_t *ctx, state_info_t *state, raw_data_t state_data)
{
    // this method is called by the main run while backtracking from an
    //   active SCC. Thus we move the state from the successor stack to
    //   the tarjan stack
    alg_local_t            *loc = ctx->local;
    raw_data_t             *addr;

    // add the state to the tarjan stack
    loc->state_tarjan.set = TARJAN_STATE; // set before serialize
    raw_data_t tarjan_loc = dfs_stack_push (loc->tarjan, NULL);
    state_info_serialize (state, tarjan_loc);

    // update the reference to the new stack
    hash32_t            hash    = ref_hash (state->ref);
    int found = fset_find (loc->states, &hash, &state->ref, (void**)&addr, false);
    HREassert (*addr == state_data, "Wrong addr?");
    HREassert (found, "Could not find key in set");
    *addr = tarjan_loc;
}


static void
move_scc (wctx_t *ctx, ref_t state)
{
    // called by the pop_scc method, removes the state from the set of states
    alg_local_t            *loc = ctx->local;
    Debug ("Marking %zu as SCC", state);

    hash32_t            hash    = ref_hash (state);
    int success = fset_delete (loc->states, &hash, &state);
    HREassert (success, "Could not remove SCC state from set");
}

static void
pop_scc (wctx_t *ctx, ref_t root, uint32_t root_low)
{
    // called by the main run upon backtracking when index == lowlink
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;
    r_uf_alg_shared_t      *shared = (r_uf_alg_shared_t*) ctx->run->shared;
    Debug ("Found SCC with root %zu", root);

    // take all states from the tarjan stack (or until we break out of it)
    //   and put these in the same UF set as the root of the SCC
    while ( (state_data = dfs_stack_top (loc->tarjan)) ) {

        // if lowlink < root.lowlink : state is part of different SCC, thus break
        state_info_deserialize (loc->target, state_data);
        if (loc->target_tarjan.tarjan_lowlink < root_low) break;

        // unite the root of the SCC with the current state from the tarjan stack
        r_uf_union(shared->uf, root, loc->target->ref);

        // remove the state from the set of states and pop it from the tarjan stack
        move_scc (ctx, loc->target->ref);
        dfs_stack_pop (loc->tarjan);
    }
    // also remove the root from the set of states
    move_scc (ctx, root);

    // mark the SCC globally dead in the UF structure
    if (r_uf_mark_dead(shared->uf, root))
        loc->cnt.scc_count++;

}

void
renault_tarjan_scc_run  (run_t *run, wctx_t *ctx)
{
    renault_tarjan_scc_init (ctx);
    
    alg_local_t            *loc = ctx->local;
    raw_data_t             *addr;
    raw_data_t              state_data;
    r_uf_alg_shared_t      *shared = (r_uf_alg_shared_t*) ctx->run->shared;
    bool                    on_stack;

    while ( !run_is_stopped(run) ) {

        // get the top state from the dfs stack
        state_data = dfs_stack_top (loc->stack);

        // we still have states on the current stackframe
        if (state_data != NULL) {

            // store the state in ctx
            state_info_deserialize (ctx->state, state_data);

            // get claim: CLAIM_FIRST (initialized), CLAIM_SUCCESS (LIVE state), CLAIM_DEAD (DEAD state)
            char claim = r_uf_make_claim(shared->uf, ctx->state->ref);

            // if state is DEAD: disregard state and continue with next one
            if (claim == CLAIM_DEAD) {
                dfs_stack_pop (loc->stack);
                continue;
            }

            // search for the state on the set of states (has this state been found before?)
            hash32_t hash = ref_hash (ctx->state->ref);
            on_stack      = fset_find (loc->states, &hash, &ctx->state->ref, (void **)&addr, true);

            // we have not encountered this state before
            if (!on_stack) {

                // initialize information about this state and store it on the set of states
                HREassert (loc->cnt.tarjan_counter != UINT32_MAX);
                loc->state_tarjan.tarjan_index   = ++loc->cnt.tarjan_counter;
                loc->state_tarjan.tarjan_lowlink = loc->cnt.tarjan_counter;
                loc->state_tarjan.set            = STACK_STATE;
                *addr = state_data; // point fset data to stack

                // go a frame deeper and store the successors for ctx->state on it
                explore_state (ctx);

                // count the number of uniquely encountered states (combined for all workers)
                if (claim == CLAIM_FIRST)
                    ctx->counters->explored ++;

                if (loc->cnt.tarjan_counter != loc->state_tarjan.tarjan_lowlink) {
                    Debug ("Forward %zu from low %d --> %d", ctx->state->ref, loc->cnt.tarjan_counter, loc->state_tarjan.tarjan_lowlink);
                }

                // store the information back on the successor stack
                state_info_serialize (ctx->state, state_data);
            }
            // we have seen the state before, so from another successor (of this state's parent)
            //   we found a cycle including this state. Since we have backtracked afterwards and
            //   this state is not marked DEAD, it must lie in an 'active' tarjan stack. So we
            //   do not have to explore this state and only update the parent for this state
            // TODO: check if this is necessary
            else {

                // update this parent's tarjan info and remove the state from the successor stack
                state_info_deserialize (ctx->state, *addr);
                update_parent (ctx, loc->state_tarjan.tarjan_lowlink); // TODO possibly remove
                dfs_stack_pop (loc->stack);
            }
        }
        // the stackframe is empty: we need to backtrack to the parent and pop that state
        else {

            // return if we backtrack from the initial state
            if (0 == dfs_stack_nframes (loc->stack))
                break;

            // go one frame higher on the successor stack
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;

            // we have just explored all successors from the current stack_top (the parent)
            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (ctx->state, state_data);
            Debug ("Backtracking %zu (%d, %d)", ctx->state->ref, loc->state_tarjan.tarjan_index, loc->state_tarjan.tarjan_lowlink);

            // if we backtrack from the root of the SCC (index == lowlink) : report it
            if (loc->state_tarjan.tarjan_index == loc->state_tarjan.tarjan_lowlink) {
                pop_scc (ctx, ctx->state->ref, loc->state_tarjan.tarjan_lowlink);
            }
            // if otherwise we backtrack from an active SCC (since index > lowlink) :
            //   we move the state to the tarjan stack and update the parent for this state
            else {
                move_tarjan (ctx, ctx->state, state_data);
                update_parent (ctx, loc->state_tarjan.tarjan_lowlink);
            }

            // remove the state from the successor stack
            dfs_stack_pop (loc->stack);
        }
    }

    if (!run_is_stopped(run) && dfs_stack_size(loc->tarjan) != 0)
        Warning (info, "Tarjan stack not empty: %zu (stack %zu)", dfs_stack_size(loc->tarjan), dfs_stack_size(loc->stack));
    if (!run_is_stopped(run) && fset_count(loc->states) != 0)
        Warning (info, "Stack-set not empty: %zu", fset_count(loc->states));
}

void
renault_tarjan_scc_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t              *reduced = (counter_t *) run->reduced;
    counter_t              *cnt = &ctx->local->cnt;

    reduced->unique_states_count    += ctx->counters->explored;
    reduced->cycle_count += cnt->cycle_count;
    reduced->scc_count += cnt->scc_count;
    reduced->self_loop_count += cnt->self_loop_count;
}

void
renault_tarjan_scc_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;

    // SCC statistics
    Warning(info,"unique states found:   %d", reduced->unique_states_count);
    Warning(info,"self-loop count:       %d", reduced->self_loop_count);
    Warning(info,"scc count:             %d", reduced->scc_count);
    Warning(info,"avg scc size:          %.3f", ((double)reduced->unique_states_count) / reduced->scc_count);
    Warning(info," ");

    run_report_total (run);

    (void) ctx;
}

void
renault_tarjan_scc_shared_init   (run_t *run)
{
    //HREassert (SCC_STATE.g == 0);
    set_alg_local_init (run->alg, renault_tarjan_scc_local_init); 
    set_alg_global_init (run->alg, renault_tarjan_scc_global_init); 
    set_alg_global_deinit (run->alg, renault_tarjan_scc_global_deinit); 
    set_alg_local_deinit (run->alg, renault_tarjan_scc_local_deinit);
    set_alg_print_stats (run->alg, renault_tarjan_scc_print_stats); 
    set_alg_run (run->alg, renault_tarjan_scc_run); 
    set_alg_reduce (run->alg, renault_tarjan_scc_reduce); 

    run->shared                = RTmallocZero (sizeof (r_uf_alg_shared_t));
    r_uf_alg_shared_t    *shared = (r_uf_alg_shared_t*) run->shared;
    shared->uf                 = r_uf_create();
}
