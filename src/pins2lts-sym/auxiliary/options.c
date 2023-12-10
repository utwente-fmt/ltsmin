#include <hre/config.h>

#include <popt.h>

#include <pins2lts-sym/aux/options.h>

#if !SPEC_MT_SAFE || defined(PROB)
int USE_PARALLELISM = 0;
#else
int USE_PARALLELISM = 1;
#endif

int REL_PERF = SPEC_REL_PERF;

int MAYBE_AND_FALSE_IS_FALSE = SPEC_MAYBE_AND_FALSE_IS_FALSE;

strategy_t strategy = BFS_P;

sat_strategy_t sat_strategy = NO_SAT;

guide_strategy_t guide_strategy = UNGUIDED;

char** ctl_star_formulas = NULL;
char** ctl_formulas = NULL;
char** ltl_formulas = NULL;
int num_ctl_star = 0;
int num_ctl = 0;
int num_ltl = 0;
char** mu_formulas  = NULL;
int num_mu = 0;
int num_total = 0;
int mu_par = 0;
int mu_opt = 0;

char* dot_dir = NULL;
char* vset_dir = NULL;

char* trc_output = NULL;
char* trc_type   = "gcf";
int   dlk_detect = 0;
char* act_detect = NULL;
char** inv_detect = NULL;
int   num_inv = 0;
int   no_exit = 0;
int   no_matrix = 0;
int   peak_nodes = 0;
int   no_soundness_check = 0;
int   act_index;
int   act_label;
int   action_typeno;
int   ErrorActions = 0; // count number of found errors (action/deadlock/invariant)
int   precise = 0;
int   next_union = 0;

int   sat_granularity = 10;
int   save_sat_levels = 0;

int   pgsolve_flag = 0;
char* pg_output = NULL;

int inv_par = 0;
int inv_bin_par = 0;

size_t lace_n_workers = 0;
size_t lace_dqsize = 40960000; // can be very big, no problemo
size_t lace_stacksize = 0; // use default

static char* order = "bfs-prev";
static si_map_entry ORDER[] = {
    {"bfs-prev", BFS_P},
    {"bfs", BFS},
#ifdef HAVE_SYLVAN
    {"par", PAR},
    {"par-prev", PAR_P},
#endif
    {"chain-prev", CHAIN_P},
    {"chain", CHAIN},
    {"none", NONE},
    {NULL, 0}
};

static char* saturation = "none";
static si_map_entry SATURATION[] = {
    {"none", NO_SAT},
    {"sat-like", SAT_LIKE},
    {"sat-loop", SAT_LOOP},
    {"sat-fix", SAT_FIX},
    {"sat", SAT},
    {NULL, 0}
};

static char *guidance = "unguided";
static si_map_entry GUIDED[] = {
    {"unguided", UNGUIDED},
    {"directed", DIRECTED},
    {NULL, 0}
};

static const char invariant_long[]="invariant";
static const char ctl_star_long[]="ctl-star";
static const char ctl_long[]="ctl";
static const char ltl_long[]="ltl";
static const char mu_long[]="mu";
#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

