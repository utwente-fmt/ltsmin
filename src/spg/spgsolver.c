/*
 * spgsolver.c
 *
 *  Created on: 20 Feb 2012
 *      Author: kant
 */
#include <hre/config.h>

#include <limits.h>

#include <hre/user.h>
#ifdef HAVE_SYLVAN
#include <sylvan.h>
#include <sched.h> // for sched_getaffinity
#endif
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>

#ifdef HAVE_SYLVAN
static size_t lace_n_workers = 0;
static size_t lace_dqsize = 40960000; // can be very big, no problemo

static  struct poptOption lace_options[] = {
    { "lace-workers", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_n_workers , 0 , "set number of Lace workers (threads for parallelization)","<workers>"},
    { "lace-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_dqsize , 0 , "set length of Lace task queue","<dqsize>"},
    POPT_TABLEEND
};
#endif

static  struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, spg_solve_options , 0, "Symbolic parity game solver options", NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
#ifdef HAVE_SYLVAN
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lace_options , 0 , "Lace options",NULL},
#endif
    POPT_TABLEEND
};

int
main (int argc, char *argv[])
{
    char *files[1];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Symbolic parity game solver. Solves <game>.\n");
    HREinitStart(&argc,&argv,1,1,files,"<game>");

#ifdef HAVE_SYLVAN
    lace_init(lace_n_workers, lace_dqsize);
    size_t n_workers = lace_workers();
    Warning(info, "Using %zu CPUs", n_workers);
    size_t stacksize = 256*1024*1024; // 256 megabytes
    lace_startup(stacksize, 0, 0);
#endif

    //vset_implementation_t vset_impl = VSET_IMPL_AUTOSELECT;
    FILE *f = fopen(files[0], "r");
    if (f == NULL)
        AbortCall ("Unable to open file ``%s''", files[0]);
    Print(infoShort, "Loading parity game from file %s. vset = %d.", files[0], vset_default_domain);
    parity_game* g = spg_load(f, vset_default_domain);
    fclose(f);
    spgsolver_options* spg_options = spg_get_solver_options();
    rt_timer_t spgsolve_timer = RTcreateTimer();
    RTstartTimer(spgsolve_timer);
    bool result = spg_solve(g, spg_options);
    Print(infoShort, " ");
    Print(infoShort, "The result is: %s", result ? "true":"false");
    RTstopTimer(spgsolve_timer);
    Print(infoShort, " ");
    RTprintTimer(infoShort, spgsolve_timer, "solving took");

    HREexit(HRE_EXIT_SUCCESS);
}
