/**
 *
 */

#include <hre/config.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/unionfind.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins.h>
#include <pins2lts-mc/algorithm/ltl.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/util.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_set.h>

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
    uint32_t            selfloop;
    uint32_t            claimdead;
    uint32_t            claimfound;
    uint32_t            claimsuccess;
    uint32_t            cum_max_stack;
    uint32_t            ftrans;
    uint32_t            itrans;
} counter_t;

/**
 * local SCC information (for each worker)
 */
struct alg_local_s {
    dfs_stack_t         search_stack;         // search stack (D)
    dfs_stack_t         roots_stack;          // roots stack (R)
    uint32_t            acc_mark;             // acceptance mark
    uint32_t            rabin_pair_id;        // Rabin Pair identifier
    counter_t           cnt;
    state_info_t       *target;               // auxiliary state
    state_info_t       *root;                 // auxiliary state
    uint32_t            state_acc;            // acceptance info for ctx->state
    uint32_t            target_acc;           // acceptance info for target
    uint32_t            root_acc;             // acceptance info for root
    wctx_t             *rctx;                 // reachability for trace construction
};


/**
 * shared SCC information (between workers)
 */
typedef struct uf_alg_shared_s {
    uf_t               *uf;             // shared union-find structure
    ref_t               lasso_acc;      // SCC root for trace construction
    ref_t               lasso_end;      // last on lasso (to iterate backwards from)
    ref_t               lasso_root;     // last on lasso (to iterate backwards from)
    run_t              *reach_run;      // parallel reachability object
    bool                ltl;            // LTL property present?
} uf_alg_shared_t;

extern void report_lasso (wctx_t *ctx, ref_t accepting);
extern int reach_scc_seen (void *ext_ctx, transition_info_t *ti,
                           ref_t ref, int seen);

void
favoid_global_init (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
favoid_global_deinit (run_t *run, wctx_t *ctx)
{
    (void) run; (void) ctx;
}


void
favoid_local_init (run_t *run, wctx_t *ctx)
{
    ctx->local = RTmallocZero (sizeof (alg_local_t) );
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) ctx->run->shared;

    ctx->local->target = state_info_create ();
    ctx->local->root   = state_info_create ();

    // extend state with TGBA acceptance marks information
    state_info_add_simple (ctx->state, sizeof (uint32_t),
                          &ctx->local->state_acc);
    state_info_add_simple (ctx->local->target, sizeof (uint32_t),
                          &ctx->local->target_acc);
    state_info_add_simple (ctx->local->root, sizeof (uint32_t),
                          &ctx->local->root_acc);

    size_t len               = state_info_serialize_int_size (ctx->state);
    ctx->local->search_stack = dfs_stack_create (len);
    ctx->local->roots_stack  = dfs_stack_create (len);

    ctx->local->rabin_pair_id               = 0;

    ctx->local->cnt.scc_count               = 0;
    ctx->local->cnt.unique_states           = 0;
    ctx->local->cnt.unique_trans            = 0;
    ctx->local->cnt.selfloop                = 0;
    ctx->local->cnt.claimdead               = 0;
    ctx->local->cnt.claimfound              = 0;
    ctx->local->cnt.claimsuccess            = 0;
    ctx->local->cnt.cum_max_stack           = 0;
    ctx->local->cnt.ftrans                  = 0;
    ctx->local->cnt.itrans                  = 0;

    shared->ltl = pins_get_accepting_state_label_index(ctx->model) != -1;

    if (shared->ltl && trc_output) {
        ctx->local->rctx = run_init (shared->reach_run, ctx->model);
        ctx->local->rctx->parent = ctx;
    }

    (void) run; 
}


void
favoid_local_deinit   (run_t *run, wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;

    dfs_stack_destroy (loc->search_stack);
    dfs_stack_destroy (loc->roots_stack);
    RTfree (loc);
    (void) run;
}


