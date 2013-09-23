#include <hre/config.h>

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <popt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/statistics.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/permute.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>


static void
exit_ltsmin (int sig)
{
    if (HREme(HREglobal()) != 0)
        return;
    if ( !lb_stop(global->lb) ) {
        Abort ("UNGRACEFUL EXIT");
    } else {
        Warning (info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
}

#ifdef OPAAL
static pthread_mutex_t    *mutex = NULL;          // global mutex
#endif

/**
 * Performs those allocations that are absolutely necessary for local initiation
 * It initializes a mutex and a table of chunk tables.
 */
void
prelocal_global_init ()
{
    if (HREme(HREglobal()) == 0) {
        RTswitchAlloc (!SPEC_MT_SAFE);

        global_create ();

        global->exit_status = LTSMIN_EXIT_SUCCESS;
        //                               multi-process && multiple processes
        global->tables = cct_create_map (!SPEC_MT_SAFE && HREdefaultRegion(HREglobal()) != NULL);

        GBloadFileShared (NULL, files[0]); // NOTE: no model argument

        RTswitchAlloc (false);
    }
    HREreduce (HREglobal(), 1, &global, &global, Pointer, Max);

#ifdef OPAAL
    if (HREme(HREglobal()) == 0) {
        RTswitchAlloc (!SPEC_MT_SAFE);
        mutex = RTmalloc (sizeof(pthread_mutex_t));
        pthread_mutex_init (mutex, NULL);
        RTswitchAlloc (false);
    }
    HREreduce (HREglobal(), 1, &mutex, &mutex, Pointer, Max);
#endif
}

/**
 * Initialize locals: model and settings
 */
wctx_t *
local_init ()
{
    model_t             model = GBcreateBase ();

    cct_cont_t         *map = cct_create_cont (global->tables);
    GBsetChunkMethods (model, (newmap_t)cct_create_vt, map,
                       HREgreyboxI2C,
                       HREgreyboxC2I,
                       HREgreyboxCAtI,
                       HREgreyboxCount);

    Print1 (info, "Loading model from %s", files[0]);

    // some frontends (opaal) do not have a thread-safe initial state function
#ifdef OPAAL
    pthread_mutex_lock (mutex);
    GBloadFile (model, files[0], &model);
    pthread_mutex_unlock (mutex);
#else
    GBloadFile (model, files[0], &model);
#endif

#if SPEC_MT_SAFE == 1
    // in the multi-process environment, we initialize statics locally:
    if (HREme(HREglobal()) == 0) {
#endif

       statics_init (model);
    #ifdef OPAAL
        options_static_setup (model, true);
    #else
        options_static_setup (model, false);
    #endif

#if SPEC_MT_SAFE == 1
    }
#endif

    if (HREme(HREglobal()) == 0) {
        print_options (model);
    }

    run_t              *run = NULL;

    RTswitchAlloc (!SPEC_MT_SAFE);
    if (HREme(HREglobal()) == 0) {
        shared_init (model);
        run = alg_create ();
    }
    HREreduce (HREglobal(), 1, &run, &run, Pointer, Max);
    wctx_t          *ctx = wctx_create (model, run);
    alg_global_init (ctx->run, ctx);
    RTswitchAlloc (false);

    alg_local_init (run, ctx);

    //RTswitchAlloc (!SPEC_MT_SAFE);
    //alg_global_init (ctx->run, ctx);
    //RTswitchAlloc (false);

    (void) signal (SIGINT, exit_ltsmin);

    return ctx;
}

static void
deinit_all (wctx_t *ctx)
{
    if (0 == ctx->id) {
        RTswitchAlloc (!SPEC_MT_SAFE);
        global_deinit ();
        RTswitchAlloc (false);
    }

    alg_destroy (ctx->run, ctx);

    wctx_free (ctx);
    HREbarrier (HREglobal());
}


void
reduce_and_print_result (wctx_t *ctx)
{
    RTswitchAlloc (!SPEC_MT_SAFE);

    for (size_t i = 0; i < W; i++) {
        if (i == ctx->id)
            alg_reduce (ctx->run, ctx);
        HREbarrier (HREglobal());
    }
    RTswitchAlloc (false);

    HREbarrier (HREglobal());
    (void) ctx;
}

/* explore is started for each thread (worker) */
static void
explore (wctx_t *ctx)
{
    transition_info_t       ti = GB_NO_TRANSITION;
    state_data_t            initial = RTmalloc (sizeof(int[N]));
    GBgetInitialState (ctx->model, initial);
    state_info_initialize (&ctx->initial, initial, &ti, &ctx->state, ctx->store2);

    HREbarrier (HREglobal());
    alg_run (ctx->run, ctx);

    RTfree (initial);
}

int
main (int argc, char *argv[])
{
    /* Init structures */
    HREinitBegin (argv[0]);

#ifdef OPAAL
    HREaddOptions (options_timed, "Perform model checking on <model>\n\nOptions");
#else
    HREaddOptions (options, "Perform model checking on <model>\n\nOptions");
#endif

    lts_lib_setup ();

#if SPEC_MT_SAFE == 1
    HREenableThreads (1);
#else
    HREenableFork (1); // enable multi-process env for mCRL/mCrl2 and PBES
#endif

    HREinitStart (&argc,&argv,1,2,files,"<model> [lts]");      // spawns threads!

    wctx_t                 *ctx;

    prelocal_global_init ();

    ctx = local_init ();

    explore (ctx);

    reduce_and_print_result (ctx);

    deinit_all (ctx);

    HREabort (global->exit_status);
}