static void
reach_popt(poptContext con, enum poptCallbackReason reason,
               const struct poptOption * opt, const char * arg, void * data)
{
    (void)con; (void)opt; (void)arg; (void)data;

    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        Abort("unexpected call to reach_popt");
    case POPT_CALLBACK_REASON_POST: {
        int res;

        res = linear_search(ORDER, order);
        if (res < 0) {
            Warning(lerror, "unknown exploration order %s", order);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Exploration order is %s", order);
        }
        strategy = res;

        res = linear_search(SATURATION, saturation);
        if (res < 0) {
            Warning(lerror, "unknown saturation strategy %s", saturation);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Saturation strategy is %s", saturation);
        }
        sat_strategy = res;

        res = linear_search(GUIDED, guidance);
        if (res < 0) {
            Warning(lerror, "unknown guided search strategy %s", guidance);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Guided search strategy is %s", guidance);
        }
        guide_strategy = res;

        if (trc_output != NULL && !dlk_detect && act_detect == NULL && HREme(HREglobal())==0)
            Warning(info, "Ignoring trace output");

        if (inv_bin_par == 1 && inv_par == 0) {
            Warning(lerror, "--inv-bin-par requires --inv-par");
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        }

        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        IF_LONG(invariant_long) {
            num_inv++;
            inv_detect = (char**) RTrealloc(inv_detect, sizeof(char*) * num_inv);
            inv_detect[num_inv - 1] = (char*) RTmalloc(strlen(arg) + 1);
            memcpy(inv_detect[num_inv - 1], arg, strlen(arg) + 1);
        }
        IF_LONG(ctl_star_long) {
            num_ctl_star++;
            ctl_star_formulas = (char**) RTrealloc(ctl_star_formulas, sizeof(char*) * num_ctl_star);
            ctl_star_formulas[num_ctl_star - 1] = (char*) RTmalloc(strlen(arg) + 1);
            memcpy(ctl_star_formulas[num_ctl_star - 1], arg, strlen(arg) + 1);
        }
        IF_LONG(ctl_long) {
            num_ctl++;
            ctl_formulas = (char**) RTrealloc(ctl_formulas, sizeof(char*) * num_ctl);
            ctl_formulas[num_ctl - 1] = (char*) RTmalloc(strlen(arg) + 1);
            memcpy(ctl_formulas[num_ctl - 1], arg, strlen(arg) + 1);
        }
        IF_LONG(ltl_long) {
            num_ltl++;
            ltl_formulas = (char**) RTrealloc(ltl_formulas, sizeof(char*) * num_ltl);
            ltl_formulas[num_ltl - 1] = (char*) RTmalloc(strlen(arg) + 1);
            memcpy(ltl_formulas[num_ltl - 1], arg, strlen(arg) + 1);
        }
        IF_LONG(mu_long) {
            num_mu++;
            mu_formulas = (char**) RTrealloc(mu_formulas, sizeof(char*) * num_mu);
            mu_formulas[num_mu - 1] = (char*) RTmalloc(strlen(arg) + 1);
            memcpy(mu_formulas[num_mu - 1], arg, strlen(arg) + 1);
        }
        return;
    }
}

struct poptOption lace_options[] = {
    { "lace-workers", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_n_workers , 0 , "set number of Lace workers (threads for parallelization)","<workers>"},
    { "lace-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_dqsize , 0 , "set length of Lace task queue","<dqsize>"},
    { "lace-stacksize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_stacksize, 0, "set size of program stack in kilo bytes (0=default stack size)", "<stacksize>"},
POPT_TABLEEND
};

struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST , (void*)reach_popt , 0 , NULL , NULL },
    {   "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , 
        "<bfs-prev|bfs|chain-prev|chain"
#ifdef HAVE_SYLVAN
            "|par-prev|par" 
#endif
            "|none>" },
#ifdef HAVE_SYLVAN
    { "inv-par", 0, POPT_ARG_VAL, &inv_par, 1, "parallelize invariant detection", NULL },
    { "inv-bin-par", 0, POPT_ARG_VAL, &inv_bin_par, 1, "also parallelize every binary operand, may be slow when lots of state labels are to be evaluated (requires --inv-par)", NULL },
    { "mu-par", 0, POPT_ARG_VAL, &mu_par, 1, "parallelize mu-calculus", NULL },
#endif
    { "mu-opt", 0, POPT_ARG_VAL, &mu_opt, 1, "optimize fix-point calculations in mu-calculus", NULL },

