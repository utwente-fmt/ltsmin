/**
 * Sequential Tarjan SCC implementation.
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/tarjan-scc.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>

// SCC state info struct
typedef struct tarjan_state_s {
    state_info_t       *info;                 // order of variables is important
    uint32_t            tarjan_index;
    uint32_t            tarjan_lowlink;
} tarjan_state_t;

// SCC information for each worker (1 in sequential Tarjan)
struct alg_local_s {
    dfs_stack_t         stack;                // Successor stack
    fset_t             *on_stack;             // states point to stack entries

    uint32_t            cycle_count;          // Counts the number of cycles (backedges)
    uint32_t            self_loop_count;      // Counts the number of self-loops
    uint32_t            scc_count;            // TODO: Counts the number of SCCs

    uint32_t            tarjan_index_counter; // Counter used for tarjan_index

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
    
    // TODO: Add serialize info on state
    tarjan_state_t dummy;

    //Warning(info, "dummy %p, si: %p, index: %p, lowlink: %p",
    //    &dummy, &dummy.info,  &dummy.tarjan_index,  &dummy.tarjan_lowlink);

    state_info_add_rel (ctx->state, sizeof(uint32_t), &dummy, &dummy.tarjan_index);
    state_info_add_rel (ctx->state, sizeof(uint32_t), &dummy, &dummy.tarjan_lowlink);
    
    state_info_add_rel (ctx->initial, sizeof(uint32_t), &dummy, &dummy.tarjan_index);
    state_info_add_rel (ctx->initial, sizeof(uint32_t), &dummy, &dummy.tarjan_lowlink);

    state_info_t       *si_perm = permute_state_info(ctx->permute);
    state_info_add_rel (si_perm, sizeof(uint32_t), &dummy, &dummy.tarjan_index);
    state_info_add_rel (si_perm, sizeof(uint32_t), &dummy, &dummy.tarjan_lowlink);

    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->local->stack = dfs_stack_create (len);

    //Warning(info,"state size = %zu", state_info_serialize_int_size(ctx->state));
    //Warning(info,"initial size = %zu", state_info_serialize_int_size(ctx->initial));
    
    ctx->local->cycle_count             = 0;
    ctx->local->self_loop_count         = 0;
    ctx->local->scc_count               = 0;
    ctx->local->tarjan_index_counter    = 1;

    // create set (ref_t -> pointer to stack item)
    ctx->local->on_stack = fset_create (sizeof(ref_t), sizeof(raw_data_t), 10, 20);


    (void) run; 
}

void
tarjan_scc_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    dfs_stack_destroy (loc->stack);
    fset_free (loc->on_stack);
    RTfree (loc);
    (void) run;
}

static void
tarjan_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t              *ctx  = (wctx_t *) arg;
    alg_local_t         *loc  = ctx->local;
    hash32_t             hash = ref_hash (successor->ref);

    // TODO: seen is global? Does this matter?
    if (!seen) {
        // allocate space for the successor
        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);

        tarjan_state_t *dummy = (tarjan_state_t *) successor;
        dummy->tarjan_index   = ctx->local->tarjan_index_counter;
        dummy->tarjan_lowlink = ctx->local->tarjan_index_counter++;

        state_info_serialize (successor, stack_loc);

        //Warning(info, "- new state = {%d,%d,%d,%d} at addr: %p", 
        //    *stack_loc, *(stack_loc+1), *(stack_loc+2), *(stack_loc+3), stack_loc);

    }  else {
        //Warning(info, "- back edge: %zu ->  %zu", ctx->state->ref, successor->ref);

        raw_data_t         *addr;
        int found = fset_find (loc->on_stack, &hash, &successor->ref, (void **)&addr, false);

        if (found) {
            loc->cycle_count ++;
            loc->self_loop_count += (ctx->state->ref == successor->ref);

            //Warning(info, "found %zu : %p", successor->ref, *p);

            raw_data_t      data = *addr;
            state_info_deserialize (successor, data);

            tarjan_state_t *source  = (tarjan_state_t *) ctx->state;
            tarjan_state_t *dest    = (tarjan_state_t *) successor;

            // Tarjan update lowlink
            if (source->tarjan_lowlink > dest->tarjan_lowlink) {
                source->tarjan_lowlink = dest->tarjan_lowlink;
                // get stack location of ctx->state and update the lowlink value
                hash = ref_hash (ctx->state->ref);
                fset_find (loc->on_stack, &hash, &ctx->state->ref, (void **)&addr, false);
                state_info_serialize (ctx->state, *addr);
            }

            //Warning(info, "- - back-edge: (%zu, %d, %d) -> (%zu, %d, %d)", 
            // ((state_info_t *)source)->ref,  source->tarjan_index,   source->tarjan_lowlink,
            // ((state_info_t *)dest)->ref, dest->tarjan_index, dest->tarjan_lowlink);

        } else {
            //Warning(info, "- - Already explored state (so no cycle)");
        }
    }
    ctx->counters->trans++;
    (void) ti;
}

static inline void
explore_state (wctx_t *ctx)
{
    //tarjan_state_t *dummy = (tarjan_state_t *) ctx->state; 

    //Warning(info, "EXPLORING STATE {%zu, %d, %d}", 
    //    ctx->state->ref, dummy->tarjan_index, dummy->tarjan_lowlink);

    permute_trans (ctx->permute, ctx->state, tarjan_handle, ctx);

    //Warning(info, "EXPLORED ");

    ctx->counters->explored++;
    work_counter_t     *cnt = ctx->counters;
    run_maybe_report1 (ctx->run, cnt, "");
}

void
tarjan_scc_init  (wctx_t *ctx)
{
    // put the initial state on the stack
    transition_info_t       ti = GB_NO_TRANSITION;
    // TODO: possibly set the initial state to ctx->state 
    // (so initial does not need to increase size)
    tarjan_handle (ctx, ctx->initial, &ti, 0);

    // reset explored and transition count
    ctx->counters->explored     = 0;
    ctx->counters->trans        = 0;
}

void
tarjan_scc_run  (run_t *run, wctx_t *ctx)
{
    tarjan_scc_init  (ctx);
    
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;

    while ( 0 < dfs_stack_size(loc->stack) ) {

        //if (loc->scc_count++ > 5) break; // premature exit for testing
        state_data = dfs_stack_top (loc->stack);

        if (NULL != state_data) { 
            // unexplored state
            dfs_stack_enter (loc->stack);
            increase_level (ctx->counters);
            state_info_deserialize (ctx->state, state_data);

            // Set state in on_stack
            hash32_t            hash = ref_hash (ctx->state->ref);
            raw_data_t         *addr;
            int found = fset_find (ctx->local->on_stack, &hash, &ctx->state->ref, (void**)&addr, true);
            HREassert (!found, "State %zu is already in the set", ctx->state->ref);
            *addr = state_data;

            explore_state (ctx);
        } 
        else { 
            // backtrack
            if (0 == dfs_stack_nframes (loc->stack))
                continue;
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;

            // Remove state from set
            state_data = dfs_stack_pop (loc->stack);
            state_info_deserialize (ctx->state, state_data);
            tarjan_state_t     *dummy   = (tarjan_state_t *) ctx->state; 
            uint32_t            low     = dummy->tarjan_lowlink;
            hash32_t            hash    = ref_hash (ctx->state->ref);
            int success = fset_delete (ctx->local->on_stack, &hash, &ctx->state->ref);
            HREassert (success, "Could not remove key from set");
            //Warning(info, "REMOVING STATE {%zu, %d, %d}", 
            //    ctx->state->ref, dummy->tarjan_index, dummy->tarjan_lowlink);

            // count the number of SCCs
            if (dummy->tarjan_index == dummy->tarjan_lowlink) {
                loc->scc_count ++;
            }


            // update the lowlink of the predecessor state
            if (dfs_stack_nframes(loc->stack) > 0) {
                state_data = dfs_stack_peek_top (loc->stack, 1);
                state_info_deserialize (ctx->state, state_data); // update dummy
                if (dummy->tarjan_lowlink > low) {
                    //Warning(info, "Updated lowlink for {%zu, %d, %d} to %d", 
                    //    ctx->state->ref, dummy->tarjan_index, dummy->tarjan_lowlink, low);
                    dummy->tarjan_lowlink = low;
                    state_info_serialize (ctx->state, state_data);
                }
            }
        }
    }

    (void) run;
}

void
tarjan_scc_reduce  (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
tarjan_scc_print_stats   (run_t *run, wctx_t *ctx)
{
    // SCC statistics
    Warning(info,"cycle count:     %d", ctx->local->cycle_count);
    Warning(info,"self-loop count: %d", ctx->local->self_loop_count);
    Warning(info,"scc count:       %d", ctx->local->scc_count);
    Warning(info," ");

    run_report_total (run);
    //fset_print_statistics (ctx->local->on_stack, "set");
    (void) ctx;
}

void
tarjan_scc_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, tarjan_scc_local_init); 
    set_alg_global_init (run->alg, tarjan_scc_global_init); 
    set_alg_global_deinit (run->alg, tarjan_scc_global_deinit); 
    set_alg_local_deinit (run->alg, tarjan_scc_local_deinit);
    set_alg_print_stats (run->alg, tarjan_scc_print_stats); 
    set_alg_run (run->alg, tarjan_scc_run); 
    set_alg_reduce (run->alg, tarjan_scc_reduce); 
}