static void
favoid_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    wctx_t             *ctx       = (wctx_t *) arg;
    uf_alg_shared_t    *shared    = (uf_alg_shared_t*) ctx->run->shared;
    alg_local_t        *loc       = ctx->local;
    raw_data_t          stack_loc;
    uint32_t            acc_set   = 0;

    // TGBA acceptance
    if (ti->labels != NULL && PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_RABIN) {
        acc_set = ti->labels[pins_get_accepting_set_edge_label_index(ctx->model)];
        if (GBgetRabinPairFin(loc->rabin_pair_id) & acc_set) {
            loc->cnt.ftrans ++;
        }
        if (GBgetRabinPairInf(loc->rabin_pair_id) & acc_set) {
            loc->cnt.itrans ++;
        }
    }

    ctx->counters->trans++;

    // self-loop
    if (ctx->state->ref == successor->ref) {
        loc->cnt.selfloop ++;
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
            uint32_t acc = uf_add_acc (shared->uf, successor->ref + 1, acc_set);
            if (GBgetAcceptingSet() == acc) {
                report_lasso (ctx, ctx->state->ref);
            }
        } else if (shared->ltl) { // BA
            if (pins_state_is_accepting(ctx->model, state_info_state(ctx->state)) ) {
                report_lasso (ctx, ctx->state->ref);
            }
        }
        return;
    } else if (EXPECT_FALSE(trc_output && !seen && ti != &GB_NO_TRANSITION)) {
        // use parent_ref from reachability (used in CE reconstuction)
        ref_t *succ_parent = get_parent_ref(loc->rctx, successor->ref);
        atomic_write (succ_parent, ctx->state->ref);
    }

    stack_loc = dfs_stack_push (loc->search_stack, NULL);
    state_info_serialize (successor, stack_loc);

    // add acceptance set to the state
    if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA) {
        state_info_deserialize (loc->target, stack_loc); // search_stack TOP
        loc->target_acc = acc_set;
        state_info_serialize (loc->target, stack_loc);
    }

    (void) ti;
}


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
    loc->root_acc = loc->state_acc;
    state_info_serialize (loc->root, stack_loc);

    increase_level (ctx->counters);
    dfs_stack_enter (loc->search_stack);

    trans = permute_trans (ctx->permute, ctx->state, favoid_handle, ctx);

    ctx->counters->explored ++;
    run_maybe_report1 (ctx->run, ctx->counters, "");

    return trans;
}


