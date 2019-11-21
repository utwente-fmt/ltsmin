/**
 * By rule:
 *
 * o Memory allocation happens prior to algorithm execution.
 *   Except for chunks whose allcoation is carefully managed by the cctables.
 *
 * o
 *
 * o Local (de)initialization can access data globally initialized
 *   but not vice versa.
 */

#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <popt.h>
#include <stdlib.h>

/* HEADERS USED BY SUB CLASSES */
#include <mc-lib/atomics.h>
#include <mc-lib/stats.h>
#include <mc-lib/trace.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins2lts-mc/parallel/counter.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/fast_hash.h>
#include <util-lib/is-balloc.h>
#include <util-lib/util.h>

/**
 * Class functionality
 */

extern alg_t *alg_create            ();
extern void alg_shared_init_strategy(run_t *run, strategy_t strategy);
extern void alg_global_init         (run_t *run, wctx_t *ctx);
extern void alg_local_init          (run_t *run, wctx_t *ctx);
extern void alg_run                 (run_t *run, wctx_t *ctx);
extern void alg_reduce              (run_t *run, wctx_t *ctx);
extern void alg_print_stats         (run_t *run, wctx_t *ctx);
extern void alg_local_deinit        (run_t *run, wctx_t *ctx);
extern void alg_global_deinit       (run_t *run, wctx_t *ctx);
extern void alg_destroy             (alg_t *alg);

/**
 * Function type definitions
 */

typedef void    (*alg_global_init_f)(run_t *run, wctx_t *ctx);
typedef void    (*alg_local_init_f) (run_t *run, wctx_t *ctx);
typedef void    (*alg_run_f)        (run_t *run, wctx_t *ctx);
typedef void    (*alg_reduce_f)     (run_t *run, wctx_t *ctx);
typedef void    (*alg_print_stats_f)(run_t *run, wctx_t *ctx);
typedef void    (*alg_global_deinit_f)  (run_t *run, wctx_t *ctx);
typedef void    (*alg_local_deinit_f)   (run_t *run, wctx_t *ctx);
typedef size_t  (*alg_global_bits_f)(run_t *run, wctx_t *ctx);

/**
 * Function setters
 */

extern void set_alg_local_init      (alg_t *alg,
                                     alg_local_init_f alg_local_init);
extern void set_alg_global_init     (alg_t *alg,
                                     alg_global_init_f alg_global_init);
extern void set_alg_global_deinit   (alg_t *alg, alg_global_deinit_f alg_destroy);
extern void set_alg_local_deinit    (alg_t *alg, alg_local_deinit_f alg_dl);
extern void set_alg_print_stats     (alg_t *alg,
                                     alg_print_stats_f alg_print_stats);
extern void set_alg_run             (alg_t *alg, alg_run_f alg_run);
extern void set_alg_state_seen      (alg_t *alg, alg_state_seen_f ssf);
extern void set_alg_reduce          (alg_t *alg, alg_reduce_f reduce);

/**
 * Function getters
 */

extern alg_state_seen_f get_alg_state_seen          (alg_t *alg);

/**
 * Child initializers
 */

extern void timed_shared_init       (run_t *run);
extern void ta_cndfs_shared_init    (run_t *run);
extern void reach_shared_init       (run_t *run);
extern void ndfs_shared_init        (run_t *run);
extern void lndfs_shared_init       (run_t *run);
extern void cndfs_shared_init       (run_t *run);
extern void owcty_shared_init       (run_t *run);
extern void dfs_fifo_shared_init    (run_t *run);
extern void tarjan_shared_init      (run_t *run);
extern void ufscc_shared_init       (run_t *run);
extern void renault_shared_init     (run_t *run);

/**
 * Helper functions
 */

extern int alg_state_new_default    (void *ctx, transition_info_t *ti,
                                     ref_t ref, int seen);

extern int num_global_bits (strategy_t s);

extern strategy_t get_strategy (alg_t *alg);

#endif // ALGORITHM_H
