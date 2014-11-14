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
    //state_info_t        info;                 // order of variables is important
    uint32_t            tarjan_index;
    uint32_t            tarjan_lowlink;
    tarjan_set_t        set;
} tarjan_state_t;

// SCC information for each worker (1 in sequential Tarjan)
struct alg_local_s {
    dfs_stack_t         stack;                // Successor stack
    dfs_stack_t         tarjan;               // Tarjan stack
    fset_t             *states;             // states point to stack entries

    uint32_t            cycle_count;          // Counts the number of simple cycles (backedges)
    uint32_t            self_loop_count;      // Counts the number of self-loops
    uint32_t            scc_count;            // TODO: Counts the number of SCCs

    uint32_t            tarjan_index_counter; // Counter used for tarjan_index

    tarjan_state_t      state;
    tarjan_state_t      initial;
    raw_data_t          source_data;

    state_info_t       *target;
    tarjan_state_t      targett;
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

    //Warning(info, "dummy %p, si: %p, index: %p, lowlink: %p",
    //    &dummy, &dummy.info,  &dummy.tarjan_index,  &dummy.tarjan_lowlink);


    ctx->local->target = state_info_create ();
    state_info_add_simple (ctx->local->target, sizeof(uint32_t), &ctx->local->targett.tarjan_index);
    state_info_add_simple (ctx->local->target, sizeof(uint32_t), &ctx->local->targett.tarjan_lowlink);
    state_info_add_simple (ctx->local->target, sizeof(tarjan_set_t), &ctx->local->targett.set);

    state_info_add_simple (ctx->state, sizeof(uint32_t), &ctx->local->state.tarjan_index);
    state_info_add_simple (ctx->state, sizeof(uint32_t), &ctx->local->state.tarjan_lowlink);
    state_info_add_simple (ctx->state, sizeof(tarjan_set_t), &ctx->local->state.set);
    
    state_info_add_simple (ctx->initial, sizeof(uint32_t), &ctx->local->initial.tarjan_index);
    state_info_add_simple (ctx->initial, sizeof(uint32_t), &ctx->local->initial.tarjan_lowlink);
    state_info_add_simple (ctx->initial, sizeof(tarjan_set_t), &ctx->local->initial.set);

    size_t len = state_info_serialize_int_size (ctx->state);
    ctx->local->stack = dfs_stack_create (len);
    ctx->local->tarjan = dfs_stack_create (len);

    //Warning(info,"state size = %zu", state_info_serialize_int_size(ctx->state));
    //Warning(info,"initial size = %zu", state_info_serialize_int_size(ctx->initial));
    
    ctx->local->cycle_count             = 0;
    ctx->local->self_loop_count         = 0;
    ctx->local->scc_count               = 0;
    ctx->local->tarjan_index_counter    = 1;

    // create set (ref_t -> pointer to stack item)
    ctx->local->states = fset_create (sizeof(ref_t), sizeof(raw_data_t), 10, 20);


    (void) run; 
}

void
tarjan_scc_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    dfs_stack_destroy (loc->stack);
    fset_free (loc->states);
    RTfree (loc);
    (void) run;
}

