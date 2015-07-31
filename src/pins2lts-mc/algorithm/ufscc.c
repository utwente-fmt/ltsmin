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
#include <pins2lts-mc/algorithm/ltl.h>


#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif



typedef struct counter_s {
    uint32_t            self_loop_count;         // Counts the number of self-loops
    uint32_t            scc_count;               // Counts the number of SCCs
    uint32_t            unique_states;           // Number of states that this worker first claimed
    uint32_t            unique_trans;            // Number of unique transitions
    uint32_t            trans_tried;             // Number of transitions tried to explore
    uint32_t            multiple_claim_count;    // Number of states that this worker did not first claimed
    uint32_t            marked_dead_count;       // Number of SCCs that this worker marked dead
    uint32_t            union_count;             // Number of states that this worker united
    uint32_t            union_success;           // Number of states that this worker united
    uint32_t            removed_from_list_count; // Number of states that this worker removed from list
} counter_t;

// SCC information for each worker
struct alg_local_s {
    dfs_stack_t         dstack;              // DFS stack (D)
    dfs_stack_t         rstack;              // Roots stack (R)
    counter_t           cnt;
    state_info_t       *target;              // Successor
    state_info_t       *root;                // Root
    size_t              start_group;         // starting group (actually a static value)
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

    size_t len                              = state_info_serialize_int_size (ctx->state);
    ctx->local->dstack                      = dfs_stack_create (len);
    ctx->local->rstack                      = dfs_stack_create (len);

    ctx->local->cnt.trans_tried             = 0;
    ctx->local->cnt.self_loop_count         = 0;
    ctx->local->cnt.scc_count               = 0;
    ctx->local->cnt.unique_states           = 0;
    ctx->local->cnt.unique_trans            = 0;
    ctx->local->cnt.multiple_claim_count    = 0;
    ctx->local->cnt.marked_dead_count       = 0;
    ctx->local->cnt.union_count             = 0;
    ctx->local->cnt.union_success           = 0;
    ctx->local->cnt.removed_from_list_count = 0;

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
    wctx_t              *ctx        = (wctx_t *) arg;
    alg_local_t         *loc        = ctx->local;

    ctx->counters->trans++;

    if (ctx->state->ref == successor->ref) {
        loc->cnt.self_loop_count++;
        // LTL
        // if (GBbuchiIsAccepting(ctx->model, state_info_state(ctx->state)))
        //     ndfs_report_cycle (ctx->run, ctx->model, loc->dstack, successor);
        return;
    }

    //Warning(info, "Initializing successor  %zu", successor->ref);

    // TODO better to do sameset/dead check here?
    // - for now, just put all successors on the stack

    raw_data_t stack_loc = dfs_stack_push (loc->dstack, NULL);
    state_info_serialize (successor, stack_loc);

    (void) arg; (void) ti; (void) seen;
}

static size_t
explore_state (wctx_t *ctx)
{
    // enter stack and put successors on it

    alg_local_t            *loc        = ctx->local;
    //Warning(info, "Exploring  %zu", ctx->state->ref);

    // push the state on the roots stack
    state_info_set(loc->root,  ctx->state->ref, LM_NULL_LATTICE);
    raw_data_t stack_loc = dfs_stack_push (loc->rstack, NULL);
    state_info_serialize (loc->root, stack_loc);
    //Warning(info, "Explored Roots top = %zu", loc->root->ref);

    increase_level (ctx->counters);
    dfs_stack_enter (loc->dstack);
    size_t trans = permute_trans (ctx->permute, ctx->state, ufscc_handle, ctx);

    ctx->counters->explored ++;
    run_maybe_report1 (ctx->run, ctx->counters, "");

    return trans;
}  

static void
ufscc_init  (wctx_t *ctx)
{
    alg_local_t            *loc        = ctx->local;
    uf_alg_shared_t        *shared     = (uf_alg_shared_t*) ctx->run->shared;
    transition_info_t       ti         = GB_NO_TRANSITION;

    // put the initial state on the stack
    ufscc_handle (ctx, ctx->initial, &ti, 0);

    char result = uf_make_claim(shared->uf, ctx->initial->ref, ctx->id);
    
    if (result == CLAIM_FIRST) {
        loc->cnt.unique_states ++;
        ctx->counters->explored ++; // increase global counters
    }
}

