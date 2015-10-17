/**
 *
 */

#include <hre/config.h>


#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

struct sync_s {
    is_stopped_f        is_stopped; // Worker synchronization mechanism
    stop_f              stop;       // Worker synchronization mechanism
    int                 stopped;
};

void
run_set_stop (run_t *run, stop_f stop)
{
    run->syncer->stop = stop;
}

void
run_set_is_stopped (run_t *run, is_stopped_f stopped)
{
    run->syncer->is_stopped = stopped;
}

stop_f
run_get_stop (run_t *run)
{
    return run->syncer->stop;
}

is_stopped_f
run_get_is_stopped (run_t *run)
{
    return run->syncer->is_stopped;
}

void
run_alg (wctx_t *ctx)
{
    RTstartTimer (ctx->timer);
    alg_run (ctx->run, ctx);
    RTstopTimer (ctx->timer);
}

size_t
run_local_state_infos (wctx_t *ctx)
{
    return ctx->run->total.local_states;
}

void
run_reduce_stats (wctx_t *ctx)
{
    RTswitchAlloc (global->procs);
    for (size_t i = 0; i < W; i++) {
        if (i == ctx->id) {
            float                   runtime = RTrealTime(ctx->timer);
            ctx->run->total.runtime += runtime;
            ctx->run->total.maxtime = max(runtime, ctx->run->total.maxtime);
            ctx->run->total.mintime = min(runtime, ctx->run->total.mintime);

            work_add_results (&ctx->run->total, ctx->counters);

            alg_reduce (ctx->run, ctx);
        }
        HREbarrier (HREglobal());
    }
    RTswitchAlloc (false);
}

void
run_print_stats (wctx_t *ctx)
{
    Print1 (info, " ");
    alg_print_stats (ctx->run, ctx);
}

run_t *
run_deinit (wctx_t *ctx)
{
    run_t           *run = ctx->run;

    alg_local_deinit (ctx->run, ctx);
    wctx_deinit (ctx);

    RTswitchAlloc (global->procs);
    alg_global_deinit (ctx->run, ctx);
    wctx_destroy (ctx);
    RTswitchAlloc (false);

    HREbarrier (HREglobal());

    return run;
}

wctx_t *
run_init (run_t *run, model_t model)
{
    RTswitchAlloc (global->procs);
    wctx_t          *ctx = wctx_create (model, run);
    RTswitchAlloc (false);

    wctx_init (ctx); // required in alg_global_init

    RTswitchAlloc (global->procs);
    alg_global_init (ctx->run, ctx);
    RTswitchAlloc (false);

    alg_local_init (run, ctx);

    return ctx;
}

static int
run_stop_impl (run_t *run)
{
    return cas (&run->syncer->stopped, 0, 1);
}

static int
run_is_stopped_impl (run_t *run)
{
    return atomic_read (&run->syncer->stopped);
}

int
run_stop (run_t *run)
{
    return run->syncer->stop (run);
}

int
run_is_stopped (run_t *run)
{
    return run->syncer->is_stopped (run);
}

run_t *
run_create (bool init)
{
    RTswitchAlloc (global->procs);
    run_t              *run = RTmallocZero (sizeof(run_t));
    run->contexts = RTmallocZero (sizeof (wctx_t *[W]));
    run->syncer = RTmallocZero (sizeof (sync_t));
    run->total.maxtime = 0;
    run->total.mintime = SIZE_MAX;
    run->total.runtime = 0;
    run_set_is_stopped (run, run_is_stopped_impl);
    run_set_stop (run, run_stop_impl);
    run->syncer->stopped = 0;
    run->alg = alg_create ();
    run->threshold = init_threshold;
    if (init)
        alg_shared_init_strategy (run, strategy[0]);
    RTswitchAlloc (false);

    return run;
}

void
run_destroy (run_t *run)
{
    RTswitchAlloc (global->procs);
    alg_destroy (run->alg);
    RTfree (run->contexts);
    // TODO: alg_shared and alg_reduced, e.g.: lb_destroy (run->shared->lb);
    RTfree (run);
    RTswitchAlloc (false);
}

void
run_report_total (run_t *run)
{
    work_counter_t         *cnt_work = &run->total;
    Warning (info, "Explored %zu states %zu transitions, fanout: %.3f",
                    cnt_work->explored, cnt_work->trans,
                    ((double)cnt_work->trans) / cnt_work->explored);
    Warning (info, "Total exploration time %5.3f sec "
                   "(%5.3f sec minimum, %5.3f sec on average)",
                   cnt_work->maxtime, cnt_work->mintime, cnt_work->runtime / W);
    //RTprintTimer (info, timer, "Total exploration time");
    Warning(info, "States per second: %.0f, Transitions per second: %.0f",
            cnt_work->explored/cnt_work->maxtime,
            cnt_work->trans/cnt_work->maxtime);
}

void
run_report (run_t *run, work_counter_t *cnt, size_t trans, char *prefix)
{
    if (!cas(&run->threshold, run->threshold, run->threshold << 1))
        return;
    work_counter_t              work;
    work.level_cur = cnt->level_cur;
    work.level_max = cnt->level_max;
    work.trans = trans;
    if (W == 1) {
        work.explored = run->threshold >> 1;
        work_report (prefix, &work);
    } else {
        work.explored = (run->threshold >> 1) * W;
        work_report_estimate (prefix, &work);
    }
}
