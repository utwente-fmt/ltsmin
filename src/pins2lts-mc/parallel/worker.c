/**
 *
 */

#include <hre/config.h>

#include <stdint.h>

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/worker.h>

void
work_add_results (work_counter_t *res, work_counter_t *cnt)
{
    res->explored += cnt->explored;
    res->trans += cnt->trans;
    res->level_cur += cnt->level_cur;
    res->level_max += cnt->level_max;
}
void
work_report (char *prefix, work_counter_t *cnt)
{
    Warning (info, "%s%zu levels %zu states %zu transitions",
             prefix, cnt->level_max, cnt->explored, cnt->trans);
}

void
work_report_estimate (char *prefix, work_counter_t *cnt)
{
    Warning (info, "%s~%zu levels ~%zu states ~%zu transitions",
             prefix, cnt->level_max, cnt->explored, cnt->trans);
}

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

wctx_t *
wctx_init (wctx_t *ctx)
{
    alg_t              *alg = ctx->run->alg;
    state_info_create_empty (&ctx->state);
    ctx->store = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->store2 = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->permute = permute_create (permutation, ctx->model,
                                   get_alg_state_seen(alg), W, K, ctx->id,
                                   ctx->run);

    ctx->counters = RTalignZero (CACHE_LINE_SIZE,
                                 sizeof(work_counter_t) + CACHE_LINE_SIZE);

    transition_info_t       ti = GB_NO_TRANSITION;
    ctx->initial_state = RTmalloc (sizeof(int[N]));
    GBgetInitialState (ctx->model, ctx->initial_state);
    state_info_initialize (&ctx->initial, ctx->initial_state, &ti, &ctx->state,
                           ctx->store2);
}

void
wctx_deinit (wctx_t *ctx)
{
    RTfree (ctx->store);
    RTfree (ctx->store2);
    RTfree (ctx->initial_state);
    permute_free (ctx->permute);
}

void
wctx_destroy (wctx_t *ctx)
{
    RTdeleteTimer (ctx->timer);
    RTfree (ctx);
}

