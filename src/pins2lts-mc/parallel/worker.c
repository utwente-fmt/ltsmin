/**
 *
 */

#include <hre/config.h>

#include <stdint.h>

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/worker.h>

wctx_t *
wctx_create (model_t model, run_t *run)
{
    HREassert (NULL == 0, "NULL != 0");
    wctx_t             *ctx = RTalignZero (CACHE_LINE_SIZE, sizeof (wctx_t));
    ctx->id = HREme (HREglobal());
    ctx->run = run;
    ctx->model = model;
    ctx->counters = RTalignZero (CACHE_LINE_SIZE,
                                 sizeof(work_counter_t) + CACHE_LINE_SIZE);
    ctx->counter_example = 0;

    return ctx;
}

void
wctx_init (wctx_t *ctx)
{
    alg_t              *alg = ctx->run->alg;
    ctx->timer = RTcreateTimer ();
    ctx->state = state_info_create ();
    ctx->ce_state = state_info_create ();
    ctx->initial = state_info_create ();

    ctx->permute = permute_create (permutation, ctx->model,
                                   get_alg_state_seen(alg), ctx->id, ctx->run);

    state_data_t            initial_state = RTmalloc (sizeof(int[N]));
    GBgetInitialState (ctx->model, initial_state);
    state_info_first (ctx->initial, initial_state);

    // register with run:
    HREassert (ctx->run->contexts[ctx->id] == NULL);
    ctx->run->contexts[ctx->id] = ctx;
    // RTfree (ctx->initial); // used in state-info
}

void
wctx_deinit (wctx_t *ctx)
{
    permute_free (ctx->permute);
}

void
wctx_destroy (wctx_t *ctx)
{
    RTdeleteTimer (ctx->timer);
    RTalignedFree (ctx);
}