    { "saturation" , 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &saturation , 0 , "select the saturation strategy" , "<none|sat-like|sat-loop|sat-fix|sat>" },
    { "sat-granularity" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sat_granularity , 0 , "set saturation granularity","<number>" },
    { "save-sat-levels", 0, POPT_ARG_VAL, &save_sat_levels, 1, "save previous states seen at saturation levels", NULL },
    { "guidance", 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &guidance, 0 , "select the guided search strategy" , "<unguided|directed>" },
    { "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
    { "action" , 0 , POPT_ARG_STRING , &act_detect , 0 , "detect action prefix" , "<action prefix>" },
    { invariant_long , 'i' , POPT_ARG_STRING , NULL , 0, "detect invariant violations (can be given multiple times)", NULL },
    { "no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
    { "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts-file>" },
    { "type", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &trc_type, 0, "trace type to write", "<aut|gcd|gcf|dir|fsm|bcg>" },
    { mu_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a MU-calculus formula  (can be given multiple times)" , "<mu-file>.mu" },
    { ctl_star_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a CTL* formula  (can be given multiple times)" , "<ctl-star-file>.ctl" },
    { ctl_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a CTL formula  (can be given multiple times)" , "<ctl-file>.ctl" },
    { ltl_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with an LTL formula  (can be given multiple times)" , "<ltl-file>.ltl" },
    { "dot", 0, POPT_ARG_STRING, &dot_dir, 0, "directory to write dot representation of vector sets to", NULL },
    { "save-levels", 0, POPT_ARG_STRING, &vset_dir, 0, "directory to write vset snapshots of all levels to", NULL },
    { "pg-solve" , 0 , POPT_ARG_NONE , &pgsolve_flag, 0, "Solve the generated parity game (only for symbolic tool).","" },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, spg_solve_options , 0, "Symbolic parity game solver options", NULL},
    { "pg-write" , 0 , POPT_ARG_STRING , &pg_output, 0, "file to write symbolic parity game to","<pg-file>.spg" },
#ifdef HAVE_SYLVAN
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lace_options , 0 , "Lace options",NULL},
#endif
    { "no-matrix" , 0 , POPT_ARG_VAL , &no_matrix , 1 , "do not print the dependency matrix when -v (verbose) is used" , NULL},
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "PINS options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
    { "no-soundness-check", 0, POPT_ARG_VAL, &no_soundness_check, 1, "disable checking whether the model specification is sound for guards", NULL },
    { "precise", 0, POPT_ARG_NONE, &precise, 0, "Compute the final number of states precisely", NULL},
    { "next-union", 0, POPT_ARG_NONE, &next_union, 0, "While computing successor states; unify simultaneously with current states", NULL },
    { "peak-nodes", 0, POPT_ARG_NONE, &peak_nodes, 0, "record peak nodes and report after reachability analysis", NULL },
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, maxsum_options, 0, "Integer arithmetic options", NULL},
    POPT_TABLEEND
};


/// GLOBALS

/*
  The inhibit and class matrices are used for maximal progress.
 */
matrix_t *inhibit_matrix=NULL;
matrix_t *class_matrix=NULL;
int inhibit_class_count=0;
vset_t *class_enabled = NULL;

bitvector_ll_t *seen_actions;
vset_t true_states;
vset_t false_states;

#ifdef LTSMIN_PBES
bool is_pbes_tool = true;
#else
bool is_pbes_tool = false;
#endif
int var_pos = 0;
int var_type_no = 0;
int variable_projection = 0;
size_t true_index = 0;
size_t false_index = 1;
size_t num_vars = 0;
int* player = 0; // players of variables
int* priority = 0; // priorities of variables
int min_priority = INT_MAX;
int max_priority = INT_MIN;

ltsmin_expr_t* mu_exprs = NULL;
ltsmin_parse_env_t* mu_parse_env = NULL;

lts_type_t ltstype;
int N;
int eLbls;
int sLbls;
int nGuards;
int nGrps;
int max_sat_levels;
ci_list **r_projs = NULL;
ci_list **w_projs = NULL;
ci_list **l_projs = NULL;
vdom_t domain;
matrix_t *read_matrix;
matrix_t *write_matrix;
vset_t *levels = NULL;
int max_levels = 0;
int global_level;
long max_lev_count = 0;
long max_vis_count = 0;
long max_grp_count = 0;
long max_trans_count = 0;
long max_mu_count = 0;
model_t model;
vset_t initial, visited;
vrel_t *group_next;
vset_t *group_explored;
vset_t *group_tmp;
vset_t *label_false = NULL; // 0
vset_t *label_true = NULL;  // 1
vset_t *label_tmp;
rt_timer_t reach_timer;

int* label_locks = NULL;

ltsmin_parse_env_t* inv_parse_env;
ltsmin_expr_t* inv_expr;
ci_list **inv_proj = NULL;
vset_t* inv_set = NULL;
int* inv_violated = NULL;
bitvector_t* inv_deps = NULL;
bitvector_t* inv_sl_deps = NULL;
int num_inv_violated = 0;
bitvector_t state_label_used;

transitions_t transitions_short = NULL; // which function to call for the next states.

