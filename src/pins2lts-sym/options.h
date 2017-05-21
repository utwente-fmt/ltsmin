#ifndef REACH_OPTIONS_H
#define REACH_OPTIONS_H

#include <limits.h>
#include <popt.h>

extern struct poptOption lace_options[];

extern struct poptOption options[];

extern int REL_PERF;

extern int MAYBE_AND_FALSE_IS_FALSE;

typedef enum {
    BFS_P,
    BFS,
#ifdef HAVE_SYLVAN
    PAR,
    PAR_P,
#endif
    CHAIN_P,
    CHAIN,
    NONE
} strategy_t;

extern strategy_t strategy;

typedef enum { NO_SAT, SAT_LIKE, SAT_LOOP, SAT_FIX, SAT } sat_strategy_t;

extern sat_strategy_t sat_strategy;

typedef enum { UNGUIDED, DIRECTED } guide_strategy_t;

extern guide_strategy_t guide_strategy;

extern char** ctl_star_formulas;
extern char** ctl_formulas;
extern char** ltl_formulas;
extern int num_ctl_star;
extern int num_ctl;
extern int num_ltl;
extern char** mu_formulas;
extern int num_mu;
extern int num_total;
extern int mu_par;
extern int mu_opt;

extern char* dot_dir;
extern char* vset_dir;

extern char* trc_output;
extern char* trc_type;
extern int   dlk_detect;
extern char* act_detect;
extern char** inv_detect;
extern int   num_inv;
extern int   no_exit;
extern int   no_matrix;
extern int   peak_nodes;
extern int   no_soundness_check;
extern int   act_index;
extern int   act_label;
extern int   action_typeno;
extern int   ErrorActions; // count number of found errors (action/deadlock/invariant)
extern int   precise;
extern int   next_union;

extern int   sat_granularity;
extern int   save_sat_levels;

extern int   pgsolve_flag;
extern char* pg_output;

extern int inv_par;
extern int inv_bin_par;

extern size_t lace_n_workers;
extern size_t lace_dqsize; // can be very big, no problemo
extern size_t lace_stacksize; // use default

#endif

