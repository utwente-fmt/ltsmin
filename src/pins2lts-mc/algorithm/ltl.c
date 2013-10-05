/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/ltl.h>

int              ecd = 1;

struct poptOption alg_ltl_options[] = {
    {"no-ecd", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &ecd, 0,
     "turn off early cycle detection (NNDFS/MCNDFS)", NULL},
    POPT_TABLEEND
};

