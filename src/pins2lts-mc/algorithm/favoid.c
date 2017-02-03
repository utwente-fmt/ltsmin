/**
 *
 */

#include <hre/config.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/unionfind.h>
#include <mc-lib/iterset.h>
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
    uint32_t            fstates;
} counter_t;

/**
 * local SCC information (for each worker)
 */
struct alg_local_s {
    dfs_stack_t         search_stack;         // search stack (D)
    dfs_stack_t         roots_stack;          // roots stack (R)
    uint32_t            acc_mark;             // acceptance mark
    uint32_t            rabin_pair_id;        // Rabin Pair identifier
    uint32_t            rabin_pair_f;         // F fragment of rabin pair
    uint32_t            rabin_pair_i;         // I fragment of rabin pair
    counter_t          *cnt;                  // local counter per rabin pair
    state_info_t       *target;               // auxiliary state
    state_info_t       *root;                 // auxiliary state
    uint32_t            state_acc;            // acceptance info for ctx->state
    uint32_t            target_acc;           // acceptance info for target
    uint32_t            root_acc;             // acceptance info for root
    wctx_t             *rctx;                 // reachability for trace construction
};


/**
 * shared SCC information per Rabin pair (between workers)
 */
typedef struct uf_alg_shared_pair_s {
    uf_t               *uf;             // shared union-find structure
    iterset_t          *is;             // shared iteration set structure
} favoid_shared_pair_t;


/**
 * shared SCC information (between workers)
 */
typedef struct uf_alg_shared_s {
    favoid_shared_pair_t  *pairs;       // Shared structure per Rabin pair
} favoid_shared_t;


