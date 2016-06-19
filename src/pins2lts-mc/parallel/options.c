/**
 *
 */

#include <hre/config.h>

#include <stdlib.h>

#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/dfs-fifo.h>
#include <pins2lts-mc/algorithm/ltl.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/state-store.h>

si_map_entry strategies[] = {
    {"none",    Strat_None},
    {"bfs",     Strat_BFS},
    {"dfs",     Strat_DFS},
    {"sbfs",    Strat_SBFS},
    {"pbfs",    Strat_PBFS},
    {"cndfs",   Strat_CNDFS},
    {"ndfs",    Strat_NDFS},
    {"lndfs",   Strat_LNDFS},
    {"endfs",   Strat_ENDFS},
    {"owcty",   Strat_OWCTY},
    {"map",     Strat_MAP},
    {"ecd",     Strat_ECD},
    {"dfsfifo", Strat_DFSFIFO},
    {"tarjan",  Strat_TARJAN},
    {"ufscc",   Strat_UFSCC},
    {"renault", Strat_RENAULT},
    {NULL, 0}
};

si_map_entry provisos[] = {
    {"none",    Proviso_None},
    {"force-none",Proviso_ForceNone},
    {"closed-set",Proviso_ClosedSet},
    {"stack",   Proviso_Stack},
    {"cndfs",   Proviso_CNDFS},
    {NULL, 0}
};

strategy_t       strategy[MAX_STRATEGIES] =
                {Strat_None, Strat_None, Strat_None, Strat_None, Strat_None};
proviso_t        proviso = Proviso_None;
char*            trc_output = NULL;
int              write_state = 0;
int              inhibit = 0;
int              no_exit = 0;
char*            label_filter = NULL;
char            *files[2];

// TODO: move this to algorithm objects
void
options_static_init      (model_t model, bool timed)
{
    if (files[1]) {
        Print1 (info,"Writing output to %s", files[1]);
        if (strategy[0] != Strat_PBFS) {
            Print1 (info,"Switching to PBFS algorithm for LTS write");
            strategy[0] = Strat_PBFS;
        }
    }

    if (strategy[0] == Strat_None) {
        if (pins_get_weak_ltl_progress_state_label_index(model) != -1 && !PINS_POR) {
            strategy[0] = Strat_DFSFIFO;
        } else if (pins_get_accepting_state_label_index(model) < 0) {
            strategy[0] = timed ? Strat_SBFS : Strat_BFS;
        } else {
            strategy[0] = Strat_CNDFS;
        }
    }

    if (timed) {
        if (!(strategy[0] & (Strat_CNDFS|Strat_Reach)))
            Abort ("Wrong strategy for timed verification: %s", key_search(strategies, strategy[0]));
        if (trc_output && (W != 1 || strategy[0] != Strat_DFS))
            Abort("Opaal error traces only supported with a single thread and DFS order");
        strategy[0] |= Strat_TA;
    }

    if (PINS_POR && (strategy[0] & Strat_LTL & ~Strat_DFSFIFO)) {
        if (HREpeers(HREglobal()) > 1 && (strategy[0] & ~Strat_CNDFS))
            Abort ("POR with more than one worker only works in CNDFS!");
        if (proviso == Proviso_None) {
            Warning (info, "Forcing use of the an ignoring proviso");
            proviso = strategy[0] & Strat_CNDFS ? Proviso_CNDFS : Proviso_Stack;
        }
        if (proviso != Proviso_ForceNone) {
            if ((strategy[0] & Strat_CNDFS) && proviso != Proviso_CNDFS)
                Abort ("Only the CNDFS proviso works in CNDFS (use --proviso=cndfs)!");
            if ((strategy[0] & Strat_NDFS) && proviso != Proviso_Stack)
                Abort ("Only the stack proviso works in NDFS!");
            if ( (strategy[0] & (Strat_OWCTY|Strat_LNDFS|Strat_ENDFS)) )
                Abort ("No POR proviso supported in OWCTY/LNDFS/ENDFS!");
        }
    }

    if (PINS_POR && (strategy[0] & Strat_Reach) && (inv_detect || act_detect)) {
        if (proviso == Proviso_None) {
            Warning (info, "Forcing use of the an ignoring proviso");
            proviso = strategy[0] & Strat_DFS ? Proviso_Stack : Proviso_ClosedSet;
        }
        if (proviso != Proviso_ForceNone) {
            if ((strategy[0] & Strat_DFS) && proviso != Proviso_Stack && proviso != Proviso_ClosedSet)
                Abort ("Only the Stack/ClosedSet proviso works in DFS!");
            if ((strategy[0] & ~Strat_DFS) && proviso != Proviso_ClosedSet)
                Abort ("Only the ClosedSet proviso works in (S)BFS!");
            if (proviso == Proviso_Stack && W > 1)
                Abort ("Stack proviso not implemented for parallel dfs.");
        }
    }

    if (proviso == Proviso_ForceNone) {
        proviso = Proviso_None;
    } else if (PINS_POR && (strategy[0] & Strat_Reach) && proviso != Proviso_None &&
               act_detect == NULL && inv_detect == NULL && !NO_L12) {
        Warning (info, "POR layer will ignore ignoring proviso in absence of safety property (--invariant or --action). To enforce the (stronger) proviso, use: --no-L12.");
    }

    if (!ecd && strategy[1] != Strat_None)
        Abort ("Conflicting options --no-ecd and %s.", key_search(strategies, strategy[1]));

    if (Perm_Unknown == permutation) //default permutation depends on strategy
        permutation = strategy[0] & Strat_Reach ? Perm_None :
                     (strategy[0] & (Strat_TA | Strat_DFSFIFO) ? Perm_RR : Perm_Dynamic);
    if (Perm_None != permutation && refs == 0) {
        Warning (info, "Forcing use of references to enable fast successor permutation.");
        refs = 1; //The permuter works with references only!
    }

    if (!(Strat_Reach & strategy[0]) && (dlk_detect || act_detect || inv_detect)) {
        Abort ("Verification of safety properties works only with reachability algorithms.");
    }
}

