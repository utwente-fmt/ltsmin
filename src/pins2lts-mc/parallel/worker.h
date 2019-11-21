/**
 *
 */

#ifndef WORKER_H
#define WORKER_H

#include <stdlib.h>

#include <hre-io/user.h>
#include <pins2lts-mc/parallel/counter.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/state-info.h>

typedef struct alg_local_s  alg_local_t;
typedef struct alg_global_s alg_global_t;

struct thread_ctx_s {
    size_t              id;             // thread id (0..NUM_THREADS)
    rt_timer_t          timer;          // Local exploration time timer
    permute_t          *permute;        // transition permutor
    work_counter_t     *counters;       // General work counters
    state_info_t       *initial;        // initial state
    wctx_t             *parent;

    run_t              *run;            // The run info shared by all workers
    model_t             model;          // PINS language module
    alg_local_t        *local;          // Worker information, local to worker
    alg_global_t       *global;         // Worker information, shared with others
    state_info_t       *state;          // currently explored state
    int                 counter_example;// found counter-example
    state_info_t       *ce_state;
};

extern wctx_t *wctx_create (model_t model, run_t *run);

extern void wctx_init (wctx_t *ctx);

extern void wctx_deinit (wctx_t *ctx);

extern void wctx_destroy (wctx_t *ctx);

#endif // WORKER_H
