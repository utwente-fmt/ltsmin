/**
 *
 */

#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <popt.h>
#include <stdlib.h>

#include <mc-lib/atomics.h>
#include <mc-lib/lb.h>
#include <mc-lib/stats.h>
#include <mc-lib/trace.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/fast_hash.h>
#include <util-lib/is-balloc.h>

extern int num_global_bits (strategy_t s);
extern strategy_t get_strategy (alg_t *alg);

extern run_t *alg_create_no_init    ();
extern run_t *alg_create            ();
extern run_t *run_create_no_init    ();
extern void alg_shared_init_strategy(run_t *run, strategy_t strategy);
extern void alg_local_init          (run_t *run, wctx_t *ctx);
extern void alg_global_init         (run_t *run, wctx_t *ctx);
extern void alg_run                 (run_t *run, wctx_t *ctx);
extern void alg_reduce              (run_t *run, wctx_t *ctx);
extern void alg_print_stats         (run_t *run, wctx_t *ctx);
extern void alg_destroy_local       (run_t *run, wctx_t *ctx);
extern void alg_destroy             (run_t *run, wctx_t *ctx);

typedef void    (*alg_global_init_f)(run_t *run, wctx_t *ctx);
typedef void    (*alg_local_init_f) (run_t *run, wctx_t *ctx);
typedef void    (*alg_run_f)        (run_t *run, wctx_t *ctx);
typedef void    (*alg_reduce_f)     (run_t *run, wctx_t *ctx);
typedef void    (*alg_print_stats_f)(run_t *run, wctx_t *ctx);
typedef void    (*alg_destroy_f)    (run_t *run, wctx_t *ctx);
typedef void    (*alg_destroy_local_f) (run_t *run, wctx_t *ctx);
typedef size_t  (*alg_global_bits_f)(run_t *run, wctx_t *ctx);
typedef int     (*alg_state_seen_f) (void *ctx, ref_t ref, int seen);

extern void set_alg_local_init      (alg_t *alg,
                                     alg_local_init_f alg_local_init);
extern void set_alg_global_init     (alg_t *alg,
                                     alg_global_init_f alg_global_init);
extern void set_alg_destroy         (alg_t *alg, alg_destroy_f alg_destroy);
extern void set_alg_destroy_local   (alg_t *alg, alg_destroy_local_f alg_dl);
extern void set_alg_print_stats     (alg_t *alg,
                                     alg_print_stats_f alg_print_stats);
extern void set_alg_run             (alg_t *alg, alg_run_f alg_run);
extern void set_alg_state_seen      (alg_t *alg, alg_state_seen_f ssf);
extern void set_alg_reduce          (alg_t *alg, alg_reduce_f reduce);

extern void timed_shared_init       (run_t *run);
extern void ta_cndfs_shared_init    (run_t *run);
extern void reach_shared_init       (run_t *run);
extern void ndfs_shared_init        (run_t *run);
extern void lndfs_shared_init       (run_t *run);
extern void cndfs_shared_init       (run_t *run);
extern void owcty_shared_init       (run_t *run);
extern void dfs_fifo_shared_init    (run_t *run);


extern int alg_state_new_default    (void *ctx, ref_t ref, int seen);

extern void find_and_write_dfs_stack_trace (wctx_t *ctx, int level); // TODO


#include <pins2lts-mc/parallel/global.h>

static inline void
increase_level (size_t *level_cur, size_t *level_max)
{
    (*level_cur)++;
    if (*level_cur > *level_max) {
        *level_max = *level_cur;
    }
}

static inline void
maybe_report (size_t explored, size_t trans, size_t level_max, char *msg)
{
    if (EXPECT_TRUE(!log_active(info) || explored < global->threshold))
        return;
    if (!cas (&global->threshold, global->threshold, global->threshold << 1))
        return;
    if (W == 1) {
        Warning (info, "%s%zu levels %zu states %zu transitions",
                 msg, level_max, explored, trans);
    } else {
        Warning (info, "%s%zu levels ~%zu states ~%zu transitions", msg,
                 level_max, W * global->threshold,  W * trans);
    }
}

#endif // ALGORITHM_H
