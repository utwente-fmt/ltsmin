#include <hre/config.h>

#include <pthread.h>
#include <signal.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/statistics.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/run.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/util.h>

struct poptOption options_mc[] = {
#ifdef OPAAL
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, options_timed, 0, NULL, NULL},
#else
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, options, 0, NULL, NULL},
#endif
     SPEC_POPT_OPTIONS,
     POPT_TABLEEND
};

static void
exit_ltsmin (int sig)
{
    if (HREme(HREglobal()) != 0)
        return;
    if ( !lb_stop(global->lb) ) { //TODO: separate stop functionality from LB
        Abort ("UNGRACEFUL EXIT");
    } else {
        Warning (info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
}

#ifdef OPAAL
static bool                timed_model = true;
static pthread_mutex_t    *mutex = NULL;          // global mutex
#else
static bool                timed_model = false;
#endif


static void
hre_init_and_spawn_workers (int argc, char *argv[])
{
    /* Init structures */
    HREinitBegin (argv[0]);

    HREaddOptions (options_mc, "Perform model checking on <model>\n\nOptions");

    lts_lib_setup ();

#if SPEC_MT_SAFE == 1
    HREenableThreads (1);
#else
    HREenableFork (1); // enable multi-process env for mCRL/mCrl2 and PBES
#endif

    HREinitStart (&argc,&argv,1,2,files,"<model> [lts]");      // spawns threads!
}

model_t
create_pins_model ()
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
    if (HREme(HREglobal()) == 0) {
        RTswitchAlloc (!SPEC_MT_SAFE);
        mutex = RTmalloc (sizeof(pthread_mutex_t));
        RTswitchAlloc (false);
        pthread_mutex_init (mutex, NULL);
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

    global_create (SPEC_MT_SAFE);

    model = create_pins_model ();

    global_init (model, timed_model);
    global_print (model);

    (void) signal (SIGINT, exit_ltsmin);

    return model;
}

/**
 * Initialize locals: model and settings
 */
wctx_t *
local_init (model_t model)
{
    run_t              *run = NULL;
    wctx_t             *ctx;

    if (HREme(HREglobal()) == 0)
        run = run_create ();
    HREreduce (HREglobal(), 1, &run, &run, Pointer, Max);

    ctx = run_init (run, model);

    return ctx;
}

static void
deinit_all (wctx_t *ctx)
{
    run_t               *run;

    run = run_deinit (ctx);

    if (0 == ctx->id) {
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

    run_reduce_and_print_stats (ctx);
    global_reduce_and_print_stats (model);

    deinit_all (ctx);

    HREexit (global->exit_status);
    return -5; // should not be reached
}
