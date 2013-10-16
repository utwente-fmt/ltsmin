/**
 * Global information on the run of an algorithm.
 *
 *
 */

#ifndef RUN_H
#define RUN_H

#include <stdbool.h>

#include <pins2lts-mc/parallel/counter.h>

typedef struct alg_s            alg_t;
typedef struct alg_shared_s     alg_shared_t;
typedef struct alg_reduced_s    alg_reduced_t;
typedef struct thread_ctx_s     wctx_t;
typedef struct sync_s           sync_t;
typedef struct run_s            run_t;

struct run_s {
    alg_t              *alg;        // The algorithm for this run
    wctx_t            **contexts;   // The worker contexts of this run
    alg_shared_t       *shared;     // Objects used in algorithm shared by all workers
    sync_t             *syncer;     // Worker synchronization mechanisms

    alg_reduced_t      *reduced;    // Reduced statistics from the run
    work_counter_t      total;

    char                pad1[CACHE_LINE_SIZE];
    size_t              threshold;
    char                pad2[CACHE_LINE_SIZE];
};

typedef int (*stop_f) (run_t *run);

typedef int (*is_stopped_f) (run_t *run);

/**
 * Function getters and setters
 */

extern void run_set_stop (run_t *run, stop_f stop);

extern void run_set_is_stopped (run_t *run, is_stopped_f stopped);

extern stop_f run_get_stop (run_t *run);

extern is_stopped_f run_get_is_stopped (run_t *run);

/**
 * Functions
 */

extern run_t *run_create    (bool init);

extern wctx_t *run_init     (run_t *run, model_t model);

extern void run_alg         (wctx_t *ctx);

/**
 * Only once!
 */
extern void run_reduce_stats (wctx_t *ctx);

extern void run_print_stats (wctx_t *ctx);

extern run_t *run_deinit    (wctx_t *ctx);

extern void run_destroy     (run_t *run);

extern int run_stop (run_t *run);

extern int run_is_stopped (run_t *run);

/**
 * Additional functionality
 */

extern size_t run_local_state_infos (wctx_t *ctx);

extern void run_report_total (run_t *run);

extern void run_report (run_t *run, work_counter_t *cnt, size_t trans,
                        char *prefix);

static inline void
run_maybe_report (run_t *run, work_counter_t *cnt, char *msg)
{
    if (EXPECT_TRUE(!log_active(info) || cnt->explored < run->threshold))
        return;
    run_report (run, cnt, cnt->trans, msg);
}

static inline void
run_maybe_report1 (run_t *run, work_counter_t *cnt, char *msg)
{
    if (EXPECT_TRUE(!log_active(info) || cnt->explored < run->threshold))
        return;
    run_report (run, cnt, cnt->trans * HREpeers(HREglobal()), msg);
}

#endif // RUN_H
