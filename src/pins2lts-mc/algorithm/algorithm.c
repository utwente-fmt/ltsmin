/**
 *
 */

#include <hre/config.h>

#include <stdlib.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/cctables.h>
#include <pins-lib/pins-impl.h>
#include <pins2lts-mc/algorithm/algorithm_object.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>

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
           ((Strat_DFS & s) && proviso == Proviso_Stack ? 1 : 0) )));
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
alg_state_new_default (void *ctx, ref_t ref, int seen)
{
    return seen;
    (void) ref; (void) ctx;
}

void
alg_global_init  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_global_init != NULL, "Not implemented %s.alg_global_init", run->alg->procs.type_name);
    run->alg->procs.alg_global_init (run, ctx);

    lb_local_init (global->lb, ctx->id, ctx); // Barrier
}

void
alg_local_init   (run_t *run, wctx_t *ctx)
{
    state_info_create_empty (&ctx->state);
    ctx->store = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2); //TODO
    ctx->store2 = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->permute = permute_create (permutation, ctx->model, run->alg->procs.alg_state_seen, W, K,
                                   ctx->id);
    HREassert (run->alg->procs.alg_local_init != NULL, "Not implemented %s.alg_local_init", run->alg->procs.type_name);
    run->alg->procs.alg_local_init (run, ctx);
}

void
alg_run          (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_run != NULL, "Not implemented %s.alg_run", run->alg->procs.type_name);

    RTstartTimer (ctx->timer);
    run->alg->procs.alg_run (run, ctx);
    RTstopTimer (ctx->timer);
}

void
alg_reduce  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_reduce != NULL, "Not implemented %s.alg_reduce", run->alg->procs.type_name);
    run->alg->procs.alg_reduce (run, ctx);

    stats_t            *stats = statistics (global->dbs);
    add_stats (stats);
}

void
alg_print_stats  (run_t *run, wctx_t *ctx)
{
    HREassert (run->alg->procs.alg_print_stats != NULL,
               "Not implemented %s.alg_print_stats", run->alg->procs.type_name);
    run->alg->procs.alg_print_stats (run, ctx);

    // Print memory statistics
    double              mem1, mem2, mem3=0, mem4, compr, fill, leafs;
    size_t              db_elts = global->stats.elts;
    size_t              db_nodes = global->stats.nodes;
    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    double              el_size =
       db_type & Tree ? (db_type==ClearyTree?1:2) + (2.0 / (1UL<<ratio)) : D+.5;
    size_t              s = state_info_size();
    size_t              max_load;

    mem1 = ((double)(s * max_load)) / (1 << 20);
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.2fMB",
             s, max_load, mem1);
    mem2 = ((double)(1UL << (dbs_size)) / (1<<20)) * SLOT_SIZE * el_size;
    mem4 = ((double)(db_nodes * SLOT_SIZE * el_size)) / (1<<20);
    fill = (double)((db_elts * 100) / (1UL << dbs_size));
    if (db_type & Tree) {
        compr = (double)(db_nodes * el_size) / ((D+1) * db_elts) * 100;
        leafs = (double)(((db_nodes - db_elts) * 100) / (1UL << (dbs_size-ratio)));
        Warning (info, "Tree memory: %.1fMB, compr.: %.1f%%, fill (roots/leafs): "
                "%.1f%%/%.1f%%", mem4, compr, fill, leafs);
    } else {
        Warning (info, "Table memory: %.1fMB, fill ratio: %.1f%%", mem4, fill);
    }
    double chunks = cct_print_stats (info, infoLong, GBgetLTStype(ctx->model),
                                     global->tables) / (1<<20);
    Warning (info, "Est. total memory use: %.1fMB (~%.1fMB paged-in)",
             mem1 + mem4 + mem3 + chunks, mem1 + mem2 + mem3 + chunks);
}

void
alg_destroy_local      (run_t *run, wctx_t *ctx)
{
    // wctx destroy:
    RTfree (ctx->store);
    RTfree (ctx->store2);

    permute_free (ctx->permute);
    HREassert (run->alg->procs.alg_destroy_local != NULL, "Not implemented %s.alg_destroy_local", run->alg->procs.type_name);
    run->alg->procs.alg_destroy_local (run, ctx);
}

void
alg_destroy      (run_t *run, wctx_t *ctx)
{
    RTfree (ctx);

    HREassert (run->alg->procs.alg_destroy != NULL, "Not implemented %s.alg_destroy", run->alg->procs.type_name);
    run->alg->procs.alg_destroy (run, ctx);
}

void
alg_shared_init_strategy      (run_t *run, strategy_t strat)
{
    run->alg->strategy = strat;
    run->alg->procs.alg_state_seen = alg_state_new_default; // default
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
    default: Abort ("Strategy (%s) is unknown or incompatible with the current "
                    "language module.", key_search(strategies, strategy[0]));
    }
}

run_t *
alg_create_no_init  ()
{
    HREassert (NULL == 0);
    run_t              *run = RTmallocZero (sizeof(run_t));
    run->alg = RTmallocZero (sizeof(alg_t));
    run->alg->procs.type_name = "DUMMY";
    return run;
}

run_t *
alg_create      ()
{
    run_t              *run = alg_create_no_init ();
    alg_shared_init_strategy      (run, strategy[0]);
    return run;
}

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
set_alg_destroy             (alg_t *alg, alg_destroy_f alg_destroy)
{
    alg->procs.alg_destroy = alg_destroy;
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

void
set_alg_destroy_local      (alg_t *alg, alg_destroy_local_f alg_dl)
{
    alg->procs.alg_destroy_local = alg_dl;
}
