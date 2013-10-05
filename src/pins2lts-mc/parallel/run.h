/**
 * Global information on the run of an algorithm.
 *
 *
 */

#ifndef RUN_H
#define RUN_H

#include <stdbool.h>

#include <pins-lib/pins.h>

typedef struct alg_s            alg_t; // TODO?
typedef struct alg_shared_s     alg_shared_t;
typedef struct alg_reduced_s    alg_reduced_t;
typedef struct thread_ctx_s     wctx_t;

typedef struct run_s {
    alg_t              *alg;        // The algorithm for this run
    wctx_t            **contexts;   // The worker contexts of this run
    alg_shared_t       *shared;     // Objects used in algorithm shared by all workers

    alg_reduced_t      *reduced;    // Reduced statistics from the run
    float               maxtime;    // Max running time for this run
    float               runtime;    // Total user time for this run (the sum of the runtimes of all workers)
} run_t;

extern run_t *run_create    ();

extern wctx_t *run_init     (run_t *run, model_t model);

extern void run_alg         (wctx_t *ctx);

/**
 * Only once!
 */
extern void run_reduce_and_print_stats (wctx_t *ctx);

extern run_t *run_deinit    (wctx_t *ctx);

extern void run_destroy     (run_t *run);

#endif // RUN_H
