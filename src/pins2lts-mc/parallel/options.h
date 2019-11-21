/**
 *
 */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <hre/runtime.h>

#include <popt.h>
#include <stdbool.h>
#include <stdlib.h>

typedef enum {
    Proviso_None,
    Proviso_ForceNone,
    Proviso_ClosedSet,
    Proviso_Stack,
    Proviso_CNDFS,
} proviso_t;

typedef enum {
    Strat_None   = 0,
    Strat_SBFS   = 1,
    Strat_BFS    = 2,
    Strat_DFS    = 4,
    Strat_PBFS   = 8,
    Strat_NDFS   = 16,
    Strat_LNDFS  = 32,
    Strat_ENDFS  = 64,
    Strat_CNDFS  = 128,
    Strat_OWCTY  = 256,
    Strat_MAP    = 512,
    Strat_ECD    = 1024,
    Strat_DFSFIFO= 2048, // Not exactly LTL, but uses accepting states (for now) and random order
    Strat_TA     = 4096,
    Strat_TARJAN = 8192,
    Strat_UFSCC  = 16384,
    Strat_RENAULT= 32768,
    Strat_TA_SBFS= Strat_SBFS | Strat_TA,
    Strat_TA_BFS = Strat_BFS | Strat_TA,
    Strat_TA_DFS = Strat_DFS | Strat_TA,
    Strat_TA_PBFS= Strat_PBFS | Strat_TA,
    Strat_TA_CNDFS= Strat_CNDFS | Strat_TA,
    Strat_2Stacks= Strat_BFS | Strat_SBFS | Strat_CNDFS | Strat_ENDFS | Strat_DFSFIFO | Strat_OWCTY,
    Strat_LTLG   = Strat_LNDFS | Strat_ENDFS | Strat_CNDFS,
    Strat_SCC    = Strat_TARJAN | Strat_UFSCC | Strat_RENAULT,
    Strat_LTL    = Strat_NDFS | Strat_LTLG | Strat_OWCTY | Strat_DFSFIFO | Strat_SCC,
    Strat_Reach  = Strat_BFS | Strat_SBFS | Strat_DFS | Strat_PBFS
} strategy_t;

extern si_map_entry strategies[];
extern si_map_entry provisos[];

extern void options_static_init        (model_t model, bool timed);

extern void print_options               (model_t model);

#define                 MAX_STRATEGIES 5
extern strategy_t       strategy[];
extern proviso_t        proviso;
extern int              no_exit;

extern char*            trc_output;
extern int              write_state;
extern int              inhibit;
extern char*            label_filter;
extern char            *files[];

extern struct poptOption options[];
extern struct poptOption options_timed[];

#endif // OPTIONS_H