static void report_counterexample (wctx_t *ctx);

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
    
    ctx->local->target = state_info_create ();
    ctx->local->root   = state_info_create ();


    // extend state with acceptance marks information
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
    ctx->local->rabin_pair_f                = 0;
    ctx->local->rabin_pair_i                = 0;

    int n_pairs = GBgetRabinNPairs();
    ctx->local->cnt    = RTmalloc (sizeof (counter_t) * n_pairs );
    for (int i=0; i<n_pairs; i++) {
        ctx->local->cnt[i].scc_count               = 0;
        ctx->local->cnt[i].unique_states           = 0;
        ctx->local->cnt[i].unique_trans            = 0;
        ctx->local->cnt[i].selfloop                = 0;
        ctx->local->cnt[i].claimdead               = 0;
        ctx->local->cnt[i].claimfound              = 0;
        ctx->local->cnt[i].claimsuccess            = 0;
        ctx->local->cnt[i].cum_max_stack           = 0;
        ctx->local->cnt[i].fstates                 = 0;
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
    favoid_shared_t    *shared    = (favoid_shared_t*) ctx->run->shared;
    alg_local_t        *loc       = ctx->local;
    raw_data_t          stack_loc;
    uint32_t            acc_set   = 0;
    int                 pair_id   = loc->rabin_pair_id;

    // acceptance
    if (ti->labels != NULL) {

        acc_set = ti->labels[pins_get_accepting_set_edge_label_index(ctx->model)];

        // avoid and store successors of F transitions
        if (loc->rabin_pair_f & acc_set) {
            // add state to iterset
            if (iterset_add_state (shared->pairs[pair_id].is, successor->ref+1)) {
                loc->cnt[pair_id].fstates ++; // count only if newly added
            }
            return;
        }
        // continue normally with I transitions
        if (loc->rabin_pair_i & acc_set) {
        }
    }

    ctx->counters->trans++;

    // self-loop
    if (ctx->state->ref == successor->ref) {
        loc->cnt[pair_id].selfloop ++;
        uint32_t acc = uf_add_acc (shared->pairs[pair_id].uf, successor->ref + 1, acc_set);
        if ((loc->rabin_pair_i & acc) == loc->rabin_pair_i) {
            report_counterexample (ctx);
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
    state_info_deserialize (loc->target, stack_loc); // search_stack TOP
    loc->target_acc = acc_set;
    state_info_serialize (loc->target, stack_loc);

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
favoid_init  (wctx_t *ctx, ref_t init_state)
{
    alg_local_t        *loc    = ctx->local;
    favoid_shared_t    *shared = (favoid_shared_t*) ctx->run->shared;
    transition_info_t   ti     = GB_NO_TRANSITION;
    char                claim;
    size_t              transitions;
    raw_data_t          state_data;
    int                 pair_id   = loc->rabin_pair_id;

    // set initial state
    state_info_set (loc->target, init_state, LM_NULL_LATTICE);

    // make sure that state -> initial doesn't get recognized as a self loop
    ctx->state->ref = init_state + 1;
    favoid_handle (ctx, loc->target, &ti, 0);
    claim = uf_make_claim (shared->pairs[pair_id].uf, init_state + 1, ctx->id);
    
    state_data = dfs_stack_top (loc->search_stack);
    state_info_deserialize (ctx->state, state_data); // search_stack TOP
    transitions = explore_state(ctx);

    loc->cnt[pair_id].claimsuccess ++;
    if (claim == CLAIM_FIRST) {
        loc->cnt[pair_id].unique_states ++;
        loc->cnt[pair_id].unique_trans += transitions;
    }
}


/**
 * uses the top state from search_stack (== ctx->state) and explores it
 */
static inline void
successor (wctx_t *ctx)
{
    alg_local_t        *loc        = ctx->local;
    favoid_shared_t    *shared     = (favoid_shared_t*) ctx->run->shared;
    raw_data_t          state_data, root_data;
    char                claim;
    size_t              trans;
    int                 pair_id   = loc->rabin_pair_id;

    // get the parent state from the search_stack
    state_data = dfs_stack_peek_top (loc->search_stack, 1);
    state_info_deserialize (loc->target, state_data);

    // edge : FROM -> TO
    // FROM = parent    = loc->target
    // TO   = successor = ctx->state

    // early backtrack if parent is explored ==> all its children are explored
    if ( !uf_is_in_list (shared->pairs[pair_id].uf, loc->target->ref + 1) ) {
        dfs_stack_pop (loc->search_stack);
        return;
    }

    // make claim:
    // - CLAIM_FIRST   (initialized)
    // - CLAIM_SUCCESS (LIVE state and we have NOT yet visited its SCC)
    // - CLAIM_FOUND   (LIVE state and we have visited its SCC before)
    // - CLAIM_DEAD    (DEAD state)
    claim = uf_make_claim (shared->pairs[pair_id].uf, ctx->state->ref + 1, ctx->id);

    if (claim == CLAIM_DEAD) {
        // (TO == DEAD) ==> get next successor
        loc->cnt[pair_id].claimdead ++;
        dfs_stack_pop (loc->search_stack);
        return;
    }

    else if (claim == CLAIM_SUCCESS || claim == CLAIM_FIRST) {
        // (TO == 'new' state) ==> 'recursively' explore
        loc->cnt[pair_id].claimsuccess ++;

        trans = explore_state (ctx);

        if (claim == CLAIM_FIRST) {
            loc->cnt[pair_id].unique_states ++;
            loc->cnt[pair_id].unique_trans += trans;
        }
        return;
    }

    else  { // result == CLAIM_FOUND
        // (TO == state in previously visited SCC) ==> cycle found
        loc->cnt[pair_id].claimfound ++;

        Debug ("cycle: %zu  --> %zu", loc->target->ref, ctx->state->ref);

        if (uf_sameset (shared->pairs[pair_id].uf, loc->target->ref + 1, ctx->state->ref + 1)) {
            // add transition acceptance set
            uint32_t acc = uf_add_acc (shared->pairs[pair_id].uf, ctx->state->ref + 1, loc->state_acc);
            if ((loc->rabin_pair_i & acc) == loc->rabin_pair_i) {
                report_counterexample (ctx);
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
        uint32_t            acc_set   = loc->state_acc;
        do {
            root_data = dfs_stack_pop (loc->roots_stack); // UF Stack POP
            state_info_deserialize (loc->root, root_data); // roots_stack TOP

            // add the acceptance set from the previous root, not the current one
            // otherwise we could add the acceptance set for the edge
            // betweem two SCCs (which cannot be part of a cycle)
            uf_add_acc (shared->pairs[pair_id].uf, loc->root->ref + 1, acc_set);
            acc_set = loc->root_acc;
            Debug ("Uniting: %zu and %zu", loc->root->ref, loc->target->ref);

            uf_union (shared->pairs[pair_id].uf, loc->root->ref + 1, loc->target->ref + 1);

        } while ( !uf_sameset (shared->pairs[pair_id].uf, loc->target->ref + 1, ctx->state->ref + 1) );
        dfs_stack_push (loc->roots_stack, root_data);

        // after uniting SCC, report lasso
        acc_set = uf_get_acc (shared->pairs[pair_id].uf, ctx->state->ref + 1);
        if ((loc->rabin_pair_i & acc) == loc->rabin_pair_i) {
            report_counterexample (ctx);
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
    favoid_shared_t    *shared        = (favoid_shared_t*) ctx->run->shared;
    raw_data_t          state_data;
    bool                is_last_state;
    ref_t               state_picked;
    char                pick;
    raw_data_t          root_data;
    int                 pair_id   = loc->rabin_pair_id;

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
    uf_remove_from_list (shared->pairs[pair_id].uf, ctx->state->ref + 1);

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
         && uf_sameset (shared->pairs[pair_id].uf, loc->target->ref + 1, ctx->state->ref + 1) ) {
        return; // backtrack in same set
    }

    // ctx->state is the last KNOWN state in its SCC (according to this worker)
    // ==> check if we can find another one with pick_from_list
    pick = uf_pick_from_list (shared->pairs[pair_id].uf, ctx->state->ref + 1, &state_picked);

    if (pick != PICK_SUCCESS) {
        // list is empty ==> SCC is completely explored

        // were we the one that marked it dead?
        if (pick == PICK_MARK_DEAD) {
            loc->cnt[pair_id].scc_count ++;
        }

        // the SCC of the initial state has been marked DEAD ==> we are done!
        if (is_last_state) {
            return;
        }

        // we may still have states on the stack of this SCC
        if ( uf_sameset (shared->pairs[pair_id].uf, loc->target->ref + 1, ctx->state->ref + 1) ) {
            return; // backtrack in same set 
            // (the state got marked dead AFTER the previous sameset check)
        }

        // pop states from Roots until !sameset (v, Roots.TOP)
        //  since Roots.TOP might not be the actual root of the SCC
        root_data = dfs_stack_top (loc->roots_stack);
        state_info_deserialize (loc->root, root_data); // R.TOP
        // pop from Roots
        while (uf_sameset (shared->pairs[pair_id].uf, ctx->state->ref + 1, loc->root->ref + 1) ) {
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
favoid_check_pair_aux (wctx_t *ctx, run_t *run, ref_t init_state)
{
    alg_local_t        *loc       = ctx->local;
    raw_data_t          state_data;
    
    favoid_init (ctx, init_state);

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


static bool
favoid_check_pair (wctx_t *ctx, run_t *run)
{
    alg_local_t            *loc       = ctx->local;
    favoid_shared_t        *shared    = (favoid_shared_t*) ctx->run->shared;
    int                     pair_id   = loc->rabin_pair_id;
    ref_t                   init_state = ctx->initial->ref;

    //Warning(info, "checking pair %d", loc->rabin_pair_id);

    // search from the initial state
    //Warning (info, "Starting initial search from %zu", ctx->initial->ref);
    if (favoid_check_pair_aux (ctx, run, init_state)) return true;


    // check all states that we have avoided
    while (!iterset_is_empty (shared->pairs[pair_id].is)) {

        if (run_is_stopped(run)) 
            return false;
        
        ref_t new_init;
        iterset_pick_state (shared->pairs[pair_id].is, &new_init);
        new_init --; // iterset uses ref_t + 1

        if (uf_is_dead (shared->pairs[pair_id].uf, new_init+1)) {
            //Warning (info, "F state %zu is dead, disregard", new_init);
            continue;
        }

        //Warning (info, "Starting new search from %zu", new_init);

        if (favoid_check_pair_aux (ctx, run, new_init)) return true;

        iterset_remove_state (shared->pairs[pair_id].is, new_init+1);
    }

    return false;
}


/**
 * Get rabin info and check only one pair at the time
 */
void
favoid_run  (run_t *run, wctx_t *ctx)
{

#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning (info, "Using the profiler");
    ProfilerStart ("favoid.perf");
#endif

    alg_local_t            *loc = ctx->local;
    raw_data_t              state_data;


    int number_of_pairs = GBgetRabinNPairs();
    int start_pair = ctx->id % number_of_pairs;

    for (int i=0; i<number_of_pairs; i++) {

        if (run_is_stopped(run)) return;

        // set the current pair id
        loc->rabin_pair_id = (start_pair + i) % number_of_pairs;
        loc->rabin_pair_f  = GBgetRabinPairFin(loc->rabin_pair_id);
        loc->rabin_pair_i  = GBgetRabinPairInf(loc->rabin_pair_id);

        if (favoid_check_pair(ctx, run)) {
            report_counterexample (ctx);
        }

        if (i+1 < number_of_pairs) {
            // reset the local stacks
            dfs_stack_clear (loc->search_stack);
            dfs_stack_clear (loc->roots_stack);
        }
    }

#if HAVE_PROFILER
    if (ctx->id == 0)
        Warning(info, "Done profiling");
    ProfilerStop();
#endif

    // TODO: add counterexample construction

    (void) run;
    (void) state_data;
    (void) loc;
}


void
favoid_reduce (run_t *run, wctx_t *ctx)
{
    int n_pairs = GBgetRabinNPairs();
    if (run->reduced == NULL) {
        run->reduced = RTmallocZero (sizeof (counter_t) * n_pairs);
    }
    counter_t          *reduced   = (counter_t *) run->reduced;
    alg_local_t        *loc       = ctx->local;

    for (int pair_id=0; pair_id<n_pairs; pair_id++) {
        reduced[pair_id].unique_trans           += loc->cnt[pair_id].unique_trans;
        reduced[pair_id].unique_states          += loc->cnt[pair_id].unique_states;
        reduced[pair_id].scc_count              += loc->cnt[pair_id].scc_count;
        reduced[pair_id].selfloop               += loc->cnt[pair_id].selfloop;
        reduced[pair_id].claimdead              += loc->cnt[pair_id].claimdead;
        reduced[pair_id].claimfound             += loc->cnt[pair_id].claimfound;
        reduced[pair_id].claimsuccess           += loc->cnt[pair_id].claimsuccess;
        reduced[pair_id].fstates                += loc->cnt[pair_id].fstates;
        reduced[pair_id].cum_max_stack          += ctx->counters->level_max;
    }
}

void
favoid_print_stats   (run_t *run, wctx_t *ctx)
{
    counter_t              *reduced = (counter_t *) run->reduced;
    int n_pairs = GBgetRabinNPairs();

    uint32_t scc_count = 0;
    uint32_t unique_states = 0;
    uint32_t unique_trans = 0;
    uint32_t selfloop = 0;
    uint32_t claimdead = 0;
    uint32_t claimfound = 0;
    uint32_t claimsuccess = 0;
    uint32_t cum_max_stack = 0;
    uint32_t fstates = 0;

    for (int i=0; i<n_pairs; i++) {
        scc_count += reduced[i].scc_count;
        unique_states += reduced[i].unique_states;
        unique_trans += reduced[i].unique_trans;
        selfloop += reduced[i].selfloop;
        claimdead += reduced[i].claimdead;
        claimfound += reduced[i].claimfound;
        claimsuccess += reduced[i].claimsuccess;
        if (reduced[i].cum_max_stack > cum_max_stack) 
        cum_max_stack = reduced[i].cum_max_stack;
        fstates += reduced[i].fstates;
    }

    /*for (int i=0; i<n_pairs; i++) {
        Warning(info, "[%d] total scc count:            %d", i, reduced[i].scc_count);
        Warning(info, "[%d] unique states count:        %d", i, reduced[i].unique_states);
        Warning(info, "[%d] unique transitions count:   %d", i, reduced[i].unique_trans);
        Warning(info, "[%d] - self-loop count:          %d", i, reduced[i].selfloop);
        Warning(info, "[%d] - claim dead count:         %d", i, reduced[i].claimdead);
        Warning(info, "[%d] - claim found count:        %d", i, reduced[i].claimfound);
        Warning(info, "[%d] - claim success count:      %d", i, reduced[i].claimsuccess);
        Warning(info, "[%d] - cum. max stack depth:     %d", i, reduced[i].cum_max_stack);
        Warning(info, "[%d] F states count:             %d", i, reduced[i].fstates);
        Warning(info, " ");
    }*/

    Warning(info, "total scc count:            %d", scc_count);
    Warning(info, "unique states count:        %d", unique_states);
    Warning(info, "unique transitions count:   %d", unique_trans);
    Warning(info, "- self-loop count:          %d", selfloop);
    Warning(info, "- claim dead count:         %d", claimdead);
    Warning(info, "- claim found count:        %d", claimfound);
    Warning(info, "- claim success count:      %d", claimsuccess);
    Warning(info, "- cum. max stack depth:     %d", cum_max_stack);
    Warning(info, "F states count:             %d", fstates);
    Warning(info, " ");

    run_report_total (run);

    (void) ctx;
}


int
favoid_state_seen (void *ptr, transition_info_t *ti, ref_t ref, int seen)
{
    wctx_t             *ctx       = (wctx_t *) ptr;
    favoid_shared_t    *shared    = (favoid_shared_t*) ctx->run->shared;
    alg_local_t        *loc       = ctx->local;
    int                 pair_id   = loc->rabin_pair_id;

    return uf_owner (shared->pairs[pair_id].uf, ref + 1, ctx->id);
    (void) seen; (void) ti;
}


void
favoid_shared_init   (run_t *run)
{
    HREassert (PINS_BUCHI_TYPE == PINS_BUCHI_TYPE_RABIN, 
        "The F avoidance algorithm can only be used for Rabin automata");

    favoid_shared_t    *shared;

    set_alg_local_init    (run->alg, favoid_local_init);
    set_alg_global_init   (run->alg, favoid_global_init);
    set_alg_global_deinit (run->alg, favoid_global_deinit); 
    set_alg_local_deinit  (run->alg, favoid_local_deinit);
    set_alg_print_stats   (run->alg, favoid_print_stats);
    set_alg_run           (run->alg, favoid_run);
    set_alg_reduce        (run->alg, favoid_reduce);
    set_alg_state_seen    (run->alg, favoid_state_seen);


    int n_pairs = GBgetRabinNPairs();

    run->shared   = RTmallocZero (sizeof (favoid_shared_t));
    shared        = (favoid_shared_t*) run->shared;
    shared->pairs = RTmalloc (sizeof (favoid_shared_pair_t) * n_pairs);

    for (int i=0; i<n_pairs; i++) {
        shared->pairs[i].uf = uf_create();
        shared->pairs[i].is = iterset_create();
    }
}


static void 
report_counterexample   (wctx_t *ctx)
{
    int master = run_stop (ctx->run);
    if (master) {
        Warning (info, " ");
        Warning (info, "Accepting cycle FOUND!")
        Warning (info, " ");
    }
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
}
