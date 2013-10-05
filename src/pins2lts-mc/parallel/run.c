/**
 *
 */

#include <hre/config.h>


#include <hre/user.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

void
run_alg (wctx_t *ctx)
{
    RTstartTimer (ctx->timer);
    alg_run (ctx->run, ctx);
    RTstopTimer (ctx->timer);
}

void
run_reduce_and_print_stats (wctx_t *ctx)
{
    Print1 (info, " ");

    RTswitchAlloc (!global->pthreads);
    for (size_t i = 0; i < W; i++) {
        if (i == ctx->id) {
            float                   runtime = RTrealTime(ctx->timer);
            ctx->run->runtime += runtime;
            ctx->run->maxtime = max(runtime, ctx->run->maxtime);

            alg_reduce (ctx->run, ctx);
        }
        HREbarrier (HREglobal());
    }
    RTswitchAlloc (false);

    if (ctx->id == 0) {
        alg_print_stats (ctx->run, ctx);
    }
}

run_t *
run_deinit (wctx_t *ctx)
{
    run_t           *run = ctx->run;

    alg_local_deinit (ctx->run, ctx);
    wctx_deinit (ctx);

    RTswitchAlloc (!global->pthreads);
    alg_global_deinit (ctx->run, ctx);
    wctx_destroy (ctx);
    RTswitchAlloc (false);

    HREbarrier (HREglobal());

    return run;
}

wctx_t *
run_init (run_t *run, model_t model)
{
    RTswitchAlloc (!global->pthreads);
    wctx_t          *ctx = wctx_create (model, run);
    ctx->run = run;
    run->contexts[ctx->id] = ctx; // polygamy
    alg_global_init (ctx->run, ctx);
    lb_local_init (global->lb, ctx->id, ctx); // Barrier
    RTswitchAlloc (false);

    wctx_init (ctx);
    alg_local_init (run, ctx);

    return ctx;
}

run_t *
run_create ()
{
    RTswitchAlloc (!global->pthreads);
    run_t              *run = RTmallocZero (sizeof(run_t));
    run->contexts = RTmalloc (sizeof (wctx_t*[W]));
    run->maxtime = 0;
    run->runtime = 0;
    run->alg = alg_create ();
    alg_shared_init_strategy (run, strategy[0]);
    RTswitchAlloc (false);

    return run;
}

void
run_destroy (run_t *run)
{
    RTswitchAlloc (!global->pthreads);
    alg_destroy (run->alg);
    RTfree (run->contexts);
    // TODO: shared and reduced!
    RTfree (run);
    RTswitchAlloc (false);
}
