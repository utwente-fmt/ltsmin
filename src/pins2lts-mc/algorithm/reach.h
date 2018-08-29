/*
 * Reachability algorithms for multi-core model checking.
 *
 *  @incollection {springerlink:10.1007/978-3-642-20398-5_40,
     author = {Laarman, Alfons and van de Pol, Jaco and Weber, Michael},
     affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
     title = {{Multi-Core LTSmin: Marrying Modularity and Scalability}},
     booktitle = {NASA Formal Methods},
     series = {Lecture Notes in Computer Science},
     editor = {Bobaru, Mihaela and Havelund, Klaus and Holzmann, Gerard and Joshi, Rajeev},
     publisher = {Springer Berlin / Heidelberg},
     isbn = {978-3-642-20397-8},
     keyword = {Computer Science},
     pages = {506-511},
     volume = {6617},
     url = {http://eprints.eemcs.utwente.nl/20004/},
     note = {10.1007/978-3-642-20398-5_40},
     year = {2011}
   }
 */

#ifndef REACH_H
#define REACH_H

#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <mc-lib/lb.h>
#include <mc-lib/statistics.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <util-lib/fast_set.h>


extern struct poptOption reach_options[];

extern char            *act_detect;
extern char            *inv_detect;
extern int              dlk_detect;
extern size_t           max_level;

typedef struct counter_s {
    size_t              level_size;     // size of the SBFS level
    size_t              splits;         // Splits by LB
    size_t              transfer;       // load transfered by LB
    size_t              deadlocks;      // deadlock count
    size_t              violations;     // invariant violation count
    size_t              errors;         // assertion error count
    size_t              ignoring;       // times the ignoring proviso was fulfilled
} counter_t;

struct alg_global_s {
    dfs_stack_t         stack;          // Successor stack (for BFS and DFS)
    dfs_stack_t         in_stack;       // Input stack (for BFS)
    dfs_stack_t         out_stack;      // Output stack (for BFS)
    isb_allocator_t    *queues;         // PBFS queues
};

struct alg_local_s {
    counter_t           counters;       // Stats counters
    fset_t             *cyan;           // Proviso stack
    int                 flip;           // PBFS queue counter
    int                 proviso;        // proviso check

    lts_file_t          lts;
    ltsmin_parse_env_t  env;
    ltsmin_expr_t       inv_expr;
};

struct alg_reduced_s {
    counter_t           counters;
    statistics_t        state_stats;
    statistics_t        trans_stats;
};

struct alg_shared_s {
    lb_t               *lb;             // Load balancer
    size_t              max_level_size;
    size_t              total_explored; // used for level synchronizing BFSs (sbfs and pbfs)
    ref_t              *parent_ref;     // trace reconstruction
};

extern void pbfs_queue_state (wctx_t *ctx, state_info_t *successor);

extern ssize_t split_bfs (void *arg_src, void *arg_tgt, size_t handoff);
extern ssize_t split_dfs (void *arg_src, void *arg_tgt, size_t handoff);
extern ssize_t split_sbfs (void *arg_src, void *arg_tgt, size_t handoff);

extern size_t sbfs_level (wctx_t *ctx, size_t local_size);

extern size_t in_load (alg_global_t *sm);

extern size_t bfs_load (alg_global_t *sm);

extern void reach_local_setup   (run_t *alg, wctx_t *ctx);

extern void reach_global_setup   (run_t *alg, wctx_t *ctx);

extern void reach_destroy   (run_t *run, wctx_t *ctx);

extern void reach_destroy_local      (run_t *run, wctx_t *ctx);

extern void reach_reduce  (run_t *run, wctx_t *ctx);

extern void reach_print_stats  (run_t *run, wctx_t *ctx);

extern void reach_init_shared (run_t *run);

extern void handle_error_trace (wctx_t *ctx);

static inline void
deadlock_detect (wctx_t *ctx, size_t count)
{
    if (EXPECT_FALSE(ctx->counter_example)) {
        run_stop (ctx->run);
        alg_global_t        *sm = ctx->global;
        raw_data_t          data = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (ctx->ce_state, data);
        dfs_stack_enter (sm->stack);

        handle_error_trace (ctx);
        return;
    }

    if (EXPECT_TRUE(count > 0))
        return;

    alg_local_t        *loc = ctx->local;
    loc->counters.deadlocks++; // counting is costless
    state_data_t        state = state_info_state (ctx->state);
    if (pins_state_is_valid_end(ctx->model, state)) return;
    if ( !loc->inv_expr ) loc->counters.violations++;

    if (dlk_detect)
        global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
    if (dlk_detect && (!no_exit || trc_output) && run_stop(ctx->run)) {
        Warning (info, " ");
        Warning (info, "Deadlock found in state at depth %zu!",
                 ctx->counters->level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
invariant_detect (wctx_t *ctx)
{
    alg_local_t        *loc = ctx->local;
    if (EXPECT_TRUE(!loc->inv_expr))
        return;

    state_data_t        state = state_info_state (ctx->state);
    if (EXPECT_TRUE(
            eval_state_predicate(ctx->model, loc->inv_expr, state, loc->env)))
        return;

    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
    loc->counters.violations++;
    if ((!no_exit || trc_output) && run_stop(ctx->run)) {
        Warning (info, " ");
        const char* violation = strcmp(inv_detect, LTSMIN_STATE_LABEL_ACCEPTING) == 0
            ? "Monitor" : "Invariant";
        Warning (info, "%s violation (%s) found at depth %zu!",
                 violation, inv_detect, ctx->counters->level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
action_detect (wctx_t *ctx, transition_info_t *ti, state_info_t *successor)
{
    if (EXPECT_TRUE(-1 == act_index))
        return;

    if (EXPECT_TRUE(NULL == ti->labels || ti->labels[act_label] != act_index))
        return;

    alg_local_t        *loc = ctx->local;

    loc->counters.errors++;
    if ((!no_exit || trc_output) && !ctx->counter_example) { // once
        Warning (info, " ");
        Warning (info, "Error action '%s' found at depth %zu!",
                 act_detect, ctx->counters->level_cur);
        Warning (info, " ");
        // GBgetTransitions is not necessarily reentrant,
        // so we handle trace writing and exit in deadlock_detect (above)
        ctx->counter_example = 1;
        state_info_set (ctx->ce_state, successor->ref, successor->lattice);
        /*handle_error_trace (ctx);*/
    }
}

#endif // REACH_H