static void
favoid_init  (wctx_t *ctx)
{
    alg_local_t        *loc    = ctx->local;
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) ctx->run->shared;
    transition_info_t   ti     = GB_NO_TRANSITION;
    char                claim;
    size_t              transitions;
    raw_data_t          state_data;

    // make sure that state -> initial doesn't get recognized as a self loop
    ctx->state->ref = ctx->initial->ref + 1;
    favoid_handle (ctx, ctx->initial, &ti, 0);
    claim = uf_make_claim (shared->uf, ctx->initial->ref + 1, ctx->id);
    
    state_data = dfs_stack_top (loc->search_stack);
    state_info_deserialize (ctx->state, state_data); // search_stack TOP
    transitions = explore_state(ctx);

    loc->cnt.claimsuccess ++;
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
    if ( !uf_is_in_list (shared->uf, loc->target->ref + 1) ) {
        dfs_stack_pop (loc->search_stack);
        return;
    }

    // make claim:
    // - CLAIM_FIRST   (initialized)
    // - CLAIM_SUCCESS (LIVE state and we have NOT yet visited its SCC)
    // - CLAIM_FOUND   (LIVE state and we have visited its SCC before)
    // - CLAIM_DEAD    (DEAD state)
    claim = uf_make_claim (shared->uf, ctx->state->ref + 1, ctx->id);

    if (claim == CLAIM_DEAD) {
        // (TO == DEAD) ==> get next successor
        loc->cnt.claimdead ++;
        dfs_stack_pop (loc->search_stack);
        return;
    }

    else if (claim == CLAIM_SUCCESS || claim == CLAIM_FIRST) {
        // (TO == 'new' state) ==> 'recursively' explore
        loc->cnt.claimsuccess ++;

        trans = explore_state (ctx);

        if (claim == CLAIM_FIRST) {
            loc->cnt.unique_states ++;
            loc->cnt.unique_trans += trans;
        }
        return;
    }

    else  { // result == CLAIM_FOUND
        // (TO == state in previously visited SCC) ==> cycle found
        loc->cnt.claimfound ++;

        Debug ("cycle: %zu  --> %zu", loc->target->ref, ctx->state->ref);

        if (uf_sameset (shared->uf, loc->target->ref + 1, ctx->state->ref + 1)) {
            // add transition acceptance set
            if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
                uint32_t acc = uf_add_acc (shared->uf, ctx->state->ref + 1, loc->state_acc);
                if (GBgetAcceptingSet() == acc) {
                    report_lasso (ctx, ctx->state->ref);
                }
            }
            dfs_stack_pop (loc->search_stack);
            return; // also no chance of new accepting cycle
        }

        // we have:  .. -> TO  -> .. -> FROM -> TO
        // where D = .. -> TO  -> .. -> FROM
        // and   R = .. -> TO* -> .. -> FROM  (may have fewer states than D)
        //                                    (TO* is a state in SCC(TO))
        // ==> unite and pop states from R until sameset (R.TOP, TO)

        // while ( not sameset (FROM, TO) )
        //   Union (R.POP(), FROM)
        // R.PUSH (TO')
        ref_t               accepting = DUMMY_IDX;
        uint32_t            acc_set   = loc->state_acc;
        do {
            root_data = dfs_stack_pop (loc->roots_stack); // UF Stack POP
            state_info_deserialize (loc->root, root_data); // roots_stack TOP

            if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
                // add the acceptance set from the previous root, not the current one
                // otherwise we could add the acceptance set for the edge
                // betweem two SCCs (which cannot be part of a cycle)
                uf_add_acc (shared->uf, loc->root->ref + 1, acc_set);
                acc_set = loc->root_acc;
            } else if (shared->ltl && pins_state_is_accepting(ctx->model, state_info_state(loc->root))) {
                accepting = loc->root->ref;
            }
            Debug ("Uniting: %zu and %zu", loc->root->ref, loc->target->ref);

            uf_union (shared->uf, loc->root->ref + 1, loc->target->ref + 1);

        } while ( !uf_sameset (shared->uf, loc->target->ref + 1, ctx->state->ref + 1) );
        dfs_stack_push (loc->roots_stack, root_data);

        // after uniting SCC, report lasso
        if (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_TGBA && shared->ltl) {
            acc_set = uf_get_acc (shared->uf, ctx->state->ref + 1);
            if (GBgetAcceptingSet() == acc_set) {
                report_lasso (ctx, ctx->state->ref);
            }
        } else if (accepting != DUMMY_IDX) {
            report_lasso (ctx, accepting);
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
    uf_remove_from_list (shared->uf, ctx->state->ref + 1);

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
         && uf_sameset (shared->uf, loc->target->ref + 1, ctx->state->ref + 1) ) {
        return; // backtrack in same set
    }

    // ctx->state is the last KNOWN state in its SCC (according to this worker)
    // ==> check if we can find another one with pick_from_list
    pick = uf_pick_from_list (shared->uf, ctx->state->ref + 1, &state_picked);

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
        if ( uf_sameset (shared->uf, loc->target->ref + 1, ctx->state->ref + 1) ) {
            return; // backtrack in same set 
            // (the state got marked dead AFTER the previous sameset check)
        }

        // pop states from Roots until !sameset (v, Roots.TOP)
        //  since Roots.TOP might not be the actual root of the SCC
        root_data = dfs_stack_top (loc->roots_stack);
        state_info_deserialize (loc->root, root_data); // R.TOP
        // pop from Roots
        while (uf_sameset (shared->uf, ctx->state->ref + 1, loc->root->ref + 1) ) {
            dfs_stack_pop (loc->roots_stack); // R.POP
            root_data = dfs_stack_top (loc->roots_stack);
            state_info_deserialize (loc->root, root_data); // R.TOP
        }
    }
    else {
        // Found w in List(v) ==> push w on stack and search its successors
        state_info_set (ctx->state, state_picked - 1, LM_NULL_LATTICE);
        state_data = dfs_stack_push (loc->search_stack, NULL);
        loc->state_acc = 0; // the acceptance marks should already be stored in the SCC
        state_info_serialize (ctx->state, state_data);
        explore_state (ctx);
    }
}


