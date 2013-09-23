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
#include <ltsmin-lib/ltsmin-syntax.h>
#include <mc-lib/statistics.h>
#include <pins-lib/property-semantics.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <util-lib/fast_set.h>


extern struct poptOption reach_options[];

extern char            *act_detect;
extern char            *inv_detect;
extern int              dlk_detect;
extern int              no_exit;
extern size_t           max_level;

typedef struct counter_s {
    size_t              visited;        // counter: visited states
    size_t              explored;       // counter: explored states
    size_t              trans;          // counter: transitions
    size_t              level_max;      // max level
    size_t              level_cur;      // current level
    size_t              level_size;     // size of the SBFS level

    size_t              splits;         // Splits by LB
    size_t              transfer;       // load transfered by LB
    size_t              deadlocks;      // deadlock count
    size_t              violations;     // invariant violation count
    size_t              errors;         // assertion error count
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

    lts_file_t          lts;
    ltsmin_parse_env_t  env;
    ltsmin_expr_t       inv_expr;
};

struct alg_reduced_s {
    float               runtime;
    float               maxtime;
    statistics_t        state_stats;
    statistics_t        trans_stats;
    counter_t           counters;
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

extern void reach_reduce  (run_t *run, wctx_t *ctx);

extern void reach_print_stats  (run_t *run, wctx_t *ctx);

extern void handle_error_trace (wctx_t *ctx);

static inline void
deadlock_detect (wctx_t *ctx, size_t count)
{
    alg_local_t        *loc = ctx->local;
    if (count > 0) return;
    loc->counters.deadlocks++; // counting is costless
    if (GBstateIsValidEnd(ctx->model, ctx->state.data)) return;
    if ( !loc->inv_expr ) loc->counters.violations++;
    if (dlk_detect && (!no_exit || trc_output) && lb_stop(global->lb)) {
        Warning (info, " ");
        Warning (info, "Deadlock found in state at depth %zu!", loc->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
invariant_detect (wctx_t *ctx, raw_data_t state)
{
    alg_local_t        *loc = ctx->local;
    if ( !loc->inv_expr ||
         eval_predicate(ctx->model, loc->inv_expr, NULL, state, N, loc->env) ) return;
    loc->counters.violations++;
    if ((!no_exit || trc_output) && lb_stop(global->lb)) {
        Warning (info, " ");
        Warning (info, "Invariant violation (%s) found at depth %zu!", inv_detect, loc->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
action_detect (wctx_t *ctx, transition_info_t *ti, state_info_t *successor)
{
    alg_local_t        *loc = ctx->local;
    alg_global_t       *sm = ctx->global;
    if (-1 == act_index || NULL == ti->labels || ti->labels[act_label] != act_index) return;
    loc->counters.errors++;
    if ((!no_exit || trc_output) && lb_stop(global->lb)) {
        if (trc_output && successor->ref != ctx->state.ref) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        state_data_t data = dfs_stack_push (sm->stack, NULL);
        state_info_serialize (successor, data);
        dfs_stack_enter (sm->stack);
        Warning (info, " ");
        Warning (info, "Error action '%s' found at depth %zu!", act_detect, loc->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

#endif // REACH_H