static void
tarjan_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t              *ctx  = (wctx_t *) arg;
    alg_local_t         *loc  = ctx->local;
    hash32_t             hash = ref_hash (successor->ref);

    //Warning(info, "- back edge: %zu ->  %zu", ctx->state->ref, successor->ref);

    raw_data_t         *addr;
    int found = fset_find (loc->states, &hash, &successor->ref, (void **)&addr, false);

    if (found) { // stack state or Tarjan stack state
        loc->cycle_count ++;
        loc->self_loop_count += (ctx->state->ref == successor->ref);

        //Warning(info, "found %zu : %p", successor->ref, *p);

        raw_data_t      data = *addr;
        state_info_deserialize (ctx->local->target, data);

        // Tarjan update lowlink
        if (ctx->local->state.tarjan_lowlink > ctx->local->targett.tarjan_lowlink) {
            ctx->local->state.tarjan_lowlink = ctx->local->targett.tarjan_lowlink;
            state_info_serialize (ctx->state, ctx->local->source_data); // avoid all this serialization work by obaining relative locations from the serializer
        }

        //Warning(info, "- - back-edge: (%zu, %d, %d) -> (%zu, %d, %d)",
        // ((state_info_t *)source)->ref,  source->tarjan_index,   source->tarjan_lowlink,
        // ((state_info_t *)dest)->ref, dest->tarjan_index, dest->tarjan_lowlink);

    } else if (!state_store_has_color(ctx->state->ref, SCC_STATE, 0)) { // !SCC
        //Warning(info, "- - Already explored state (so no cycle)");

        raw_data_t stack_loc = dfs_stack_push (loc->stack, NULL);
        state_info_serialize (successor, stack_loc);
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
    raw_data_t             *addr;

    while ( true ) {

        //if (loc->scc_count++ > 5) break; // premature exit for testing
        ctx->local->source_data = dfs_stack_top (loc->stack);

        if (ctx->local->source_data != NULL) {
            // unexplored state
            dfs_stack_enter (loc->stack);
            increase_level (ctx->counters);
            state_info_deserialize (ctx->state, ctx->local->source_data);

            hash32_t            hash = ref_hash (ctx->state->ref);
            int found = fset_find (ctx->local->states, &hash, &ctx->state->ref, (void**)&addr, true);

            if (!found && !state_store_has_color(ctx->state->ref, SCC_STATE, 0)) {
                ctx->local->state.tarjan_index   = ctx->local->tarjan_index_counter;
                ctx->local->state.tarjan_lowlink = ctx->local->tarjan_index_counter++;
                ctx->local->state.set   = STACK_STATE;
                *addr = ctx->local->source_data; // point fset data to stack

                explore_state (ctx);
            }
        } 
        else { 
            // backtrack
            if (0 == dfs_stack_nframes (loc->stack))
                break;
            dfs_stack_leave (loc->stack);
            ctx->counters->level_cur--;


            raw_data_t          state_data = dfs_stack_pop (loc->stack);
            state_info_deserialize (ctx->state, state_data);
            uint32_t            low     = ctx->local->state.tarjan_lowlink;
            hash32_t            hash    = ref_hash (ctx->state->ref);

            // count the number of SCCs
            if (ctx->local->state.tarjan_index == ctx->local->state.tarjan_lowlink) {
                loc->scc_count ++;

                // Mark SCC!
                do {
                    state_data = dfs_stack_pop (loc->tarjan);
                    state_info_deserialize (ctx->local->target, state_data); // update dummy

                    int success = fset_delete (ctx->local->states, &hash, &ctx->local->target->ref);
                    HREassert (success, "Could not remove key from set");

                    state_store_try_color(ctx->state->ref, SCC_STATE, 0); // set SCC globally!
                } while (ctx->local->targett.tarjan_lowlink > low);

            } else {


                // Add to Tarjan incomplete SCC stack
                ctx->local->state.set = TARJAN_STATE; // set before serialize
                raw_data_t tarjan_loc = dfs_stack_push (loc->stack, NULL);
                state_info_serialize (ctx->state, state_data);

                int found = fset_find (ctx->local->states, &hash, &ctx->state->ref, (void**)&addr, false);
                HREassert (found, "Could find key in set");
                *addr = tarjan_loc;

                // update the lowlink of the predecessor state
                if (dfs_stack_nframes(loc->stack) > 0) {
                    state_data = dfs_stack_peek_top (loc->stack, 1);
                    state_info_deserialize (ctx->local->target, state_data); // update dummy
                    if (ctx->local->targett.tarjan_lowlink > low) {
                        ctx->local->targett.tarjan_lowlink = low;
                        state_info_serialize (ctx->local->target, state_data);
                    }
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
    Warning(info,"simple cycle count: %d", ctx->local->cycle_count);
    Warning(info,"self-loop count:    %d", ctx->local->self_loop_count);
    Warning(info,"scc count:          %d", ctx->local->scc_count);
    Warning(info," ");

    run_report_total (run);
    //fset_print_statistics (ctx->local->on_stack, "set");
    (void) ctx;
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
