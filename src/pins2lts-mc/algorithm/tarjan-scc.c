/**
 * Sequential Tarjan SCC implementation.
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/tarjan-scc.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>


#define SCC_STATE  GRED

typedef enum tarjan_set_e {
    STACK_STATE,
    TARJAN_STATE
} tarjan_set_t;

// SCC state info struct
typedef struct tarjan_state_s { // TODO: make relative serializer / deserializer
    //state_info_t        info;               // order of variables is important
    uint32_t            tarjan_index;
    uint32_t            tarjan_lowlink;
    tarjan_set_t        set;
} tarjan_state_t;

typedef struct counter_s {
    uint32_t            cycle_count;          // Counts the number of simple cycles (backedges)
    uint32_t            self_loop_count;      // Counts the number of self-loops
    uint32_t            scc_count;            // TODO: Counts the number of SCCs
    uint32_t            tarjan_counter;       // Counter used for tarjan_index
} counter_t;

// SCC information for each worker (1 in sequential Tarjan)
struct alg_local_s {
    dfs_stack_t         stack;                // Successor stack
    dfs_stack_t         tarjan;               // Tarjan stack
    fset_t             *states;               // states point to stack entries
    counter_t           cnt;
    tarjan_state_t      state_tarjan;
    state_info_t       *target;
    tarjan_state_t      target_tarjan;
    //tarjan_state_t      initial;              // Can I comment this one out?
};



void
tarjan_scc_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
tarjan_scc_global_deinit   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
tarjan_scc_local_init   (run_t *run, wctx_t *ctx)
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

    ctx->local->cnt.cycle_count             = 0;
    ctx->local->cnt.self_loop_count         = 0;
    ctx->local->cnt.scc_count               = 0;
    ctx->local->cnt.tarjan_counter          = 0;

    // create set (ref_t -> pointer to stack item)
    ctx->local->states = fset_create (sizeof(ref_t), sizeof(raw_data_t), 10, dbs_size);

    (void) run; 
}

void
tarjan_scc_local_deinit   (run_t *run, wctx_t *ctx)
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
    wctx_t              *ctx  = (wctx_t *) arg;
    alg_local_t         *loc  = ctx->local;

    ctx->counters->trans++;

    if (ctx->state->ref == successor->ref) {
        loc->cnt.self_loop_count++;
        return;
    }

    if (state_store_has_color(successor->ref, SCC_STATE, 0)) { // SCC
        return;
    }

    raw_data_t         *addr;
    hash32_t            hash = ref_hash (successor->ref);
    int found = fset_find (loc->states, &hash, &successor->ref, (void **)&addr, false);

    if (found) { // stack state or Tarjan stack state (handle immediately)

        state_info_deserialize (loc->target, *addr);
        loc->cnt.cycle_count += loc->target_tarjan.set == STACK_STATE;

        if (loc->state_tarjan.tarjan_lowlink > loc->target_tarjan.tarjan_lowlink) {
            loc->state_tarjan.tarjan_lowlink = loc->target_tarjan.tarjan_lowlink;
        }

    } else { // NEW

        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);

    }

    (void) ti; (void) seen;
}

static inline void
explore_state (wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;

    dfs_stack_enter (loc->stack);
    increase_level (ctx->counters);

    Debug ("Exploring %zu (%d, %d)", ctx->state->ref, loc->cnt.tarjan_counter, loc->cnt.tarjan_counter)
    permute_trans (ctx->permute, ctx->state, tarjan_handle, ctx);

    ctx->counters->explored++;
    work_counter_t     *cnt = ctx->counters;
    run_maybe_report1 (ctx->run, cnt, "");
}

static void
tarjan_scc_init  (wctx_t *ctx)
{
    // put the initial state on the stack
    transition_info_t       ti = GB_NO_TRANSITION;
    tarjan_handle (ctx, ctx->initial, &ti, 0);

    // reset explored and transition count
    ctx->counters->explored     = 0;
    ctx->counters->trans        = 0;
}


static void
update_parent (wctx_t *ctx, uint32_t low_child)
{
    alg_local_t            *loc = ctx->local;

    if (dfs_stack_nframes(loc->stack) == 0) return; // no parent

    // update the lowlink of the predecessor state
    raw_data_t state_data = dfs_stack_peek_top (loc->stack, 1);
    state_info_deserialize (loc->target, state_data);
    if (loc->target_tarjan.tarjan_lowlink > low_child) {
        Debug ("Updating %zu from low %d --> %d", loc->target->ref, loc->target_tarjan.tarjan_lowlink, low_child);
        loc->target_tarjan.tarjan_lowlink = low_child;
        state_info_serialize (loc->target, state_data);
    }
}

// Move a DFS stack state to the Tarjan stack
static void
move_tarjan (wctx_t *ctx, state_info_t *state, raw_data_t state_data)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t             *addr;

    // Add to Tarjan incomplete SCC stack
    loc->state_tarjan.set = TARJAN_STATE; // set before serialize
    raw_data_t tarjan_loc = dfs_stack_push (loc->tarjan, NULL);
    state_info_serialize (state, tarjan_loc);

    // Update reference to new stack
    hash32_t            hash    = ref_hash (state->ref);
    int found = fset_find (loc->states, &hash, &state->ref, (void**)&addr, false);
    HREassert (*addr == state_data, "Wrong addr?");
    HREassert (found, "Could not find key in set");
    *addr = tarjan_loc;
}

// Move a stack state to the SCC set
static void
move_scc (wctx_t *ctx, ref_t state)
{
    alg_local_t            *loc = ctx->local;
    Debug ("Marking %zu as SCC", state);

    hash32_t            hash    = ref_hash (state);
    int success = fset_delete (loc->states, &hash, &state);
    HREassert (success, "Could not remove SCC state from set");

    state_store_try_color (state, SCC_STATE, 0); // set SCC globally!
}

static void
pop_scc (wctx_t *ctx, ref_t root, uint32_t root_low)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;
    Debug ("Found SCC with root %zu", root);

    // Mark SCC!
    loc->cnt.scc_count++;
    while (( state_data = dfs_stack_top (loc->tarjan) )) {

        state_info_deserialize (loc->target, state_data); // update dummy
        if (loc->target_tarjan.tarjan_lowlink < root_low) break;

        move_scc (ctx, loc->target->ref);
        dfs_stack_pop (loc->tarjan);
    }
    move_scc (ctx, root); // move the root!
}

void
tarjan_scc_run  (run_t *run, wctx_t *ctx)
{
    tarjan_scc_init (ctx);
    
    alg_local_t            *loc = ctx->local;
    raw_data_t             *addr;
    raw_data_t              state_data;
    bool                    on_stack;

    while ( !run_is_stopped(run) ) {

        state_data = dfs_stack_top (loc->stack);
        if (state_data != NULL) {

            state_info_deserialize (ctx->state, state_data);

            if (state_store_has_color (ctx->state->ref, SCC_STATE, 0)) { // SCC
                dfs_stack_pop (loc->stack);
                continue;
            }

            hash32_t            hash = ref_hash (ctx->state->ref);
            on_stack = fset_find (loc->states, &hash, &ctx->state->ref, (void **)&addr, true);

            if (!on_stack) { // NEW state

                HREassert (loc->cnt.tarjan_counter != UINT32_MAX);
                loc->state_tarjan.tarjan_index   = ++loc->cnt.tarjan_counter;
                loc->state_tarjan.tarjan_lowlink = loc->cnt.tarjan_counter;
                loc->state_tarjan.set            = STACK_STATE;
                *addr = state_data; // point fset data to stack

                explore_state (ctx);

                if (loc->cnt.tarjan_counter != loc->state_tarjan.tarjan_lowlink) {
                    Debug ("Forward %zu from low %d --> %d", ctx->state->ref, loc->cnt.tarjan_counter, loc->state_tarjan.tarjan_lowlink);
                }
                state_info_serialize (ctx->state, state_data);

            } else {

                state_info_deserialize (ctx->state, *addr); // necessary as it might be on Tarjan stack! (actually it can only be there)
                update_parent (ctx, loc->state_tarjan.tarjan_lowlink);
                dfs_stack_pop (loc->stack);

            }

        } else {  // backtrack

            if (0 == dfs_stack_nframes (loc->stack))
                break;
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;

            state_data = dfs_stack_top (loc->stack);
            state_info_deserialize (ctx->state, state_data); // Stack state
            Debug ("Backtracking %zu (%d, %d)", ctx->state->ref, loc->state_tarjan.tarjan_index, loc->state_tarjan.tarjan_lowlink)

            if (loc->state_tarjan.tarjan_index == loc->state_tarjan.tarjan_lowlink) {

                pop_scc (ctx, ctx->state->ref, loc->state_tarjan.tarjan_lowlink);

            } else {

                move_tarjan (ctx, ctx->state, state_data);
                update_parent (ctx, loc->state_tarjan.tarjan_lowlink);

            }

            dfs_stack_pop (loc->stack);
        }
    }
    
    if (!run_is_stopped(run) && dfs_stack_size(loc->tarjan) != 0)
        Warning (info, "Tarjan stack not empty: %zu (stack %zu)", dfs_stack_size(loc->tarjan), dfs_stack_size(loc->stack));
    if (!run_is_stopped(run) && fset_count(loc->states) != 0)
        Warning (info, "Stack-set not empty: %zu", fset_count(loc->states));
}

void
tarjan_scc_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t              *reduced = (counter_t *) run->reduced;
    counter_t              *cnt = &ctx->local->cnt;

    reduced->cycle_count += cnt->cycle_count;
    reduced->scc_count += cnt->scc_count;
    reduced->self_loop_count += cnt->self_loop_count;
}

void
tarjan_scc_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;

    // SCC statistics
    Warning(info,"unique states found:   %zu", ctx->counters->explored);
    Warning(info,"simple cycle count: %d", reduced->cycle_count);
    Warning(info,"self-loop count:    %d", reduced->self_loop_count);
    Warning(info,"scc count:          %d", reduced->scc_count);
    Warning(info,"avg scc size:       %.3f", ((double)ctx->counters->explored) / reduced->scc_count);
    Warning(info," ");

    run_report_total (run);
}

void
tarjan_scc_shared_init   (run_t *run)
{
    HREassert (SCC_STATE.g == 0);
    set_alg_local_init (run->alg, tarjan_scc_local_init); 
    set_alg_global_init (run->alg, tarjan_scc_global_init); 
    set_alg_global_deinit (run->alg, tarjan_scc_global_deinit); 
    set_alg_local_deinit (run->alg, tarjan_scc_local_deinit);
    set_alg_print_stats (run->alg, tarjan_scc_print_stats); 
    set_alg_run (run->alg, tarjan_scc_run); 
    set_alg_reduce (run->alg, tarjan_scc_reduce); 
}
