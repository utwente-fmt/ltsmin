/**
 *
 * Multi-core LTL and reachability model checking tool.
 *
 * Described in Laarman's PhD thesis:
    @phdthesis{mcmc,
           title = {{Scalable Multi-Core Model Checking}},
          author = {Alfons W. Laarman},
         address = {Enschede, The Netherlands},
       publisher = {University of Twente},
            year = {2014},
             url = {fmt.cs.utwente.nl/tools/ltsmin/laarman_thesis/}
    }
 *
 * Partial order-reduction for LTL added as described in:
    @inproceedings{cndfs,
     Author = {Alfons. W. Laarman and Wijs, Anton J.},
     Booktitle = {HVC 2014},
     Editor = {E. {Yahav}},
     Pages = {16},
     Publisher = {Springer},
     Series = {LNCS},
     Title = {{Partial-Order Reduction for Multi-Core LTL Model Checking}},
     Volume = {(accepted for publication)},
     Year = {2014}
    }
 */

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
#else
static bool                timed_model = false;
#endif

static run_t              *run = NULL;

static void
exit_ltsmin (int sig)
{
    if (HREme(HREglobal()) == 0 && atomic_read(&run) != NULL) {
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

    if (HREpeers(HREglobal()) > 64) Abort("No more than 64 threads are supported.");
}

model_t
create_pins_model ()
{
    model_t             model = GBcreateBase ();

    table_factory_t     factory = cct_create_table_factory  (global->tables);
    GBsetChunkMap (model, factory); //HREgreyboxTableFactory());

    Print1 (info, "Loading model from %s", files[0]);

    GBloadFile (model, files[0], &model);

    return model;
}

/**
 * Performs those allocations that are absolutely necessary for local initiation
 * It initializes a table of chunk tables and the PINS model.
 */
model_t
global_and_model_init ()
{
    model_t             model;

    global_create (HRE_PROCS);

    model = create_pins_model ();

    global_init (model, SPEC_REL_PERF, timed_model);

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

    wctx_t             *ctx = run_init (run, model);

    return ctx;
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

    global_print (model);

    run_alg (ctx);

    int                 exit_code = global->exit_status;

    reduce_and_print (ctx);

    atomic_write (&run, NULL);
    deinit_all (ctx);

    GBExit (model);

    HREbarrier (HREglobal());
    HREexit (exit_code);
}
