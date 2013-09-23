/**
 *
 */

#include <hre/config.h>

#include <stdint.h>

#include <pins2lts-mc/parallel/worker.h>

wctx_t *
wctx_create (model_t model, run_t *run)
{
    HREassert (NULL == 0, "NULL != 0");
    wctx_t             *ctx = RTalignZero (CACHE_LINE_SIZE, sizeof (wctx_t));
    ctx->id = HREme (HREglobal());
    ctx->run = run;
    ctx->model = model;
    ctx->timer = RTcreateTimer ();
    return ctx;
}

void
wctx_free (wctx_t *ctx)
{
    // wctx destroy:
    RTfree (ctx->store);
    RTfree (ctx->store2);
    (void) ctx;
}
