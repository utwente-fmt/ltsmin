#include <hre/config.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <mpi.h>
#include "Dtaudlts.h"
#include "paint.h"
#include "groups.h"

#include <hre/runtime.h>
#include <hre-mpi/user.h>
#include <lts-lib/lts.h>

// some variables requested by bufs.h
int                 flag;
int                 send_pending;
int                 bufnewids_pending;
MPI_Request         send_request;

static enum {
    SCC_COLOR = 1,
    SCC_GROUP = 2
} action = SCC_COLOR;

static struct poptOption options[] = {
    {"color", 0, POPT_ARG_VAL, &action, SCC_COLOR,
     "cycle elimination using coloring (default)", NULL},
    {"group", 0, POPT_ARG_VAL, &action, SCC_GROUP,
     "cycle elimination using groups", NULL},
    // This program should use:
    // { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL,
    // NULL},
    POPT_TABLEEND
};

// the number of vertices and arcs ..
// final
int                 Nfinal,
                    Mfinal;
// hooked, i.e. on a cycle
int                 Nhooked,
                    Mhooked;
// not yet classified as "final" or "hooked"
int                 Nleft,
                    Mleft;

int                 me,
                    nodes;

// ***************************************************************

int
main (int argc, char **argv)
{
    dlts_t              lts;
    rt_timer_t          timer;
    int                 oldN, oldM, tauN, tauM, N, M, i, j;

    char               *files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a distributed cycle elimination on the input.\n\nOptions");
    lts_lib_setup();
    HREselectMPI();
    HREinitStart(&argc,&argv,1,2,files,"<input> [<output>]");


    MPI_Comm_size (MPI_COMM_WORLD, &nodes);
    MPI_Comm_rank (MPI_COMM_WORLD, &me);

    timer = RTcreateTimer ();

    if (me == 0)
        Warning (info, "(tau)SCC elimination");
    if (me == 0)
        RTstartTimer (timer);
    MPI_Barrier (MPI_COMM_WORLD);
    lts = dlts_read(MPI_COMM_WORLD,files[0]);
    oldN = lts->state_count[me];
    oldM = 0;
    for (i = 0; i < lts->segment_count; i++)
        oldM += lts->transition_count[me][i];
    MPI_Reduce (&oldN, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    MPI_Reduce (&oldM, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);

    if (me == 0) {
        oldN = i;
        oldM = j;
        Warning (info, "%d states and %d transitions", oldN, oldM);
        RTstopTimer (timer);
        RTprintTimer (info, timer, "***** reading the LTS took");
        RTresetTimer (timer);
        RTstartTimer (timer);
    }
    MPI_Barrier (MPI_COMM_WORLD);
    switch (action) {
    case SCC_COLOR:
        dlts_elim_tauscc_colours (lts);
        break;
    case SCC_GROUP:
        if (!dlts_elim_tauscc_groups (lts)) {
            if (me == 0)
                Abort("cannot get it small enough!");
        }
        MPI_Barrier (MPI_COMM_WORLD);
        break;
    default:
        if (me == 0)
            Abort("bad action %d", action);
        MPI_Barrier (MPI_COMM_WORLD);
    }
    MPI_Barrier (lts->comm);
    if (me == 0) {
        RTstopTimer (timer);
        RTprintTimer (info, timer, "***** SCC reduction took");
        RTresetTimer (timer);
        RTstartTimer (timer);
    }
    // statistics...
    N = lts->state_count[me];
    M = 0;
    for (i = 0; i < lts->segment_count; i++)
        M += lts->transition_count[me][i];
    MPI_Reduce (&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    MPI_Reduce (&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    if (me == 0) {
        Warning (info, "LTS initial:%10d states and %10d transitions",
                 oldN, oldM);
        Warning (info, "LTS reduced:%10d states [%3.3f] and %10d [%3.3f] transitions",
                 i, 100 * i / (float)oldN,
                 j, 100 * j / (float)oldM);
    }
    N = Nfinal + Nleft + Nhooked;
    M = Mfinal + Mhooked;
    MPI_Reduce (&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    MPI_Reduce (&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    tauN = i;
    tauM = j;
    N = Nfinal + Nleft;
    M = Mfinal;
    MPI_Reduce (&N, &i, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    MPI_Reduce (&M, &j, 1, MPI_INT, MPI_SUM, 0, lts->comm);
    if (me == 0) {
        Warning (info, "TAU initial:%10d states [%3.3f] and %10d [%3.3f] transitions",
                 tauN, 100 * tauN / (float)oldN,
                 tauM, 100 * tauM / (float)oldM);
        Warning (info, "TAU reduced:%10d states [%3.3f] and %10d [%3.3f] transitions",
                 i, 100 * i / (float)tauN,
                 j, 100 * j / (float)tauM);
    }

    if (files[1]) {
        if (me == 0)
            Warning (info, "NOW WRITING");
        dlts_writedir (lts, files[1]);
    }
    // if (lts != NULL) dlts_free(lts);
    HREexit(HRE_EXIT_SUCCESS);
}
