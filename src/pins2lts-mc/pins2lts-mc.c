#include <hre/config.h>

#include <pthread.h>
#include <signal.h>

#include <lts-io/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

#define FORCE_STRING "force-procs"

static int          HRE_PROCS = 0;

static struct poptOption options_mc[] = {
#ifdef OPAAL
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, options_timed, 0, NULL, NULL},
#else
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, options, 0, NULL, NULL},
#endif
     {FORCE_STRING, 0, POPT_ARG_VAL | POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARGFLAG_DOC_HIDDEN,
      &HRE_PROCS, 1, "Force multi-process in favor of pthreads", NULL},
     SPEC_POPT_OPTIONS,
     POPT_TABLEEND
};

#ifdef OPAAL
static bool                timed_model = true;
static pthread_mutex_t    *mutex = NULL;          // global mutex
#else
static bool                timed_model = false;
#endif

static run_t              *run = NULL;

static void
exit_ltsmin (int sig)
{
    if (HREme(HREglobal()) == 0) {
        if ( run_stop(run) ) {
            Warning (info, "PREMATURE EXIT (caught signal: %d)", sig);
        } else {
            Abort ("UNGRACEFUL EXIT");
        }
    }
}

static void
hre_init_and_spawn_workers (int argc, char *argv[])
{
    /* Init structures */
    HREinitBegin (argv[0]);

    HREaddOptions (options_mc, "Perform model checking on <model>\n\nOptions");

    lts_lib_setup ();

    // Only use the multi-process environment if necessary:
    HRE_PROCS |= !SPEC_MT_SAFE;
    HRE_PROCS |= char_array_search (argv, argc, "--" FORCE_STRING);

    if (HRE_PROCS) {
        HREenableFork (RTnumCPUs(), true);
    } else {
        HREenableThreads (RTnumCPUs(), true);
    }

    // spawns threads:
    HREinitStart (&argc, &argv, 1, 2, files, "<model> [lts]");

    // Only use shared allocation if available
    // HRE only initiates this for more than one process
    HRE_PROCS &= HREdefaultRegion(HREglobal()) != NULL;
}

model_t
create_pins_model ()
{
    model_t             model = GBcreateBase ();

    cct_cont_t         *map = cct_create_cont (global->tables);
    GBsetChunkMethods (model, (newmap_t)cct_create_vt, map, HREgreyboxI2C,
                       HREgreyboxC2I, HREgreyboxCAtI, HREgreyboxCount);

    Print1 (info, "Loading model from %s", files[0]);

    // some frontends (opaal) do not have a thread-safe initial state function
#ifdef OPAAL
    if (HREme(HREglobal()) == 0) {
        RTswitchAlloc (HRE_PROCS);
        mutex = RTmalloc (sizeof(pthread_mutex_t));
        RTswitchAlloc (false);
        pthread_mutexattr_t    lock_attr;
        pthread_mutexattr_init(&lock_attr);
        int type = HRE_PROCS ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE;
        if (pthread_mutexattr_setpshared(&lock_attr, type))
            AbortCall("pthread_rwlockattr_setpshared");
        pthread_mutex_init (mutex, &lock_attr);
    }
    HREreduce (HREglobal(), 1, &mutex, &mutex, Pointer, Max);

    pthread_mutex_lock (mutex);
    GBloadFile (model, files[0], &model);
    pthread_mutex_unlock (mutex);
#else
    GBloadFile (model, files[0], &model);
#endif

    return model;
}

/**
 * Performs those allocations that are absolutely necessary for local initiation
 * It initializes a mutex and a table of chunk tables.
 */
model_t
global_and_model_init ()
{
    model_t             model;

    global_create (HRE_PROCS);

    model = create_pins_model ();

    global_init (model, SPEC_REL_PERF, timed_model);
    global_print (model);

    return model;
}

/**
 * Initialize locals: model and settings
 */
wctx_t *
local_init (model_t model)
{
    if (HREme(HREglobal()) == 0)
        run = run_create (true);
    HREreduce (HREglobal(), 1, &run, &run, Pointer, Max);

    (void) signal (SIGINT, exit_ltsmin);

    return run_init (run, model);
}

/**
 * Reduce statistics and print results
 */
void
reduce_and_print (wctx_t *ctx)
{
    run_reduce_stats (ctx);
    global_reduce_stats (ctx->model);

    if (HREme(HREglobal()) == 0) {
        run_print_stats (ctx);
        global_print_stats (ctx->model, run_local_state_infos(ctx),
                            state_info_serialize_size (ctx->state));
    }
}

static void
deinit_all (wctx_t *ctx)
{
    run_t               *run;

    run = run_deinit (ctx);

    if (HREme(HREglobal()) == 0) {
        run_destroy (run);
        global_deinit ();
    }
}

int
main (int argc, char *argv[])
{
    wctx_t             *ctx;
    model_t             model;

    hre_init_and_spawn_workers (argc, argv);

    /*************************************************************************/
    /* from this location on, multiple workers execute the code in parallel  */
    /*************************************************************************/

    model = global_and_model_init ();

    ctx = local_init (model);

    run_alg (ctx);

    reduce_and_print (ctx);

    deinit_all (ctx);

    HREexit (global->exit_status);
}
