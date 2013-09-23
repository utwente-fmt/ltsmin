/**
 *
 */

#ifndef WORKER_H
#define WORKER_H

#include <stdlib.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <mc-lib/stats.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/state-info.h>

typedef struct alg_local_s  alg_local_t;
typedef struct alg_global_s alg_global_t;

typedef struct thread_ctx_s {
    size_t              id;             // thread id (0..NUM_THREADS)
    model_t             model;          // Greybox model
    stream_t            out;            // raw file output stream
    permute_t          *permute;        // transition permutor
    rt_timer_t          timer;          // Local exploration time timer

    alg_local_t        *local;
    alg_global_t       *global;
    // state_info_t TODO:

    state_data_t        store;          // temporary state storage1
    state_data_t        store2;         // temporary state storage2
    state_info_t        state;          // currently explored state
    state_info_t        initial;        // initial state

    run_t              *run;
} wctx_t;

extern wctx_t *wctx_create (model_t model, run_t *run);

extern void wctx_free (wctx_t *ctx);

#endif // WORKER_H