/*bool
getNextSuccessor (wctx_t *ctx, state_info_t *si, size_t *next_group) 
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

        //T//HREassert(count <= 1, "TODO: implement handling multiple states in group");

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

    //T//HREassert(*next_group == loc->start_group);

    *next_group = nGroups;
    return false;
}*/

void 
print_worker_stats (wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    Warning(info, "First claim count:         %d", loc->cnt.unique_states);
    Warning(info, "Multiple claim count:      %d", loc->cnt.multiple_claim_count);
    Warning(info, "Union count:               %d", loc->cnt.union_count);
    Warning(info, "Removed from list count:   %d", loc->cnt.removed_from_list_count);
    Warning(info, "Marked dead count:         %d", loc->cnt.marked_dead_count);
}


void
successor (wctx_t *ctx)
{
    alg_local_t            *loc    = ctx->local;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;

    raw_data_t state_data = dfs_stack_peek_top (loc->dstack, 1);
    state_info_deserialize (loc->target, state_data);

    // successor = ctx->state->ref
    // parent    = loc->target->ref

    // early backtrack if parent = explored (not in list)
    if (!uf_is_in_list(shared->uf, loc->target->ref)) {

        //Warning(info, "Early backtrack (%zu !in list)", loc->target->ref);

        // remove stackframe
        //dfs_stack_leave (loc->dstack);
        //ctx->counters->level_cur--;

        // TODO do we also need to pop? What is on top now?
        dfs_stack_pop (loc->dstack);
        return;
    }

    //Warning(info, "FROM = %zu TO = %zu", loc->target->ref, ctx->state->ref);
    // FROM   = loc->target->ref;
    // TO     = ctx->state->ref;

    loc->cnt.trans_tried ++;
    char result = uf_make_claim(shared->uf, ctx->state->ref, ctx->id);
    
    // (TO == DEAD) ==> get next successor
    if (result == CLAIM_DEAD) {
        dfs_stack_pop (loc->dstack);
        return;
    }

    // (TO == 'new' state) ==> 'recursively' explore
    else if (result == CLAIM_SUCCESS || result == CLAIM_FIRST) {
        if (result == CLAIM_FIRST) { 
            // increase unique states count
            loc->cnt.unique_states ++;
            //Warning(info, "DOT: A%zu [color=chocolate,style=filled];", loc->target->ref);
        } else {
            loc->cnt.multiple_claim_count ++;
        }

        // explore new state
        size_t trans = explore_state(ctx);

        if (result == CLAIM_FIRST) {
            loc->cnt.unique_trans += trans;
        }
        return;
    }

    // (TO == found state) ==> cycle found
    else  { // result == CLAIM_FOUND

        if (uf_sameset(shared->uf, loc->target->ref, ctx->state->ref))  {
            dfs_stack_pop (loc->dstack);
            return;
        }

        raw_data_t root_data = dfs_stack_top (loc->rstack);
        state_info_deserialize (loc->root, root_data); // Roots Stack TOP
        //Warning(info, "CLAIM_FOUND Roots top = %zu", loc->root->ref);

        //T//HREassert(uf_sameset(shared->uf, loc->root->ref, loc->target->ref),
        //    "Root: %d\nState: %d", uf_debug(shared->uf, loc->root->ref),
        //    uf_debug(shared->uf, loc->target->ref));

        // not SameSet(FROM,TO) ==> unite cycle
        while (!uf_sameset(shared->uf, ctx->state->ref, loc->target->ref)) {
            // in every step:
            // - R.POP
            // - Union(R.TOP, D.TOP)
            // (eventually, SameSet(R.TOP, TO) )

            
            dfs_stack_pop (loc->rstack); // UF Stack POP

            //T//HREassert(dfs_stack_size(loc->rstack) != 0);

            root_data = dfs_stack_top (loc->rstack);
            state_info_deserialize (loc->root, root_data); // Roots Stack TOP

            //Warning(info, "Union(F:%zu, T:%zu)", loc->root->ref, loc->target->ref);

            // LTL
            // if (GBbuchiIsAccepting(ctx->model, state_info_state(loc->root)) ||
            //     GBbuchiIsAccepting(ctx->model, state_info_state(loc->target)))
            //     ndfs_report_cycle (ctx->run, ctx->model, loc->dstack, loc->root);


            /*if (uf_is_dead (shared->uf, loc->root->ref) ||
                uf_is_dead (shared->uf, loc->target->ref)) {
                //T//HREassert (uf_sameset(shared->uf, loc->root->ref, loc->target->ref),
                    "Dead states cannot be united %d %d",
                    uf_debug(shared->uf, loc->root->ref),
                    uf_debug(shared->uf, loc->target->ref));
            }*/

            loc->cnt.union_count ++;
            if (uf_union(shared->uf, loc->root->ref, loc->target->ref)) {
                loc->cnt.union_success ++;
            }

            //T//HREassert(uf_sameset(shared->uf, loc->root->ref, loc->target->ref),
            //        "%d %d", uf_debug(shared->uf, loc->root->ref), uf_debug(shared->uf, loc->target->ref));

        }
        //T//HREassert(uf_sameset(shared->uf, loc->root->ref, ctx->state->ref));
        //T//HREassert(uf_sameset(shared->uf, loc->root->ref, loc->target->ref));
        //Warning(info, "UNIONED Roots top = %zu", loc->root->ref);

        // cycle is now merged (and DFS stack is unchanged)
        dfs_stack_pop (loc->dstack);
        return;
    }
}

