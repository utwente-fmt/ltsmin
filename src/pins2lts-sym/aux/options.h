#ifndef SYM_OPTIONS_H
#define SYM_OPTIONS_H

#include <limits.h>
#include <popt.h>
#include <stdbool.h>


#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/bitvector-ll.h>
#include <pins2lts-sym/maxsum/maxsum.h>
#include <pins2lts-sym/aux/options.h>
#include <pins-lib/pins-impl.h>
#include <spg-lib/spg-options.h>
#include <vset-lib/vector_set.h>
#include <util-lib/util.h>

extern struct poptOption lace_options[];

extern struct poptOption options[];

extern int USE_PARALLELISM;

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


/// GLOBALS


extern matrix_t *inhibit_matrix;
extern matrix_t *class_matrix;
extern int inhibit_class_count;
extern vset_t *class_enabled;

extern bitvector_ll_t *seen_actions;
extern vset_t true_states;
extern vset_t false_states;

extern bool is_pbes_tool;
extern int var_pos;
extern int var_type_no;
extern int variable_projection;
extern size_t true_index;
extern size_t false_index;
extern size_t num_vars;
extern int* player; // players of variables
extern int* priority; // priorities of variables
extern int min_priority;
extern int max_priority;

ltsmin_expr_t* mu_exprs;
ltsmin_parse_env_t* mu_parse_env;

extern lts_type_t ltstype;
extern int N;
extern int eLbls;
extern int sLbls;
extern int nGuards;
extern int nGrps;
extern int max_sat_levels;
extern ci_list **r_projs;
extern ci_list **w_projs;
extern ci_list **l_projs;
extern vdom_t domain;
extern matrix_t *read_matrix;
extern matrix_t *write_matrix;
extern vset_t *levels;
extern int max_levels;
extern int global_level;
extern long max_lev_count;
extern long max_vis_count;
extern long max_grp_count;
extern long max_trans_count;
extern long max_mu_count;
extern model_t model;
extern vset_t initial, visited;
extern vrel_t *group_next;
extern vset_t *group_explored;
extern vset_t *group_tmp;
extern vset_t *label_false; // 0
extern vset_t *label_true;  // 1
extern vset_t *label_tmp;
extern rt_timer_t reach_timer;

extern int* label_locks;

extern ltsmin_parse_env_t* inv_parse_env;
extern ltsmin_expr_t* inv_expr;
extern ci_list **inv_proj;
extern vset_t* inv_set;
extern int* inv_violated;
extern bitvector_t* inv_deps;
extern bitvector_t* inv_sl_deps;
extern int num_inv_violated;
extern bitvector_t state_label_used;

typedef int (*transitions_t)(model_t model,int group,int*src,TransitionCB cb,void*context);

extern transitions_t transitions_short; // which function to call for the next states.

typedef int(*vset_count_t)(vset_t set, long* nodes, long double* elements);

extern vset_count_t vset_count_fn;

typedef void(*vset_next_t)(vset_t dst, vset_t src, vrel_t rel);

extern vset_next_t vset_next_fn;

#endif // SYM_OPTIONS_H