void
print_options (model_t model)
{
    Warning (info, "There are %zu state labels and %zu edge labels", SL, EL);
    Warning (info, "State length is %zu, there are %zu groups", N, K);
    if (act_detect)
        Warning(info, "Detecting action \"%s\"", act_detect);
    Warning (info, "Running %s using %zu %s", key_search(strategies, strategy[0] & ~Strat_TA),
             W, W == 1 ? "core (sequential)" : "cores");
    if (db_type == HashTable) {
        Warning (info, "Using a hash table with 2^%d elements", dbs_size);
    } else
        Warning (info, "Using a%s tree table with 2^%d elements", indexing ? "" : " non-indexing", dbs_size);
    Warning (info, "Successor permutation: %s", key_search(permutations, permutation));
    if (PINS_POR) {
        int            *visibility = GBgetPorGroupVisibility (model);
        size_t          visibles = 0, labels = 0;
        for (size_t i = 0; i < K && visibility; i++)
            visibles += visibility[i];
        visibility = GBgetPorStateLabelVisibility (model);
        for (size_t i = 0; i < SL && visibility; i++)
            labels += visibility[i];
        Warning (info, "Visible groups: %zu / %zu, labels: %zu / %zu", visibles, K, labels, SL);
        Warning (info, "POR cycle proviso: %s %s", key_search(provisos, proviso), strategy[0] & Strat_LTL ? "(ltl)" : "");
    }
}

/*************************************************************************/
/* Popt                                                                  */
/*************************************************************************/

static const char      *arg_proviso = "none";
static char            *arg_strategy = "none";

static void
alg_popt (poptContext con, enum poptCallbackReason reason,
          const struct poptOption *opt, const char *arg, void *data)
{
    int                 res;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
        int i = 0, begin = 0, end = 0;
        char *strat = strdup (arg_strategy);
        char last;
        do {
            if (i > 0 && !((Strat_ENDFS | Strat_OWCTY | Strat_UFSCC) & strategy[i-1]))
                Abort ("Only ENDFS/OWCTY/UFSCC can use secondary search procedure procedures.");
            while (',' != arg_strategy[end] && '\0' != arg_strategy[end]) ++end;
            last = strat[end];
            strat[end] = '\0';
            res = linear_search (strategies, &strat[begin]);
            if (res < 0)
                Abort ("unknown search strategy %s", &strat[begin]);
            strategy[i++] = res;
            end += 1;
            begin = end;
        } while ('\0' != last && i < MAX_STRATEGIES);
        free (strat); // strdup
        if (Strat_OWCTY == strategy[0]) {
            if (ecd && Strat_None == strategy[1]) {
                Warning (info, "Defaulting to MAP as OWCTY early cycle detection procedure.");
                strategy[1] = Strat_MAP;
            }
        }
        if (Strat_ENDFS == strategy[i-1]) {
            if (MAX_STRATEGIES == i)
                Abort ("Open-ended recursion in ENDFS repair strategies.");
            Warning (info, "Defaulting to NDFS as ENDFS repair procedure.");
            strategy[i] = Strat_NDFS;
        }

        int p = proviso = linear_search (provisos, arg_proviso);
        if (p < 0) Abort ("unknown proviso %s", arg_proviso);
        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to alg_popt");
    (void)con; (void)opt; (void)arg; (void)data;
}

struct poptOption alg_options_extra[] = {
    {"trace", 0, POPT_ARG_STRING, &trc_output, 0, "file to write trace to", "<lts output>" },
    {"write-state", 0, POPT_ARG_VAL, &write_state, 1, "write the full state vector", NULL },
    {"filter" , 0 , POPT_ARG_STRING , &label_filter , 0 ,
     "Select the labels to be written to file from the state vector elements, "
     "state labels and edge labels." , "<patternlist>" },
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, reach_options, 0, "Reachability options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, state_store_options, 0, "State store options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, perm_options, 0, "Permutation options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, alg_ltl_options, 0, /*"LTL options"*/ NULL, NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, ndfs_options, 0, /*"NDFS options"*/ NULL, NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "PINS options", NULL},
    {NULL, 0 ,POPT_ARG_INCLUDE_TABLE, ltl_options, 0, "LTL options", NULL },
    POPT_TABLEEND
};

struct poptOption options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)alg_popt, 0, NULL, NULL},
    {"strategy", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_strategy, 0, "select the search strategy", "<bfs|sbfs|dfs|cndfs|lndfs|endfs|endfs,<strategy>|ndfs>"},
    {"proviso", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &arg_proviso , 0 ,
     "select proviso for LTL+POR or safety+POR", "<force-none|closed-set|stack|cndfs>"},
    {"inhibit", 0, POPT_ARG_VAL, &inhibit, 1, "Obey the inhibit matrix if the model defines it.", NULL },
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, alg_options_extra, 0, NULL, NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, dfs_fifo_options, 0, "DFS FIFO options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, owcty_options, 0, /*"OWCTY options"*/NULL, NULL},
     POPT_TABLEEND
};

struct poptOption options_timed[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)alg_popt, 0, NULL, NULL},
    {"strategy", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_strategy, 0, "select the search strategy", "<sbfs|bfs|dfs|cndfs>"},
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, alg_options_extra, 0, NULL, NULL},
     {NULL, 0, POPT_ARG_INCLUDE_TABLE, timed_options, 0, "Timed Automata options", NULL},
    POPT_TABLEEND
};

