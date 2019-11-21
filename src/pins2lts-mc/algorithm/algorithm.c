/**
 *
 */

#include <hre/config.h>

#include <stdlib.h>

#include <pins2lts-mc/algorithm/algorithm_object.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/options.h>

struct alg_s {
    alg_obj_t           procs;
    strategy_t          strategy;
};

int
num_global_bits (strategy_t s)
{
    return (Strat_ENDFS  & s ? 3 :
           ((Strat_CNDFS | Strat_DFSFIFO) & s ? 2 :
           ((Strat_LNDFS | Strat_OWCTY | Strat_TA) & s ? 1 :
           ( Strat_TARJAN & s ? 1 :
           ((Strat_DFS & s) && proviso == Proviso_Stack ? 1 : 0) ))));
}

/*************************************************************************/
/* Wrapper functions.                                                    */
/*************************************************************************/

strategy_t
get_strategy (alg_t *alg)
{
    return alg->strategy;
}

int
alg_state_new_default (void *ctx, transition_info_t *ti, ref_t ref, int seen)
{
    return seen;
    (void) ref; (void) ctx; (void) ti;
}

void
alg_global_init  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_global_init != NULL,
               "Not implemented %s.alg_global_init", run->alg->procs.type_name);
    run->alg->procs.alg_global_init (run, ctx);
}

void
alg_local_init   (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_local_init != NULL,
               "Not implemented %s.alg_local_init", run->alg->procs.type_name);
    run->alg->procs.alg_local_init (run, ctx);
}

void
alg_run          (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_run != NULL,
               "Not implemented %s.alg_run", run->alg->procs.type_name);
    run->alg->procs.alg_run (run, ctx);
}

void
alg_reduce  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_reduce != NULL,
               "Not implemented %s.alg_reduce", run->alg->procs.type_name);
    run->alg->procs.alg_reduce (run, ctx);
}

void
alg_print_stats  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_print_stats != NULL,
               "Not implemented %s.alg_print_stats", run->alg->procs.type_name);
    run->alg->procs.alg_print_stats (run, ctx);
}

void
alg_local_deinit      (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_local_deinit != NULL,
               "Not implemented %s.alg_local_deinit", run->alg->procs.type_name);
    run->alg->procs.alg_local_deinit (run, ctx);
}

void
alg_global_deinit      (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_global_deinit != NULL,
               "Not implemented %s._deinit", run->alg->procs.type_name);
    run->alg->procs.alg_global_deinit (run, ctx);
}

void
alg_destroy         (alg_t *alg)
{
    RTfree (alg);
}

void
alg_shared_init_strategy      (run_t *run, strategy_t strat)
{
    run->alg->strategy = strat;
    run->alg->procs.alg_state_seen = alg_state_new_default; // default
    run->alg->procs.type_name = key_search (strategies, strat);
    switch (strat) {
    case Strat_TA_PBFS:
    case Strat_TA_SBFS:
    case Strat_TA_BFS:
    case Strat_TA_DFS:
        timed_shared_init (run);
        break;
    case Strat_TA_CNDFS:
        ta_cndfs_shared_init (run);
        break;
    case Strat_SBFS:
    case Strat_PBFS:
    case Strat_BFS:
    case Strat_DFS:
        reach_shared_init (run);
        break;
    case Strat_NDFS:
        ndfs_shared_init (run);
        break;
    case Strat_LNDFS:
        lndfs_shared_init (run);
        break;
    case Strat_CNDFS:
    case Strat_ENDFS:
        cndfs_shared_init (run);
        break;
    case Strat_OWCTY:
        owcty_shared_init (run);
        break;
    case Strat_DFSFIFO:
        dfs_fifo_shared_init (run);
        break;
    case Strat_TARJAN:
        tarjan_shared_init (run);
        break;
    case Strat_UFSCC:
        ufscc_shared_init (run);
        break;
    case Strat_RENAULT:
        renault_shared_init (run);
        break;
    default: Abort ("Strategy (%s) is unknown or incompatible with the current "
                    "language module.", key_search(strategies, strategy[0]));
    }
}

alg_t *
alg_create  ()
{
    HREassert (NULL == 0);
    alg_t               *alg = RTmallocZero (sizeof(alg_t));
    return alg;
}

/*************************************************************************/
/* Wrapper set functions.                                                */
/*************************************************************************/

void
set_alg_local_init          (alg_t *alg, alg_local_init_f alg_local_init)
{
    alg->procs.alg_local_init = alg_local_init;
}

void
set_alg_global_init         (alg_t *alg, alg_global_init_f alg_global_init)
{
    alg->procs.alg_global_init = alg_global_init;
}

void
set_alg_local_deinit        (alg_t *alg, alg_local_deinit_f alg_dl)
{
    alg->procs.alg_local_deinit = alg_dl;
}

void
set_alg_global_deinit       (alg_t *alg, alg_global_deinit_f alg_destroy)
{
    alg->procs.alg_global_deinit = alg_destroy;
}

void
set_alg_print_stats         (alg_t *alg, alg_print_stats_f alg_print_stats)
{
    alg->procs.alg_print_stats = alg_print_stats;
}

void
set_alg_run                 (alg_t *alg, alg_run_f alg_run)
{
    alg->procs.alg_run = alg_run;
}

/**
 * Fresh successor heuristic implementation.
 * Used by permutor.
 *
 * Return a positive integer when a state has been visited locally
 * Return a negative integer when a state has only been visited by another worker
 * Or zero when unknown.
 */
alg_state_seen_f
get_alg_state_seen          (alg_t *alg)
{
    return alg->procs.alg_state_seen;
}

void
set_alg_state_seen          (alg_t *alg, alg_state_seen_f ssf)
{
    alg->procs.alg_state_seen = ssf;
}

void
set_alg_reduce              (alg_t *alg, alg_run_f alg_reduce)
{
    alg->procs.alg_reduce = alg_reduce;
}
