/*
 * Multi-Core implementation of a Union-Find based
 * Strongly-Connected-Component algorithm
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

// SCC state info struct
typedef struct ufscc_state_s { 
    size_t              group_index;
} ufscc_state_t;


typedef struct counter_s {
    uint32_t            unique_states_count;
    uint32_t            self_loop_count;     // Counts the number of self-loops
    uint32_t            scc_count;           // Counts the number of SCCs
} counter_t;

// SCC information for each worker
struct alg_local_s {
    dfs_stack_t         dstack;              // DFS stack (D)
    dfs_stack_t         rstack;              // Roots stack (R)
    counter_t           cnt;
    state_info_t       *target;              // Successor
    state_info_t       *root;                // Root
    ufscc_state_t       state_ufscc;
    ufscc_state_t       target_ufscc;
    ufscc_state_t       root_ufscc;
    size_t              start_group;          // starting group (actually a static value)
};

typedef struct uf_alg_shared_s {
    uf_t               *uf;                  // Union-Find structure
} uf_alg_shared_t;


void
ufscc_global_init   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ufscc_global_deinit   (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}

void
ufscc_local_init   (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof(alg_local_t));

    ctx->local->target = state_info_create ();
    ctx->local->root   = state_info_create ();
    state_info_add_simple (ctx->local->target, sizeof(size_t), &ctx->local->target_ufscc.group_index);
    state_info_add_simple (ctx->local->root, sizeof(size_t), &ctx->local->root_ufscc.group_index);
    
    state_info_add_simple (ctx->state, sizeof(size_t), &ctx->local->state_ufscc.group_index);

    size_t len                              = state_info_serialize_int_size (ctx->state);
    ctx->local->dstack                      = dfs_stack_create (len);
    ctx->local->rstack                      = dfs_stack_create (len);

    ctx->local->cnt.unique_states_count     = 0;
    ctx->local->cnt.self_loop_count         = 0;
    ctx->local->cnt.scc_count               = 0;

    size_t                          nGroups = pins_get_group_count(ctx->model);
    // according to the internet, 73 is the best prime number
    // (this is used to divide the workers over the successors)
    ctx->local->start_group                     = (ctx->id * 73) % nGroups;

    (void) run; 
}

void
ufscc_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    dfs_stack_destroy (loc->dstack);
    dfs_stack_destroy (loc->rstack);

    RTfree (loc);
    (void) run;
}

static void
ufscc_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    // sets loc->target = successor

    //Warning(info, "Initializing successor  %zu", successor->ref);
    wctx_t              *ctx        = (wctx_t *) arg;
    alg_local_t         *loc        = ctx->local;

    ctx->counters->trans++;

    loc->target = successor;

    (void) arg; (void) ti; (void) seen;
}

static void
explore_state (wctx_t *ctx)
{
    // puts the loc->target state on the DFS and Roots stacks

    //Warning(info, "Exploring  %zu", ctx->local->target->ref);
    alg_local_t            *loc        = ctx->local;
    raw_data_t              state_data, root_data;

    // TODO: Is there a better way to save the state info on the root and dfs stacks?
    //       It seems that the group_index is not stored, unless we use state_info_set
    state_info_set(ctx->state, loc->target->ref, LM_NULL_LATTICE);  // get state info
    state_info_set(loc->root,  loc->target->ref, LM_NULL_LATTICE);  // get state info

    increase_level (ctx->counters);

    loc->state_ufscc.group_index  = loc->start_group;
    loc->root_ufscc.group_index   = loc->start_group;

    state_data = dfs_stack_push (loc->dstack, NULL);
    state_info_serialize (ctx->state, state_data);

    root_data = dfs_stack_push (loc->rstack, NULL);
    state_info_serialize (loc->root, root_data);

    run_maybe_report1 (ctx->run, ctx->counters, "");

    //state_data = dfs_stack_top (loc->dstack);
    //state_info_deserialize (ctx->state, state_data); // DFS Stack TOP
    //HREassert(loc->state_ufscc.group_index == loc->start_group, "%zu != %zu", loc->state_ufscc.group_index, loc->start_group);
}  

static void
ufscc_init  (wctx_t *ctx)
{
    // put the initial state on the stack

    alg_local_t            *loc        = ctx->local;
    uf_alg_shared_t        *shared     = (uf_alg_shared_t*) ctx->run->shared;

    loc->target = ctx->initial;
    explore_state (ctx);

    char result = uf_make_claim(shared->uf, ctx->initial->ref, ctx->id);
    
    if (result == CLAIM_FIRST) {
        ctx->counters->explored ++; // increase global counters
    }
}

bool
getNextSuccessor(wctx_t *ctx, state_info_t *si, size_t *next_group) 
{
    // Iterates over the pins groups and searches for successors
    // returns true if a successor is found, also sets next_group
    // if we iterated over all groups, we set next_group to nGroups

    alg_local_t            *loc        = ctx->local;
    size_t                  nGroups    = pins_get_group_count(ctx->model);
    int                     count;

    if (*next_group == nGroups) 
        return false;

    do {
        count = permute_next (ctx->permute, si, *next_group, ufscc_handle, ctx); 

        HREassert(count <= 1, "TODO: implement handling multiple states in group");

        // slight improvement: increment with relative prime of nGroups
        
        *next_group = *next_group + 1;
        if (*next_group == nGroups) 
            *next_group = 0;

        if (count == 1) {
            if (*next_group == loc->start_group)
                *next_group = nGroups;
            return true;
        }
    } while (*next_group != loc->start_group);

    HREassert(*next_group == loc->start_group);

    *next_group = nGroups;
    return false;
}

void
ufscc_run  (run_t *run, wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data, root_data;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;

    ufscc_init (ctx);

    while ( !run_is_stopped(run) ) {

        state_data = dfs_stack_top (loc->dstack);
        state_info_deserialize (ctx->state, state_data); // DFS Stack TOP

        ref_t v = ctx->state->ref;
        if (uf_is_dead(shared->uf, v)) {
            HREassert(!uf_is_in_list(shared->uf, v), "%d %d", 
                uf_debug(shared->uf, v), uf_print_list(shared->uf, v));
        }
        if (!uf_is_in_list(shared->uf, v)) {
            // assert GlobalDone(v) (all successors are explored)


            dfs_stack_pop (loc->dstack);  // D Stack POP
            ctx->counters->level_cur--;

            
            if (dfs_stack_size(loc->dstack) > 0) { 
                // get new D.TOP
                state_data = dfs_stack_top (loc->dstack);
                state_info_deserialize (ctx->state, state_data); // DFS Stack TOP

                if (uf_sameset(shared->uf, ctx->state->ref, v)) {
                    continue; // backtrack in same set
                }
            }

            // v is the last KNOWN state in the uf[v] set
            // ==> check if we can find another one with pick_from_list

            ref_t v_p;
            char pick = uf_pick_from_list(shared->uf, v, &v_p);

            if (pick != PICK_SUCCESS) {
                // List(v) = \emptyset ==> GlobalDead(v)
                // state is dead ==> backtrack

                //Warning(info, "DOT: A%zu [color=gray,style=filled];", v);

                HREassert(uf_is_dead(shared->uf, v));

                // were we the one that marked it dead?
                if (pick == PICK_MARK_DEAD)
                    loc->cnt.scc_count ++;


                // last state marked dead ==> done with algorithm
                if (dfs_stack_size(loc->dstack) == 0) 
                    break;

                if (uf_sameset(shared->uf, ctx->state->ref, v)) {
                    continue; // backtrack in same set
                }
                // pop states from Roots until !sameset(v, Roots.TOP)
                // (Roots.TOP might not be the actual root)
                root_data = dfs_stack_top (loc->rstack);
                state_info_deserialize (loc->root, root_data);      // Roots Stack TOP

                HREassert(uf_sameset(shared->uf, v, loc->root->ref));

                // pop from Roots
                while (uf_sameset(shared->uf, v, loc->root->ref)) {
                    dfs_stack_pop (loc->rstack);                    // Roots Stack POP

                    HREassert(dfs_stack_size(loc->rstack) != 0);

                    root_data = dfs_stack_top (loc->rstack);
                    state_info_deserialize (loc->root, root_data);  // Roots Stack TOP
                }
                HREassert(uf_sameset(shared->uf, ctx->state->ref, loc->root->ref));

                continue; // Backtrack
            }
            else {
                // Found w \in List(v) ==> push w on stack and search its successors

                state_info_set(ctx->state, v_p, LM_NULL_LATTICE);  // get state info
                loc->state_ufscc.group_index = loc->start_group;    // 'explore state'
                state_data = dfs_stack_push (loc->dstack, NULL);
                state_info_serialize (ctx->state, state_data);
                //Warning(info, "found unexplored state %zu in uf[%zu]", ctx->state->ref, v);
            }
        }

        size_t next_group = loc->state_ufscc.group_index;
        //Warning(info, "D.TOP = %zu (%zu)", ctx->state->ref, next_group);

        // Iterate over Next(ctx->state) until no new successors are found
        bool explore_new_state = false;
        bool state_is_dead     = false;
        while (getNextSuccessor(ctx, ctx->state, &next_group)) {

            // (early) backtrack if state is marked dead by another worker
            if (uf_is_dead(shared->uf, ctx->state->ref)) {
                // assert List = \emptyset
                state_is_dead = true;
                // TODO: removing the state here should improve performance a little bit
                break; // state will be removed in next iteration
            }

            //Warning(info, "FROM = %zu TO = %zu (%zu)", ctx->state->ref, loc->target->ref, next_group);
            // FROM = ctx->state->ref;
            // TO   = loc->target->ref;

            if (ctx->state->ref == loc->target->ref) { // self-loop
                loc->cnt.self_loop_count++;
                continue;
            }

            char result = uf_make_claim(shared->uf, loc->target->ref, ctx->id);
            
            // (TO == DEAD) ==> get next successor
            if (result == CLAIM_DEAD) 
                continue;

            // (TO == 'new' state) ==> 'recursively' explore
            else if (result == CLAIM_SUCCESS || result == CLAIM_FIRST) {
                if (result == CLAIM_FIRST) { 
                    // increase unique states count
                    ctx->counters->explored ++;
                    //Warning(info, "DOT: A%zu [color=chocolate,style=filled];", loc->target->ref);
                }

                // save next_group index on stack
                loc->state_ufscc.group_index = next_group;
                state_info_serialize(ctx->state, state_data);  

                // explore new state
                explore_state(ctx);
                explore_new_state = true;
                break; // exit the while loop
            }

            // (TO == found state) ==> cycle found
            else  { // result == CLAIM_FOUND

                if (uf_sameset(shared->uf, ctx->state->ref, loc->target->ref)) 
                    continue;

                HREassert(dfs_stack_size(loc->rstack) != 0);
                root_data = dfs_stack_top (loc->rstack);
                state_info_deserialize (loc->root, root_data); // Roots Stack TOP

                HREassert(uf_sameset(shared->uf, loc->root->ref, ctx->state->ref),
                    "Root: %d\nState: %d", uf_debug(shared->uf, loc->root->ref),
                    uf_debug(shared->uf, ctx->state->ref)); 

                // not SameSet(FROM,TO) ==> unite cycle
                while (!uf_sameset(shared->uf, ctx->state->ref, loc->target->ref)) {
                    // in every step:
                    // - R.POP
                    // - Union(R.TOP, D.TOP)
                    // (eventually, SameSet(R.TOP, TO) )

                    
                    dfs_stack_pop (loc->rstack); // UF Stack POP

                    HREassert(dfs_stack_size(loc->rstack) != 0);

                    root_data = dfs_stack_top (loc->rstack);
                    state_info_deserialize (loc->root, root_data); // Roots Stack TOP

                    //Warning(info, "Union(F:%zu, T:%zu) (%zu)", loc->root->ref, ctx->state->ref, loc->root_ufscc.group_index);

                    uf_union(shared->uf, loc->root->ref, ctx->state->ref);


                    HREassert(uf_sameset(shared->uf, loc->root->ref, ctx->state->ref));

                }
                HREassert(uf_sameset(shared->uf, loc->root->ref, ctx->state->ref)); 
                HREassert(uf_sameset(shared->uf, loc->root->ref, loc->target->ref)); 

                // cycle is now merged (and DFS stack is unchanged)
                // so we can continue exploring successors from ctx->state
                continue;
            }

        }

        if (!explore_new_state && !state_is_dead) {
            //Warning(info, "Fully explored state %zu (%zu, %zu)", ctx->state->ref, loc->state_ufscc.group_index, next_group);
            // all successors explored ==> remove from List
            uf_remove_from_list(shared->uf, ctx->state->ref);
        }
        /*else {
            // assert D.TOP = R.TOP = TO
            // assert D.TOP.group_index = loc->start_group
        }*/
    }

    if (!run_is_stopped(run) && dfs_stack_size(loc->dstack) != 0)
        Warning (info, "Stack not empty: %zu ", dfs_stack_size(loc->dstack));
    

    (void) run;
}