void
backtrack (wctx_t *ctx)
{

    alg_local_t            *loc = ctx->local;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;

    dfs_stack_leave (loc->dstack);
    ctx->counters->level_cur--;

    raw_data_t state_data = dfs_stack_top (loc->dstack);
    state_info_deserialize (ctx->state, state_data); // Stack state
    ref_t v = ctx->state->ref;
    dfs_stack_pop (loc->dstack);

    bool is_last_state = (0 == dfs_stack_nframes (loc->dstack));
    if (!is_last_state) {
        state_data = dfs_stack_peek_top (loc->dstack, 1);
        state_info_deserialize (loc->target, state_data);
        //Warning(info, "Backtrack : Popped = %zu, parent = %zu", v, loc->target->ref);
    }
    
    // ctx->state->ref == globalDone ==> remove from list
    if (uf_remove_from_list(shared->uf, v)) {
        loc->cnt.removed_from_list_count ++;
    }

    // check if previous state is part of the same SCC
    // - if so, no problem
    // - else, pick from list
    if (!is_last_state && uf_sameset(shared->uf, loc->target->ref, v)) {
        return; // backtrack in same set
    }

    // v is the last KNOWN state in the uf[v] set
    // ==> check if we can find another one with pick_from_list

    ref_t v_p;
    char pick = uf_pick_from_list(shared->uf, v, &v_p);

    if (pick != PICK_SUCCESS) {
        // List(v) = \emptyset ==> GlobalDead(v)
        // state is dead ==> backtrack

        //Warning(info, "DOT: A%zu [color=gray,style=filled];", v);
        //Warning(info, "State %zu is DEAD;", v);

        //T//HREassert(uf_is_dead(shared->uf, v));

        // were we the one that marked it dead?
        if (pick == PICK_MARK_DEAD) {
            loc->cnt.marked_dead_count ++;
            loc->cnt.scc_count ++;
        }

        if (is_last_state) {
            return;
        }

        if (uf_sameset(shared->uf, loc->target->ref, v)) {
            return; // backtrack in same set 
            // (state is marked dead after previous sameset check)
        }

        // pop states from Roots until !sameset(v, Roots.TOP)
        // (Roots.TOP might not be the actual root)
        raw_data_t root_data = dfs_stack_top (loc->rstack);
        state_info_deserialize (loc->root, root_data);      // Roots Stack TOP
        //Warning(info, "DEAD Roots top = %zu", loc->root->ref);

        //T//HREassert(uf_sameset(shared->uf, v, loc->root->ref), "%d != %d",
        //    uf_debug(shared->uf, v), uf_debug(shared->uf, loc->root->ref));

        // pop from Roots
        while (uf_sameset(shared->uf, v, loc->root->ref)) {
            dfs_stack_pop (loc->rstack);                    // Roots Stack POP

            //T//HREassert(dfs_stack_size(loc->rstack) != 0);

            root_data = dfs_stack_top (loc->rstack);
            state_info_deserialize (loc->root, root_data);  // Roots Stack TOP
        }
        //Warning(info, "UNDEAD Roots top = %zu", loc->root->ref);
        /*if (!uf_sameset(shared->uf, loc->target->ref, loc->root->ref)) {
            uf_debug(shared->uf, loc->target->ref);
            uf_debug(shared->uf, loc->root->ref);
        }*/
        //T//HREassert(uf_sameset(shared->uf, loc->target->ref, loc->root->ref));
    }
    else {
        // Found w \in List(v) ==> push w on stack and search its successors

        state_info_set(ctx->state, v_p, LM_NULL_LATTICE);  // get state info
        state_data = dfs_stack_push (loc->dstack, NULL);
        state_info_serialize (ctx->state, state_data);
        explore_state (ctx);
        // TODO: do we need to add the worker ID??
        // - no, is sameset

        //Warning(info, "found unexplored state %zu in uf[%zu]", ctx->state->ref, v);
    }
    
}