static bool
favoid_check_pair(wctx_t *ctx, run_t *run)
{
    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;

    Warning(info, "checking pair %d", loc->rabin_pair_id);
    // GBgetRabinPairFin(i), GBgetRabinPairInf(i)
    
    favoid_init (ctx);

    // continue until we are done exploring the graph or interrupted
    while ( !run_is_stopped(run) ) {

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
    return false;
}


/**
 * Get rabin info and check only one pair at the time
 */
void
favoid_run  (run_t *run, wctx_t *ctx)
{
    Warning(info, " ");
    Warning(info, "Starting Rabin checking algorithm");
    Warning(info, " ");

    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;
    uf_alg_shared_t        *shared = (uf_alg_shared_t*) ctx->run->shared;
    counter_t              *cnt     = &loc->cnt;


    int number_of_pairs = GBgetRabinNPairs();
    int start_pair = ctx->id % number_of_pairs;

    for (int i=0; i<number_of_pairs; i++) {

        // set the current pair id
        loc->rabin_pair_id = (start_pair + i) % number_of_pairs;

        if (favoid_check_pair(ctx, run)) {
            Abort("Found Accepting Rabin cycle");
        }
        // prepare for next iteration: reset structure
        //if (i+1 < number_of_pairs) {
            // print info for the current round:
            Warning(info, "total scc count:            %d", cnt->scc_count);
            Warning(info, "unique states count:        %d", cnt->unique_states);
            Warning(info, "unique transitions count:   %d", cnt->unique_trans);
            Warning(info, "- self-loop count:          %d", cnt->selfloop);
            Warning(info, "- claim dead count:         %d", cnt->claimdead);
            Warning(info, "- claim found count:        %d", cnt->claimfound);
            Warning(info, "- claim success count:      %d", cnt->claimsuccess);
            Warning(info, "- cum. max stack depth:     %zu", ctx->counters->level_max);
            Warning(info, " ");
            Warning(info, "- F transition count:       %d", cnt->ftrans);
            Warning(info, "- I transition count:       %d", cnt->itrans);
            Warning(info, " ");

            // and reset the values
            ctx->local->cnt.scc_count               = 0;
            ctx->local->cnt.unique_states           = 0;
            ctx->local->cnt.unique_trans            = 0;
            ctx->local->cnt.selfloop                = 0;
            ctx->local->cnt.claimdead               = 0;
            ctx->local->cnt.claimfound              = 0;
            ctx->local->cnt.claimsuccess            = 0;
            ctx->local->cnt.cum_max_stack           = 0;
            ctx->local->cnt.ftrans                  = 0;
            ctx->local->cnt.itrans                  = 0;

            dfs_stack_clear (loc->search_stack);
            dfs_stack_clear (loc->search_stack);
            uf_clear(shared->uf);
        //}
    }
    //Abort("No Accepting Rabin cycle found");

    // TODO: add profiler info
    // TODO: add counterexample construction
    // TODO: what to do when one thread is finished?

    (void) run;
    (void) state_data;
    (void) loc;
}


void
favoid_reduce (run_t *run, wctx_t *ctx)
{
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t));
    }
    counter_t          *reduced = (counter_t *) run->reduced;
    counter_t          *cnt     = &ctx->local->cnt;

    reduced->unique_trans           += cnt->unique_trans;
    reduced->unique_states          += cnt->unique_states;
    reduced->scc_count              += cnt->scc_count;
    reduced->selfloop               += cnt->selfloop;
    reduced->claimdead              += cnt->claimdead;
    reduced->claimfound             += cnt->claimfound;
    reduced->claimsuccess           += cnt->claimsuccess;
    reduced->cum_max_stack          += ctx->counters->level_max;
}

void
favoid_print_stats   (run_t *run, wctx_t *ctx)
{
    /*counter_t              *reduced = (counter_t *) run->reduced;

    
    // SCC statistics
    Warning(info, "total scc count:            %d", reduced->scc_count);
    Warning(info, "unique states count:        %d", reduced->unique_states);
    Warning(info, "unique transitions count:   %d", reduced->unique_trans);
    Warning(info, "- self-loop count:          %d", reduced->selfloop);
    Warning(info, "- claim dead count:         %d", reduced->claimdead);
    Warning(info, "- claim found count:        %d", reduced->claimfound);
    Warning(info, "- claim success count:      %d", reduced->claimsuccess);
    Warning(info, "- cum. max stack depth:     %d", reduced->cum_max_stack);
    Warning(info, " ");
    */

    run_report_total (run);

    (void) ctx;
}


int
favoid_state_seen (void *ptr, transition_info_t *ti, ref_t ref, int seen)
{
    wctx_t             *ctx    = (wctx_t *) ptr;
    uf_alg_shared_t    *shared = (uf_alg_shared_t*) ctx->run->shared;

    return uf_owner (shared->uf, ref + 1, ctx->id);
    (void) seen; (void) ti;
}


void
favoid_shared_init   (run_t *run)
{
    uf_alg_shared_t    *shared;

    set_alg_local_init    (run->alg, favoid_local_init);
    set_alg_global_init   (run->alg, favoid_global_init);
    set_alg_global_deinit (run->alg, favoid_global_deinit); 
    set_alg_local_deinit  (run->alg, favoid_local_deinit);
    set_alg_print_stats   (run->alg, favoid_print_stats);
    set_alg_run           (run->alg, favoid_run);
    set_alg_reduce        (run->alg, favoid_reduce);
    set_alg_state_seen    (run->alg, favoid_state_seen);

    run->shared = RTmallocZero (sizeof (uf_alg_shared_t));
    shared      = (uf_alg_shared_t*) run->shared;
    shared->uf  = uf_create();

    if (trc_output) {
        // Prepare parallel reachability (should be done in shared, .i.e. global and once)
        if (strategy[1] == Strat_None) strategy[1] = Strat_DFS;
        shared->reach_run = run_create (false);
        alg_shared_init_strategy (shared->reach_run, strategy[1]);
        set_alg_state_seen (shared->reach_run->alg, reach_scc_seen);
    }
}