void
ufscc_reduce  (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t              *reduced = (counter_t *) run->reduced;
    counter_t              *cnt     = &ctx->local->cnt;

    reduced->unique_states_count    += ctx->counters->explored;
    reduced->scc_count              += cnt->scc_count;
    reduced->self_loop_count        += cnt->self_loop_count;
}

void
ufscc_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;

    // count actual SCCs in UF struture, for gear.1.dve example
    /*uint32_t correct_count = 0;
    ref_t x = 0;
    while (x <= 134139521) {
        if (uf_find(shared->uf, x) != 0) {
            if (uf_is_dead(shared->uf, x)) {
                correct_count ++;
                uf_mark_undead(shared->uf, x);
            }
        }
        x ++;
    }*/

    // SCC statistics
    Warning(info,"unique states found:   %d", reduced->unique_states_count);
    Warning(info,"self-loop count:       %d", reduced->self_loop_count);
    //Warning(info,"correct count:         %d", correct_count);
    Warning(info,"scc count:             %d", reduced->scc_count);
    Warning(info,"avg scc size:          %.3f", ((double)reduced->unique_states_count) / reduced->scc_count);
    Warning(info," ");

    uf_free(shared->uf);

    run_report_total (run);
}

void
ufscc_shared_init   (run_t *run)
{
    set_alg_local_init (run->alg, ufscc_local_init); 
    set_alg_global_init (run->alg, ufscc_global_init); 
    set_alg_global_deinit (run->alg, ufscc_global_deinit); 
    set_alg_local_deinit (run->alg, ufscc_local_deinit);
    set_alg_print_stats (run->alg, ufscc_print_stats); 
    set_alg_run (run->alg, ufscc_run); 
    set_alg_reduce (run->alg, ufscc_reduce); 

    run->shared                = RTmallocZero (sizeof (uf_alg_shared_t));
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) run->shared;
    shared->uf                 = uf_create();
}