void
ufscc_run  (run_t *run, wctx_t *ctx)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;


#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning(info, "Using the profiler");
    ProfilerStart("ufscc.perf");
#endif

    ufscc_init (ctx);


    // explore initial state
    state_data = dfs_stack_top (loc->dstack);
    state_info_deserialize (ctx->state, state_data); // DFS Stack TOP
    explore_state(ctx);

    while ( !run_is_stopped(run)) {
        if (0 == dfs_stack_nframes (loc->dstack))
            break;

        state_data = dfs_stack_top (loc->dstack);

        if (state_data != NULL) {
            // unexplored successor
            state_info_deserialize (ctx->state, state_data); // DFS Stack TOP
            successor(ctx);
        }
        else {
            // all successors explored/removed from stack -> backtrack
            backtrack(ctx);
        }
    }


#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning(info, "Done profiling");
    ProfilerStop();
#endif

    //print_worker_stats(ctx);

    //if (!run_is_stopped(run) && dfs_stack_size(loc->dstack) != 0)
    //    Warning (info, "Stack not empty: %zu ", dfs_stack_size(loc->dstack));
    

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

    reduced->unique_trans           += cnt->unique_trans;
    reduced->trans_tried            += cnt->trans_tried;
    reduced->unique_states          += cnt->unique_states;
    reduced->scc_count              += cnt->scc_count;
    reduced->self_loop_count        += cnt->self_loop_count;
    reduced->multiple_claim_count   += cnt->multiple_claim_count;
    reduced->marked_dead_count      += cnt->marked_dead_count;
    reduced->union_count            += cnt->union_count;
    reduced->union_success          += cnt->union_success;
    reduced->removed_from_list_count+= cnt->removed_from_list_count;
}

void
ufscc_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;
    //uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;

    // SCC statistics
    Warning(info,"unique states found:   %d", reduced->unique_states);
    Warning(info,"self-loop count:       %d", reduced->self_loop_count);
    //Warning(info,"correct count:         %d", correct_count);
    Warning(info,"scc count:             %d", reduced->scc_count);
    Warning(info,"avg scc size:          %.3f", ((double)reduced->unique_states) / reduced->scc_count);
    Warning(info,"re-explorations:       %.3f", ((double)run->total.explored) / reduced->unique_states);
    Warning(info,"re-tried transitions:  %.3f", ((double)reduced->trans_tried) / reduced->unique_trans);
    Warning(info,"unions:                %.3f (%.3f success)", ((double)reduced->unique_states) / reduced->union_count,
                                                               ((double)reduced->unique_states) / reduced->union_success);
    Warning(info," ");

    Warning(info,"UF memory usage:       %.3f MB (add to 'Est. total memory use')",
            ((double)reduced->unique_states) * sizeof(int[8]) / (1ULL << 20)); // TODO: hardcoded UFset node size

    Warning(info," ");
    //if (ctx->id==0) // TODO: make this more elegant (implement alg_destoy callback)
    //    uf_free(shared->uf);

    run_report_total (run);

    (void) ctx;
}

int
ufscc_state_seen (void *ptr, ref_t ref, int seen)
{
    wctx_t                 *ctx = (wctx_t *) ptr;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;
    return uf_owner (shared->uf, ref, ctx->id);
    (void) seen;
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

    set_alg_state_seen      (run->alg, ufscc_state_seen);

    run->shared                = RTmallocZero (sizeof (uf_alg_shared_t));
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) run->shared;
    shared->uf                 = uf_create();
}
