#include <hre/config.h>

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <float.h>
#include <alloca.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pg-types.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-guards.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/pins2pins-mucalc.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/property-semantics.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/bitvector-ll.h>
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>
#include <util-lib/dynamic-array.h>
#include <util-lib/bitset.h>
#include <hre/stringindex.h>
#include <mc-lib/atomics.h>

#include <sylvan.h>

hre_context_t ctx;

static ltsmin_expr_t* mu_exprs = NULL;
static char** ctl_star_formulas = NULL;
static char** ctl_formulas = NULL;
static char** ltl_formulas = NULL;
static int num_ctl_star = 0;
static int num_ctl = 0;
static int num_ltl = 0;
static char** mu_formulas  = NULL;
static int num_mu = 0;
static int num_total = 0;
static int mu_par = 0;
static ltsmin_parse_env_t* mu_parse_env = NULL;

static char* dot_dir = NULL;

static char* transitions_save_filename = NULL;
static char* transitions_load_filename = NULL;
static int save_reachable = 0; // save reachable states too in --save-transitions

static char* trc_output = NULL;
static char* trc_type   = "gcf";
static int   dlk_detect = 0;
static char* act_detect = NULL;
static char** inv_detect = NULL;
static int   num_inv = 0;
static int   no_exit = 0;
static int   no_matrix = 0;
static int   peak_nodes = 0;
static int   no_soundness_check = 0;
static int   act_index;
static int   act_label;
static int   action_typeno;
static int   ErrorActions = 0; // count number of found errors (action/deadlock/invariant)
static int   precise = 0;
static int   next_union = 0;

static bitvector_ll_t *seen_actions;

static int   sat_granularity = 10;
static int   save_sat_levels = 0;

static int   pgsolve_flag = 0;
static char* pg_output = NULL;
static int var_pos = 0;
static int var_type_no = 0;
static int variable_projection = 0;
static size_t true_index = 0;
static size_t false_index = 1;
static size_t num_vars = 0;
static int* player = 0; // players of variables
static int* priority = 0; // priorities of variables
static int min_priority = INT_MAX;
static int max_priority = INT_MIN;
static vset_t true_states;
static vset_t false_states;
static int inv_par = 0;
static int inv_bin_par = 0;

/*
  The inhibit and class matrices are used for maximal progress.
 */
static matrix_t *inhibit_matrix=NULL;
static matrix_t *class_matrix=NULL;
static int inhibit_class_count=0;
static vset_t *class_enabled = NULL;

static enum {
    BFS_P,
    BFS,
    PAR,
    PAR_P,
    CHAIN_P,
    CHAIN,
    NONE
} strategy = BFS_P;

static int expand_groups = 1; // set to 0 if transitions are loaded from file

static size_t lace_n_workers = 0;
static size_t lace_dqsize = 40960000; // can be very big, no problemo
static size_t lace_stacksize = 0; // use default

static char* order = "bfs-prev";
static si_map_entry ORDER[] = {
    {"bfs-prev", BFS_P},
    {"bfs", BFS},
    {"par", PAR},
    {"par-prev", PAR_P},
    {"chain-prev", CHAIN_P},
    {"chain", CHAIN},
    {"none", NONE},
    {NULL, 0}
};

static enum { NO_SAT, SAT_LIKE, SAT_LOOP, SAT_FIX, SAT } sat_strategy = NO_SAT;

static char* saturation = "none";
static si_map_entry SATURATION[] = {
    {"none", NO_SAT},
    {"sat-like", SAT_LIKE},
    {"sat-loop", SAT_LOOP},
    {"sat-fix", SAT_FIX},
    {"sat", SAT},
    {NULL, 0}
};

static enum { UNGUIDED, DIRECTED } guide_strategy = UNGUIDED;

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
            Warning(error, "unknown exploration order %s", order);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Exploration order is %s", order);
        }
        strategy = res;

        res = linear_search(SATURATION, saturation);
        if (res < 0) {
            Warning(error, "unknown saturation strategy %s", saturation);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Saturation strategy is %s", saturation);
        }
        sat_strategy = res;

        res = linear_search(GUIDED, guidance);
        if (res < 0) {
            Warning(error, "unknown guided search strategy %s", guidance);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else if (HREme(HREglobal())==0) {
            Warning(info, "Guided search strategy is %s", guidance);
        }
        guide_strategy = res;

        if (trc_output != NULL && !dlk_detect && act_detect == NULL && HREme(HREglobal())==0)
            Warning(info, "Ignoring trace output");

        if (inv_bin_par == 1 && inv_par == 0) {
            Warning(error, "--inv-bin-par requires --inv-par");
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

static struct poptOption lace_options[] = {
    { "lace-workers", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_n_workers , 0 , "set number of Lace workers (threads for parallelization)","<workers>"},
    { "lace-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_dqsize , 0 , "set length of Lace task queue","<dqsize>"},
    { "lace-stacksize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &lace_stacksize, 0, "set size of program stack in kilo bytes (0=default stack size)", "<stacksize>"},
POPT_TABLEEND
};

static  struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST , (void*)reach_popt , 0 , NULL , NULL },
    { "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , "<bfs-prev|bfs|chain-prev|chain|par-prev|par|none>" },
    { "inv-par", 0, POPT_ARG_VAL, &inv_par, 1, "parallelize invariant detection", NULL },
    { "inv-bin-par", 0, POPT_ARG_VAL, &inv_bin_par, 1, "also parallelize every binary operand, may be slow when lots of state labels are to be evaluated (requires --inv-par)", NULL },
    { "mu-par", 0, POPT_ARG_VAL, &inv_par, 1, "parallelize mu-calculus", NULL },
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
    { "save-transitions", 0 , POPT_ARG_STRING, &transitions_save_filename, 0, "file to write transition relations to", "<outputfile>" },
    { "save-reachable", 0, POPT_ARG_NONE, &save_reachable, 0, "when saving transitions, also save reachable states", 0 },
    { "load-transitions", 0 , POPT_ARG_STRING, &transitions_load_filename, 0, "file to read transition relations from", "<inputfile>" },
    { mu_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a MU-calculus formula  (can be given multiple times)" , "<mu-file>.mu" },
    { ctl_star_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a CTL* formula  (can be given multiple times)" , "<ctl-star-file>.ctl" },
    { ctl_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with a CTL formula  (can be given multiple times)" , "<ctl-file>.ctl" },
    { ltl_long , 0 , POPT_ARG_STRING , NULL , 0 , "file with an LTL formula  (can be given multiple times)" , "<ltl-file>.ltl" },
    { "dot", 0, POPT_ARG_STRING, &dot_dir, 0, "directory to write dot representation of vector sets to", NULL },
    { "pg-solve" , 0 , POPT_ARG_NONE , &pgsolve_flag, 0, "Solve the generated parity game (only for symbolic tool).","" },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, spg_solve_options , 0, "Symbolic parity game solver options", NULL},
    { "pg-write" , 0 , POPT_ARG_STRING , &pg_output, 0, "file to write symbolic parity game to","<pg-file>.spg" },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lace_options , 0 , "Lace options",NULL},
    { "no-matrix" , 0 , POPT_ARG_VAL , &no_matrix , 1 , "do not print the dependency matrix when -v (verbose) is used" , NULL},
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "PINS options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
    { "no-soundness-check", 0, POPT_ARG_VAL, &no_soundness_check, 1, "disable checking whether the model specification is sound for guards", NULL },
    { "precise", 0, POPT_ARG_NONE, &precise, 0, "Compute the final number of states precisely", NULL},
    { "next-union", 0, POPT_ARG_NONE, &next_union, 0, "While computing successor states; unify simultaneously with current states", NULL },
    { "peak-nodes", 0, POPT_ARG_NONE, &peak_nodes, 0, "record peak nodes and report after reachability analysis", NULL },
    POPT_TABLEEND
};

typedef struct {
    int len;
    int *proj;
} proj_info;

static lts_type_t ltstype;
static int N;
static int eLbls;
static int sLbls;
static int nGuards;
static int nGrps;
static int max_sat_levels;
static proj_info *r_projs = NULL;
static proj_info *w_projs = NULL;
static proj_info *l_projs = NULL;
static vdom_t domain;
static vset_t *levels = NULL;
static int max_levels = 0;
static int global_level;
static long max_lev_count = 0;
static long max_vis_count = 0;
static long max_grp_count = 0;
static long max_trans_count = 0;
static model_t model;
static vrel_t *group_next;
static vset_t *group_explored;
static vset_t *group_tmp;
static vset_t *label_false = NULL; // 0
static vset_t *label_true = NULL;  // 1
static vset_t *label_tmp;
static rt_timer_t reach_timer;

static int* label_locks = NULL;

static ltsmin_parse_env_t* inv_parse_env;
static ltsmin_expr_t* inv_expr;
static proj_info* inv_proj = NULL;
static vset_t* inv_set = NULL;
static int* inv_violated = NULL;
static bitvector_t* inv_deps = NULL;
static bitvector_t* inv_sl_deps = NULL;
static int num_inv_violated = 0;
static bitvector_t state_label_used;

typedef void (*reach_proc_t)(vset_t visited, vset_t visited_old,
                             bitvector_t *reach_groups,
                             long *eg_count, long *next_count, long *guard_count);

typedef void (*sat_proc_t)(reach_proc_t reach_proc, vset_t visited,
                           bitvector_t *reach_groups,
                           long *eg_count, long *next_count, long *guard_count);

typedef void (*guided_proc_t)(sat_proc_t sat_proc, reach_proc_t reach_proc,
                              vset_t visited, char *etf_output);

typedef int (*transitions_t)(model_t model,int group,int*src,TransitionCB cb,void*context);

static transitions_t transitions_short = NULL; // which function to call for the next states.

typedef int(*vset_count_t)(vset_t set, long* nodes, long double* elements);

static int vset_count_dbl(vset_t set, long* nodes, long double* elements);
static int vset_count_ldbl(vset_t set, long* nodes, long double* elements);

static vset_count_t vset_count_fn = &vset_count_dbl;

static int
vset_count_dbl(vset_t set, long* nodes, long double* elements)
{
    if (elements != NULL) {
        *elements = 0.0;
        double e;
        vset_count(set, nodes, &e);
        *elements += e;
        if(vdom_supports_ccount(domain) && isinf(e)) {
            vset_count_fn = &vset_count_ldbl;
            vset_count_fn(set, nodes, elements);
            return LDBL_DIG;
        }
    } else {
        vset_count(set, nodes, NULL);
    }
    return DBL_DIG;
}

static int
vset_count_ldbl(vset_t set, long* nodes, long double* elements)
{
    vset_ccount(set, nodes, elements);
    return LDBL_DIG;
}

typedef void(*vset_next_t)(vset_t dst, vset_t src, vrel_t rel);

static vset_next_t vset_next_fn = vset_next;

static void
vset_next_union_src(vset_t dst, vset_t src, vrel_t rel)
{
    vset_next_union(dst, src, rel, src);
}

/*
 * Add parallel operations
 */
// join
#define vset_join_par(dst, left, right) SPAWN(vset_join_par, dst, left, right)
VOID_TASK_3(vset_join_par, vset_t, dst, vset_t, left, vset_t, right) { vset_join(dst, left, right); }

// union
#define vset_union_par(dst, src) SPAWN(vset_union_par, dst, src)
VOID_TASK_2(vset_union_par, vset_t, dst, vset_t, src) { vset_union(dst, src); }

// intersect
#define vset_intersect_par(dst, src) SPAWN(vset_intersect_par, dst, src)
VOID_TASK_2(vset_intersect_par, vset_t, dst, vset_t, src) { vset_intersect(dst, src); }

// minus
#define vset_minus_par(dst, src) SPAWN(vset_minus_par, dst, src)
VOID_TASK_2(vset_minus_par, vset_t, dst, vset_t, src) { vset_minus(dst, src); }

static inline void
reduce(int group, vset_t set)
{
    if (PINS_USE_GUARDS) {
        guard_t* guards = GBgetGuard(model, group);
        for (int g = 0; g < guards->count && !vset_is_empty(set); g++) {
            vset_join(set, set, label_true[guards->guard[g]]);
        }
    }
}

static inline void
grow_levels(int new_levels)
{
    if (global_level == max_levels) {
        max_levels += new_levels;
        levels = RTrealloc(levels, max_levels * sizeof(vset_t));

        for(int i = global_level; i < max_levels; i++)
            levels[i] = vset_create(domain, -1, NULL);
    }
}

static inline void
save_level(vset_t visited)
{
    grow_levels(1024);
    vset_copy(levels[global_level], visited);
    global_level++;
}

static void
write_trace_state(lts_file_t trace_handle, int src_no, int *state)
{
  int labels[sLbls];

  Warning(debug, "dumping state %d", src_no);

  for (int i = 0; i < sLbls; i++) {
      labels[i] = GBgetStateLabelLong(model, i, state);
  }

  lts_write_state(trace_handle, 0, state, labels);
}

struct write_trace_step_s {
    lts_file_t    trace_handle;
    int           src_no;
    int           dst_no;
    int          *dst;
    int           found;
};

static void
write_trace_next(void *arg, transition_info_t *ti, int *dst, int *cpy)
{
    struct write_trace_step_s *ctx = (struct write_trace_step_s*)arg;

    if (ctx->found)
        return;

    for(int i = 0; i < N; i++) {
        if (ctx->dst[i] != dst[i])
            return;
    }

    ctx->found = 1;
    lts_write_edge(ctx->trace_handle, 0, &ctx->src_no, 0, dst, ti->labels);
    (void)cpy;
}

static void
write_trace_step(lts_file_t trace_handle, int src_no, int *src,
                 int dst_no, int *dst)
{
    struct write_trace_step_s ctx;

    Warning(debug, "finding edge for state %d", src_no);
    ctx.trace_handle = trace_handle;
    ctx.src_no = src_no;
    ctx.dst_no = dst_no;
    ctx.dst = dst;
    ctx.found = 0;

    for (int i = 0; i < nGrps && !ctx.found; i++) {
        GBgetTransitionsLong(model, i, src, write_trace_next, &ctx);
    }

    if (!ctx.found)
        Abort("no matching transition found");
}

static void
write_trace(lts_file_t trace_handle, int **states, int total_states)
{
    // output starting from initial state, which is in states[total_states-1]

    for(int i = total_states - 1; i > 0; i--) {
        int current_step = total_states - i - 1;

        write_trace_state(trace_handle, current_step, states[i]);
        write_trace_step(trace_handle, current_step, states[i],
                         current_step + 1, states[i - 1]);
    }

    write_trace_state(trace_handle, total_states - 1, states[0]);
}

static void
find_trace_to(int trace_end[][N], int end_count, int level, vset_t *levels,
              lts_file_t trace_handle)
{
    int    prev_level   = level - 2;
    vset_t src_set      = vset_create(domain, -1, NULL);
    vset_t dst_set      = vset_create(domain, -1, NULL);
    vset_t temp         = vset_create(domain, -1, NULL);

    int   max_states    = 1024 + end_count;
    int   current_state = end_count;
    int **states        = RTmalloc(sizeof(int*[max_states]));

    for (int i = 0; i < end_count; i++)
        states[i] = trace_end[i];

    for(int i = end_count; i < max_states; i++)
        states[i] = RTmalloc(sizeof(int[N]));

    int     max_int_level  = 32;
    vset_t *int_levels     = RTmalloc(sizeof(vset_t[max_int_level]));

    for(int i = 0; i < max_int_level; i++)
        int_levels[i] = vset_create(domain, -1, NULL);

    while (prev_level >= 0) {
        int int_level = 0;

        if (vset_member(levels[prev_level], states[current_state - 1])) {
            Warning(debug, "Skipping level %d in trace generation", prev_level);
            prev_level--;
            continue;
        }

        vset_add(int_levels[0], states[current_state - 1]);

        // search backwards from states[current_state - 1] to prev_level
        do {
            int_level++;

            // grow int_levels if needed
            if (int_level == max_int_level) {
                max_int_level += 32;
                int_levels = RTrealloc(int_levels, sizeof(vset_t[max_int_level]));

                for(int i = int_level; i < max_int_level; i++)
                    int_levels[i] = vset_create(domain, -1, NULL);
            }

            for (int i=0; i < nGrps; i++) {
                vset_prev(temp, int_levels[int_level - 1], group_next[i], levels[level-1]); // just use last level as universe // TODO FIXME
                reduce(i, temp);

                vset_union(int_levels[int_level], temp);
                vset_intersect(temp, levels[prev_level]);
                if (!vset_is_empty(temp)) break; // found a good ancestor! we can leave now!
                else vset_clear(temp);
            }

            // if there was no ancestor, abort (this should be impossible!)
            if (vset_is_empty(int_levels[int_level])) Abort("Error trying to trace action!");

            // do this until we find an actual state from levels[prev_level], i.e., temp is not empty
        } while (vset_is_empty(temp));

        // grow states if needed
        if (current_state + int_level >= max_states) {
            int old_max_states = max_states;

            max_states = current_state + int_level + 1024;
            states = RTrealloc(states,sizeof(int*[max_states]));

            for(int i = old_max_states; i < max_states; i++)
                states[i] = RTmalloc(sizeof(int[N]));
        }

        vset_example(temp, states[current_state + int_level - 1]);

        // find the states that give us a trace to states[current_state - 1]
        for(int i = int_level - 1; i > 0; i--) {
            vset_clear(src_set);
            vset_add(src_set, states[current_state + i]);

            for(int j = 0; j < nGrps; j++) {
                reduce(j, temp);
                vset_next_fn(temp, src_set, group_next[j]);
                vset_union(dst_set, temp);
            }

            vset_intersect(dst_set, int_levels[i]);
            vset_minus(dst_set, src_set);
            vset_example(dst_set, states[current_state + i - 1]);
            vset_clear(src_set);
            vset_clear(dst_set);
        }

        current_state += int_level;
        prev_level--;

        for(int i = 0; i <= int_level; i++)
            vset_clear(int_levels[i]);

        vset_clear(temp);
    }

    write_trace(trace_handle, states, current_state);
}

static void
find_trace(int trace_end[][N], int end_count, int level, vset_t *levels, char* file_prefix)
{
    // Find initial state and open output file
    int             init_state[N];
    hre_context_t   n = HREctxCreate(0, 1, "blah", 0);
    lts_file_t      trace_output = lts_vset_template();
    lts_type_t      ltstype = GBgetLTStype(model);

    GBgetInitialState(model, init_state);
    lts_file_set_context(trace_output, n);

    char* file_name=alloca((5+strlen(trc_output)+strlen(file_prefix))*sizeof(char));
    sprintf(file_name, "%s%s.%s", trc_output, file_prefix, trc_type);
    Warning(info,"writing to file: %s",file_name);
    trace_output = lts_file_create(file_name, ltstype, 1, trace_output);
    lts_write_init(trace_output, 0, (uint32_t*)init_state);
    int T=lts_type_get_type_count(ltstype);
    for(int i=0;i<T;i++){
        lts_file_set_table(trace_output,i,GBgetChunkMap(model,i));
    }

    // Generate trace
    rt_timer_t  timer = RTcreateTimer();
    RTstartTimer(timer);
    find_trace_to(trace_end, end_count, level, levels, trace_output);
    RTstopTimer(timer);
    RTprintTimer(info, timer, "constructing trace for '%s' took", file_prefix);

    // Close output file
    lts_file_close(trace_output);
}

static void
find_action(int* src, int* dst, int* cpy, int group, char* action)
{
    int trace_end[2][N];

    for (int i = 0; i < N; i++) {
        trace_end[0][i] = src[i];
        trace_end[1][i] = src[i];
    }

    // Set dst of the last step of the trace to its proper value
    for (int i = 0; i < w_projs[group].len; i++) {
        int is_read = 0;
        for (int j=0; j<r_projs[group].len; j++) if (w_projs[group].proj[i] == r_projs[group].proj[j]) is_read = 1;
        if (is_read || cpy == NULL || cpy[i] == 0) {
            trace_end[0][w_projs[group].proj[i]] = dst[i];
        }
    }

    find_trace(trace_end, 2, global_level, levels, action);
}

struct label_add_info
{
    int label; // label number being evaluated
    int result; // desired result of the label
};

static void eval_cb (vset_t set, void *context, int *src)
{
    // evaluate the label
    int result = GBgetStateLabelShort(model, ((struct label_add_info*)context)->label, src);

    // add to the correct set dependening on the result
    int dresult = ((struct label_add_info*)context)->result;
    if (
            dresult == result ||  // we have true or false (just add)
            (dresult == 0 && result == 2) ||  // always add maybe to false
            (dresult == 1 && result == 2 && !no_soundness_check)) { // if we want to do soundness
            vset_add(set, src);                                     // check then also add maybe to true.
                                                                    // maybe = false \cap true
    }
}

#define eval_label(l, s) CALL(eval_label, (l), (s))
VOID_TASK_2(eval_label, int, label, vset_t, set)
{
    // get the short vectors we need to evaluate
    // minus what we have already evaluated
    vset_project_minus(label_tmp[label], set, label_false[label]);
    vset_minus(label_tmp[label], label_true[label]);

    // count when verbose
    if (log_active(infoLong)) {
        double elem_count;
        vset_count(label_tmp[label], NULL, &elem_count);
        if (elem_count >= 10000.0 * SPEC_REL_PERF) {
            Print(infoLong, "expanding label %d for %.*g states.", label, DBL_DIG, elem_count);
        }
    }

    // we evaluate labels twice, because we can not yet add to two different sets.
    struct label_add_info ctx_false;

    ctx_false.label = label;
    ctx_false.result = 0;

    // evaluate labels and add to label_false[guard] when false
    vset_update(label_false[label], label_tmp[label], eval_cb, &ctx_false);

    struct label_add_info ctx_true;

    ctx_true.label = label;
    ctx_true.result = 1;

    // evaluate labels and add to label_true[label] when true
    vset_update(label_true[label], label_tmp[label], eval_cb, &ctx_true);

    vset_clear(label_tmp[label]);
}

static inline void
learn_guards(vset_t states, long *guard_count) {
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) {
            if (guard_count != NULL) (*guard_count)++;
            LACE_ME;
            eval_label(g, states);
        }
    }
}

static inline void
learn_guards_par(vset_t states, long *guard_count)
{
    LACE_ME;
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) {
            if (guard_count != NULL) (*guard_count)++;
            SPAWN(eval_label, g, states);
        }
    }
    if (PINS_USE_GUARDS) {
        for (int g = 0; g < nGuards; g++) SYNC(eval_label);
    }
}

static inline void
learn_labels(vset_t states)
{
    for (int i = 0; i < sLbls; i++) {
        LACE_ME;
        if (bitvector_is_set(&state_label_used, i)) eval_label(i, states);
    }
}

static inline void
learn_labels_par(vset_t states)
{
    LACE_ME;
    for (int i = 0; i < sLbls; i++) {
        if (bitvector_is_set(&state_label_used, i)) SPAWN(eval_label, i, states);
    }
    for (int i = 0; i < sLbls; i++) {
        if (bitvector_is_set(&state_label_used, i)) SYNC(eval_label);
    }
}

struct inv_info_s {
    vset_t container;
    void* work;
};

static void
inv_info_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;
    vset_destroy(info->container);
    RTfree(info);
}

struct inv_rel_s {
    vset_t tmp; // some workspace for learning
    vset_t true_states; // all short states that satisfy the expression
    vset_t false_states; // all short states that do not satisfy the expression
    vset_t shortcut; // only used when not evaluating every binary operand in parallel.
    int* vec; // space for long vector
    int len; // length of short vector
    int* deps; // dependencies for short vector
};

static void
inv_rel_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;    
    struct inv_rel_s* rel = (struct inv_rel_s*) info->work;
    
    vset_destroy(rel->tmp);
    vset_destroy(rel->true_states);
    vset_destroy(rel->false_states);
    if (!inv_bin_par) vset_destroy(rel->shortcut);
    inv_info_destroy(info);
}

struct inv_svar_s {
    vset_t tmp;
};

static void
inv_svar_destroy(void* context)
{
    struct inv_info_s* info = (struct inv_info_s*) context;
    struct inv_svar_s* svar = (struct inv_svar_s*) info->work;
    
    if (svar->tmp != NULL) vset_destroy(svar->tmp);
    inv_info_destroy(info);
}

struct rel_expr_info {
    int* vec; // a long vector to use for expanding short vectors
    int len; // number of dependencies in this relational expression
    int* deps; // the dependencies in this relational expression
    ltsmin_expr_t e; // the relation expression
    ltsmin_parse_env_t env; // its environment
};

static void
rel_expr_cb(vset_t set, void *context, int *e)
{
    struct rel_expr_info* ctx = (struct rel_expr_info*) context;
    int vec[N];
    memcpy(vec, ctx->vec, sizeof(int[N]));
    for (int i = 0; i < ctx->len; i++) vec[ctx->deps[i]] = e[i];
    if (eval_predicate(model, ctx->e, vec, ctx->env)) vset_add(set, e);
}

#define eval_predicate_set_par(e, env, s) CALL(eval_predicate_set_par, (e), (env), (s))
VOID_TASK_3(eval_predicate_set_par, ltsmin_expr_t, e, ltsmin_parse_env_t, env, vset_t, states)
{
    struct inv_info_s* c = (struct inv_info_s*) e->context;
    struct inv_info_s* left, *right;
    left = right = NULL;
    if (e->node_type == UNARY_OP || e->node_type == BINARY_OP) left = (struct inv_info_s*) e->arg1->context;
    if (e->node_type == BINARY_OP) right = (struct inv_info_s*) e->arg2->context;
    
    switch (e->token) {
        case PRED_TRUE: {
            // do nothing (c->container already contains everything)
        } break;
        case PRED_FALSE: {
            vset_clear(c->container);
        } break;
        case PRED_SVAR: // assume state label
            vset_join(c->container, c->container, label_true[e->idx - N]);            
            break;
        case PRED_NOT: {
            vset_copy(left->container, c->container);
            eval_predicate_set_par(e->arg1, env, states);
            vset_minus(c->container, left->container);
            vset_clear(left->container);            
        } break;
        case PRED_AND: {
            vset_copy(left->container, c->container);
            SPAWN(eval_predicate_set_par, e->arg1, env, states);
            vset_copy(right->container, c->container);
            vset_clear(c->container);
            eval_predicate_set_par(e->arg2, env, states);
            SYNC(eval_predicate_set_par);
            vset_copy(c->container, left->container);
            vset_clear(left->container);
            vset_intersect(c->container, right->container);
            vset_clear(right->container);
        } break;
        case PRED_OR: {
            vset_copy(left->container, c->container);
            SPAWN(eval_predicate_set_par, e->arg1, env, states);
            vset_copy(right->container, c->container);
            vset_clear(c->container);
            eval_predicate_set_par(e->arg2, env, states);
            SYNC(eval_predicate_set_par);
            vset_copy(c->container, left->container);
            vset_clear(left->container);
            vset_union(c->container, right->container);
            vset_clear(right->container);
        } break;
        case PRED_EQ:
        case PRED_NEQ:
        case PRED_LT:
        case PRED_LEQ:
        case PRED_GT:
        case PRED_GEQ: {
            struct inv_rel_s* rel = (struct inv_rel_s*) c->work;
            
            vset_project_minus(rel->tmp, states, rel->true_states);
            vset_minus(rel->tmp, rel->false_states);
            
            struct rel_expr_info ctx;
            ctx.vec = rel->vec;

            ctx.len = rel->len;
            ctx.deps = rel->deps;

            ctx.e = e;
            ctx.env = env;
            
            // count when verbose
            if (log_active(infoLong)) {
                double elem_count;
                vset_count(rel->tmp, NULL, &elem_count);
                if (elem_count >= 10000.0 * SPEC_REL_PERF) {                    
                    char* p = LTSminPrintExpr(e, env);
                    Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
                    RTfree(p);
                }
            }

            vset_update(rel->true_states, rel->tmp, rel_expr_cb, &ctx);
            vset_minus(rel->tmp, rel->true_states);
            vset_union(rel->false_states, rel->tmp);
            vset_clear(rel->tmp);
            vset_join(c->container, c->container, rel->true_states);
            break;
        }
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
}

static void
eval_predicate_set(ltsmin_expr_t e, ltsmin_parse_env_t env, vset_t states)
{
    struct inv_info_s* c = (struct inv_info_s*) e->context;
    struct inv_info_s* left, *right;
    left = right = NULL;
    if (e->node_type == UNARY_OP || e->node_type == BINARY_OP) left = (struct inv_info_s*) e->arg1->context;
    if (e->node_type == BINARY_OP) right = (struct inv_info_s*) e->arg2->context;
    
    LACE_ME;

    switch (e->token) {
        case PRED_TRUE: {
            // do nothing (c->container already contains everything)
        } break;
        case PRED_FALSE: {
            vset_clear(c->container);
        } break;
        case PRED_SVAR: { // assume state label
            /* following join is necessary because vset does not yet support
             * set projection of a projected set. */
            struct inv_svar_s* svar = (struct inv_svar_s*) c->work;

            vset_join(svar->tmp, c->container, states);
            if (inv_par) {
                volatile int* ptr = &label_locks[e->idx - N];
                while (!cas(ptr, 0, 1)) {
                    lace_steal_random();
                    ptr = &label_locks[e->idx - N];
                }
            }
            eval_label(e->idx - N, svar->tmp);
            if (inv_par) label_locks[e->idx - N] = 0;
            vset_clear(svar->tmp);
            vset_join(c->container, c->container, label_true[e->idx - N]);            
        } break;
        case PRED_NOT: {
            vset_copy(left->container, c->container);
            eval_predicate_set(e->arg1, env, states);
            vset_minus(c->container, left->container);
            vset_clear(left->container);
        } break;
        case PRED_AND: {
            vset_copy(left->container, c->container);
            vset_clear(c->container);
            eval_predicate_set(e->arg1, env, states);
            if (!vset_is_empty(left->container)) {
                vset_copy(right->container, left->container); // epic win for state labels
                eval_predicate_set(e->arg2, env, states);
                vset_copy(c->container, right->container);
                vset_intersect(c->container, left->container);
                vset_clear(right->container);
                vset_clear(left->container);
            }
        } break;
        case PRED_OR: {
            vset_copy(left->container, c->container);
            eval_predicate_set(e->arg1, env, states);
            if (!vset_equal(left->container, c->container)) {
                vset_copy(right->container, c->container);
                vset_minus(right->container, left->container); // epic win for state labels
                eval_predicate_set(e->arg2, env, states);
                vset_copy(c->container, left->container);
                vset_union(c->container, right->container);
                vset_clear(right->container);
            }
            vset_clear(left->container);
        } break;
        case PRED_EQ:
        case PRED_NEQ:
        case PRED_LT:
        case PRED_LEQ:
        case PRED_GT:
        case PRED_GEQ: {
            struct inv_rel_s* rel = (struct inv_rel_s*) c->work;

            // this join is necessary because we can not project an already projected vset.
            vset_join(rel->shortcut, states, c->container);
            
            vset_project_minus(rel->tmp, rel->shortcut, rel->true_states);
            vset_clear(rel->shortcut);
            vset_minus(rel->tmp, rel->false_states);
            
            struct rel_expr_info ctx;
            ctx.vec = rel->vec;

            ctx.len = rel->len;
            ctx.deps = rel->deps;

            ctx.e = e;
            ctx.env = env;
            
            // count when verbose
            if (log_active(infoLong)) {
                double elem_count;
                vset_count(rel->tmp, NULL, &elem_count);
                if (elem_count >= 10000.0 * SPEC_REL_PERF) {                    
                    const char* p = LTSminPrintExpr(e, env);
                    Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
                }
            }

            vset_update(rel->true_states, rel->tmp, rel_expr_cb, &ctx);
            vset_minus(rel->tmp, rel->true_states);
            vset_union(rel->false_states, rel->tmp);
            vset_clear(rel->tmp);
            vset_join(c->container, c->container, rel->true_states);
            break;
        }
        default:
            LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
            HREabort (LTSMIN_EXIT_FAILURE);
    }
}

static inline void
inv_cleanup()
{    
    bitvector_clear(&state_label_used);

    int n_violated = 0;
    for (int i = 0; i < num_inv; i++) {
        if (!inv_violated[i]) {
            bitvector_union(&state_label_used, &inv_sl_deps[i]);
        } else n_violated++;
    }
    
    if (n_violated == num_inv) RTfree(inv_detect);

    if (PINS_USE_GUARDS) {
        for (int i = 0; i < nGuards; i++) {
            bitvector_set(&state_label_used, i);
        }
    }

    if (label_true != NULL) {
        for (int i = 0; i < sLbls; i++) {
            if (!bitvector_is_set(&state_label_used, i)) {
                if (label_true[i] != NULL) {
                    vset_destroy(label_false[i]);
                    vset_destroy(label_true[i]);
                    vset_destroy(label_tmp[i]);
                    label_false[i] = NULL;
                    label_true[i] = NULL;
                    label_tmp[i] = NULL;
                }
            }
        }
    }
}

static inline void
check_inv(vset_t states, const int level)
{
    if (num_inv_violated != num_inv && !vset_is_empty(states)) {
        int iv = 0;
        for (int i = 0; i < num_inv; i++) {
            if (!inv_violated[i]) {
                vset_project(inv_set[i], states);
                if (!vset_is_empty(inv_set[i])) {
                    vset_t container = ((struct inv_info_s*) inv_expr[i]->context)->container;
                    vset_copy(container, inv_set[i]);
                    eval_predicate_set(inv_expr[i], inv_parse_env[i], states);
                    if (!vset_equal(inv_set[i], container)) {
                        LTSminExprDestroy(inv_expr[i], 1);
                        LTSminParseEnvDestroy(inv_parse_env[i]);
                        vset_destroy(inv_set[i]);
                        Warning(info, " ");
                        Warning(info, "Invariant violation (%s) found at depth %d!", inv_detect[i], level);
                        Warning(info, " ");
                        inv_violated[i] = 1;
                        iv = 1;
                        num_inv_violated++;
                        if (num_inv_violated == num_inv) {
                            Warning(info, "all invariants violated");
                            if(!no_exit) {
                                RTstopTimer(reach_timer);
                                RTprintTimer(info, reach_timer, "invariant detection took");
                                Warning(info, "exiting now");
                                GBExit(model);
                                HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
                            }
                            Warning(info, "continuing...")
                        }
                    } else {
                        vset_clear(inv_set[i]);
                        vset_clear(container);
                    }
                }
            }
        }
        if (iv) inv_cleanup();
    }
}

TASK_3(int, check_inv_par_go, vset_t, states, int, i, int, level)
{
    int res = 0;
    if (!inv_violated[i]) {
        vset_project(inv_set[i], states);
        if (!vset_is_empty(inv_set[i])) {
            vset_t container = ((struct inv_info_s*) inv_expr[i]->context)->container;
            vset_copy(container, inv_set[i]);
            if (inv_bin_par) eval_predicate_set_par(inv_expr[i], inv_parse_env[i], states);
            else eval_predicate_set(inv_expr[i], inv_parse_env[i], states);

            if (!vset_equal(inv_set[i], container)) {
                LTSminExprDestroy(inv_expr[i], 1);
                LTSminParseEnvDestroy(inv_parse_env[i]);
                vset_destroy(inv_set[i]);
                Warning(info, " ");
                Warning(info, "Invariant violation (%s) found at depth %d!", inv_detect[i], level);
                Warning(info, " ");
                RTfree(inv_detect[i]);
                inv_violated[i] = 1;
                res = 1;
                add_fetch(&num_inv_violated, 1);
            } else {
                vset_clear(container);
                vset_clear(inv_set[i]);
            }
        }
    }
    return res;
}

static inline void
check_inv_par(vset_t states, const int level)
{
    LACE_ME;
    if (num_inv_violated != num_inv && !vset_is_empty(states)) {
        if (inv_bin_par) learn_labels_par(states);
        int iv = 0;
        for (int i = 0; i < num_inv; i++) {
            SPAWN(check_inv_par_go, states, i, level);
        }
        for (int i = 0; i < num_inv; i++) {
            int res = SYNC(check_inv_par_go);
            iv = res || iv;
        }
        if (num_inv_violated == num_inv) {
            Warning(info, "all invariants violated");
            if(!no_exit) {
                Warning(info, "exiting now");
                RTstopTimer(reach_timer);
                RTprintTimer(info, reach_timer, "invariant detection took");
                GBExit(model);
                HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
            }
            Warning(info, "continuing...")
        }
        if (iv) inv_cleanup();
    }
}

static inline void
check_invariants(vset_t set, int level)
{
    if (inv_par) check_inv_par(set, level);
    else check_inv(set, level);
}

struct trace_action {
    int *dst;
    int *cpy;
    char *action;
};

struct group_add_info {
    vrel_t rel; // target relation
    vset_t set; // source set
    int group; // which transition group
    int *src; // state vector
    int trace_count; // number of actions to trace after next-state call
    struct trace_action *trace_action;
};

static int
seen_actions_test (int idx)
{
    int size = BVLLget_size(seen_actions);
    if (idx >= size - 1) {
        if (BVLLtry_set_sat_bit(seen_actions, size-1, 0)) {
            Warning(info, "Warning: Action cache full. Caching currently limited to %d labels.", size-1);
        }
        return 1;
    }
    return BVLLtry_set_sat_bit(seen_actions, idx, 0);
}

static void
group_add(void *context, transition_info_t *ti, int *dst, int *cpy)
{
    struct group_add_info *ctx = (struct group_add_info*)context;

    int act_index = 0;
    if (ti->labels != NULL && act_label != -1) act_index = ti->labels[act_label];
    vrel_add_act(ctx->rel, ctx->src, dst, cpy, act_index);

    if (act_detect && (no_exit || ErrorActions == 0)) {
        // note: in theory, it might be possible that ti->labels == NULL,
        // even though we are using action detection and act_label != -1,
        // which was checked earlier in init_action_detection().
        // this indicates an incorrect implementation of the pins model
        if (ti->labels == NULL) {
            Abort("ti->labels is null");
        }
        if (seen_actions_test(act_index)) { // is this the first time we encounter this action?
            char *action=pins_chunk_get (model,action_typeno,act_index).data;

            if (strncmp(act_detect,action,strlen(act_detect))==0)  {
                Warning(info, "found action: %s", action);

                if (trc_output) {

                    size_t vec_bytes = sizeof(int[w_projs[ctx->group].len]);

                    ctx->trace_action = (struct trace_action*) RTrealloc(ctx->trace_action, sizeof(struct trace_action) * (ctx->trace_count+1));
                    ctx->trace_action[ctx->trace_count].dst = (int*) RTmalloc(vec_bytes);
                    if (cpy != NULL) {
                        ctx->trace_action[ctx->trace_count].cpy = (int*) RTmalloc(vec_bytes);
                    } else {
                        ctx->trace_action[ctx->trace_count].cpy = NULL;
                    }

                    // set the required values in order to find the trace after the next-state call
                    memcpy(ctx->trace_action[ctx->trace_count].dst, dst, vec_bytes);
                    if (cpy != NULL) memcpy(ctx->trace_action[ctx->trace_count].cpy, cpy, vec_bytes);
                    ctx->trace_action[ctx->trace_count].action = action;

                    ctx->trace_count++;
                }

                add_fetch(&ErrorActions, 1);
            }
        }
    }
}

static void
explore_cb(vrel_t rel, void *context, int *src)
{
    struct group_add_info ctx;
    ctx.group = ((struct group_add_info*)context)->group;
    ctx.set = ((struct group_add_info*)context)->set;
    ctx.rel = rel;
    ctx.src = src;
    ctx.trace_count = 0;
    ctx.trace_action = NULL;
    (*transitions_short)(model, ctx.group, src, group_add, &ctx);

    if (ctx.trace_count > 0) {
        int long_src[N];
        for (int i = 0; i < ctx.trace_count; i++) {
            vset_example_match(ctx.set,long_src,r_projs[ctx.group].len, r_projs[ctx.group].proj,src);
            find_action(long_src,ctx.trace_action[i].dst,ctx.trace_action[i].cpy,ctx.group,ctx.trace_action[i].action);
            RTfree(ctx.trace_action[i].dst);
            if (ctx.trace_action[i].cpy != NULL) RTfree(ctx.trace_action[i].cpy);
        }

        RTfree(ctx.trace_action);
    }
}

#define expand_group_next(g, s) CALL(expand_group_next, (g), (s))
VOID_TASK_2(expand_group_next, int, group, vset_t, set)
{
    if (!expand_groups) return; // assume transitions loaded from file cannot expand further

    struct group_add_info ctx;
    ctx.group = group;
    ctx.set = set;
    vset_project_minus(group_tmp[group], set, group_explored[group]);
    vset_union(group_explored[group], group_tmp[group]);

    if (log_active(infoLong)) {
        double elem_count;
        vset_count(group_tmp[group], NULL, &elem_count);

        if (elem_count >= 10000.0 * SPEC_REL_PERF) {
            Print(infoLong, "expanding group %d for %.*g states.", group, DBL_DIG, elem_count);
        }
    }

    vrel_update(group_next[group], group_tmp[group], explore_cb, &ctx);
    vset_clear(group_tmp[group]);
}

struct expand_info {
    int group;
    vset_t group_explored;
    long *eg_count;
};

static inline void
expand_group_next_projected(vrel_t rel, vset_t set, void *context)
{
    if (!expand_groups) return; // assume transitions loaded from file cannot expand further

    struct expand_info *expand_ctx = (struct expand_info*)context;
    (*expand_ctx->eg_count)++;

    vset_t group_explored = expand_ctx->group_explored;
    vset_zip(group_explored, set);

    struct group_add_info group_ctx;
    int group = expand_ctx->group;
    group_ctx.group = group;
    group_ctx.set = NULL;
    vrel_update(rel, set, explore_cb, &group_ctx);
}

static void
valid_end_cb(void *context, int *src)
{
    int *state = (int *) context;
    if (!state[N] && !pins_state_is_valid_end(model, src)) {
        memcpy (state, src, sizeof(int[N]));
        state[N] = 1;
    }
}

static inline void
learn_guards_reduce(vset_t true_states, int t, long *guard_count, vset_t *guard_maybe, vset_t false_states, vset_t maybe_states, vset_t tmp) {

    LACE_ME;
    if (PINS_USE_GUARDS) {
        guard_t* guards = GBgetGuard(model, t);
        for (int g = 0; g < guards->count && !vset_is_empty(true_states); g++) {
            if (guard_count != NULL) (*guard_count)++;
            eval_label(guards->guard[g], true_states);

            if (!no_soundness_check) {

                // compute guard_maybe (= guard_true \cap guard_false)
                vset_copy(guard_maybe[guards->guard[g]], label_true[guards->guard[g]]);
                vset_intersect(guard_maybe[guards->guard[g]], label_false[guards->guard[g]]);

                if (!SPEC_MAYBE_AND_FALSE_IS_FALSE) {
                    // If we have Promela, Java etc. then if we encounter a maybe guard then this is an error.
                    // Because every guard is evaluated in order.
                    if (!vset_is_empty(guard_maybe[guards->guard[g]])) {
                        Warning(info, "Condition in group %d does not evaluate to true or false", t);
                        HREabort(LTSMIN_EXIT_UNSOUND);
                    }
                } else {
                    // If we have mCRL2 etc., then we need to store all (real) false states and maybe states
                    // and see if after evaluating all guards there are still maybe states left.
                    vset_join(tmp, true_states, label_false[guards->guard[g]]);
                    vset_union(false_states, tmp);
                    vset_join(tmp, true_states, guard_maybe[guards->guard[g]]);
                    vset_minus(false_states,tmp);
                    vset_union(maybe_states, tmp);
                }
                vset_clear(guard_maybe[guards->guard[g]]);
            }
            vset_join(true_states, true_states, label_true[guards->guard[g]]);
        }

        if (!no_soundness_check && SPEC_MAYBE_AND_FALSE_IS_FALSE) {
            vset_copy(tmp, maybe_states);
            vset_minus(tmp, false_states);
            if (!vset_is_empty(tmp)) {
                Warning(info, "Condition in group %d does not evaluate to true or false", t);
                HREabort(LTSMIN_EXIT_UNSOUND);
            }
            vset_clear(tmp);
            vset_clear(maybe_states);
            vset_clear(false_states);
        }

        if (!no_soundness_check) {
            for (int g = 0; g < guards->count; g++) {
                vset_minus(label_true[guards->guard[g]], label_false[guards->guard[g]]);
            }
        }
    }
}

static void
deadlock_check(vset_t deadlocks, bitvector_t *reach_groups)
// checks for deadlocks, generate trace if requested, and unsets dlk_detect
{
    if (vset_is_empty(deadlocks))
        return;

    Warning(debug, "Potential deadlocks found");

    vset_t next_temp = vset_create(domain, -1, NULL);
    vset_t prev_temp = vset_create(domain, -1, NULL);
    vset_t new_reduced[nGrps];

    for(int i=0;i<nGrps;i++) {
        new_reduced[i]=vset_create(domain, -1, NULL);
    }

    vset_t guard_maybe[nGuards];
    vset_t tmp = NULL;
    vset_t false_states = NULL;
    vset_t maybe_states = NULL;
    if (!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            guard_maybe[i] = vset_create(domain, l_projs[i].len, l_projs[i].proj);
        }
        false_states = vset_create(domain, -1, NULL);
        maybe_states = vset_create(domain, -1, NULL);
        tmp = vset_create(domain, -1, NULL);
    }

    LACE_ME;
    for (int i = 0; i < nGrps; i++) {
        if (bitvector_is_set(reach_groups, i)) continue;
        vset_copy(new_reduced[i], deadlocks);
        learn_guards_reduce(new_reduced[i], i, NULL, guard_maybe, false_states, maybe_states, tmp);
        expand_group_next(i, new_reduced[i]);
        vset_next_fn(next_temp, new_reduced[i], group_next[i]);
        vset_prev(prev_temp, next_temp, group_next[i],new_reduced[i]);
        reduce(i, prev_temp);
        vset_minus(deadlocks, prev_temp);
    }

    vset_destroy(next_temp);
    vset_destroy(prev_temp);

    for(int i=0;i<nGrps;i++) {
        vset_destroy(new_reduced[i]);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }

    if (vset_is_empty(deadlocks))
        return;

    int dlk_state[1][N + 1];
    if (pins_get_valid_end_state_label_index(model) >= 0) {
        dlk_state[0][N] = 0; // Did not find an invalid end state yet
        vset_enum (deadlocks, valid_end_cb, dlk_state[0]);
        if (!dlk_state[0][N])
            return;
    } else {
        vset_example(deadlocks, dlk_state[0]);
    }

    Warning(info, "deadlock found");

    if (trc_output) {
        find_trace(dlk_state, 1, global_level, levels, "deadlock");
    }

    if (no_exit) {
        dlk_detect=0; // avoids checking for more deadlocks; as long as dlk_detect==1, no deadlocks have been found.
    } else {
        RTstopTimer(reach_timer);
        RTprintTimer(info, reach_timer, "deadlock detection took");
        Warning(info, "exiting now");
        GBExit(model);
        HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}

static void
stats_and_progress_report(vset_t current, vset_t visited, int level)
{
    long   n_count;
    long double e_count;
    
    if (sat_strategy == NO_SAT || log_active (infoLong)) {
        Print(infoShort, "level %d is finished", level);
    }
    if (log_active (infoLong) || peak_nodes) {
        if (current != NULL) {
            int digs = vset_count_fn (current, &n_count, &e_count);
            Print(infoLong, "level %d has %.*Lg states ( %ld nodes )", level, digs, e_count, n_count);
            if (n_count > max_lev_count) max_lev_count = n_count;
        }
        int digs = vset_count_fn (visited, &n_count, &e_count);
        Print(infoLong, "visited %d has %.*Lg states ( %ld nodes )", level, digs, e_count, n_count);

        if (n_count > max_vis_count) max_vis_count = n_count;

        if (log_active (debug)) {
            Debug("transition caches ( grp nds elts ):");

            for (int i = 0; i < nGrps; i++) {
                vrel_count(group_next[i], &n_count, NULL);
                Debug("( %d %ld ) ", i, n_count);

                if (n_count > max_trans_count) max_trans_count = n_count;
            }

            Debug("\ngroup explored    ( grp nds elts ): ");

            for (int i = 0; i < nGrps; i++) {
                vset_count(group_explored[i], &n_count, NULL);
                Debug("( %d %ld) ", i, n_count);

                if (n_count > max_grp_count) max_grp_count = n_count;
            }
        }
    }
    
    if (dot_dir != NULL) {
        
        FILE *fp;
        char *file;

        file = "%s/current-l%d.dot";
        char fcbuf[snprintf(NULL, 0, file, dot_dir, level)];
        sprintf(fcbuf, file, dot_dir, level);

        fp = fopen(fcbuf, "w+");
        vset_dot(fp, current);
        fclose(fp);

        file = "%s/visited-l%d.dot";
        char fvbuf[snprintf(NULL, 0, file, dot_dir, level)];
        sprintf(fvbuf, file, dot_dir, level);

        fp = fopen(fvbuf, "w+");
        vset_dot(fp, visited);
        fclose(fp);

        for (int i = 0; i < nGrps; i++) {
            file = "%s/group_next-l%d-k%d.dot";
            char fgbuf[snprintf(NULL, 0, file, dot_dir, level, i)];
            sprintf(fgbuf, file, dot_dir, level, i);
            fp = fopen(fgbuf, "w+");
            vrel_dot(fp, group_next[i]);
            fclose(fp);
        }

        for (int g = 0; g < nGuards && PINS_USE_GUARDS; g++) {
            file = "%s/guard_false-l%d-g%d.dot";
            char fgfbuf[snprintf(NULL, 0, file, dot_dir, level, g)];
            sprintf(fgfbuf, file, dot_dir, level, g);
            fp = fopen(fgfbuf, "w+");
            vset_dot(fp, label_false[g]);
            fclose(fp);

            file = "%s/guard_true-l%d-g%d.dot";
            char fgtbuf[snprintf(NULL, 0, file, dot_dir, level, g)];
            sprintf(fgtbuf, file, dot_dir, level, g);
            fp = fopen(fgtbuf, "w+");
            vset_dot(fp, label_true[g]);
            fclose(fp);
        }
    }    
}

static void
final_stat_reporting(vset_t visited)
{
    RTprintTimer(info, reach_timer, "reachability took");

    if (dlk_detect) Warning(info, "No deadlocks found");

    if (act_detect != NULL) {
        Warning(info, "%d different actions with prefix \"%s\" are found", ErrorActions, act_detect);
    }

    long n_count;
    Print(infoShort, "counting visited states...");
    rt_timer_t t = RTcreateTimer();
    RTstartTimer(t);
    char states[128];
    long double e_count;
    int digs = vset_count_fn(visited, &n_count, &e_count);
    snprintf(states, 128, "%.*Lg", digs, e_count);

    RTstopTimer(t);
    RTprintTimer(infoShort, t, "counting took");
    RTresetTimer(t);

    int is_precise = strstr(states, "e") == NULL && strstr(states, "inf") == NULL;

    Print(infoShort, "state space has%s %s states, %ld nodes", precise && is_precise ? " precisely" : "", states, n_count);

    if (!is_precise && precise) {
        if (vdom_supports_precise_counting(domain)) {
            Print(infoShort, "counting visited states precisely...");
            RTstartTimer(t);
            bn_int_t e_count;
            vset_count_precise(visited, n_count, &e_count);
            RTstopTimer(t);
            RTprintTimer(infoShort, t, "counting took");

            size_t len = bn_strlen(&e_count);
            char e_str[len];
            bn_int2string(e_str, len, &e_count);
            bn_clear(&e_count);

            Print(infoShort, "state space has precisely %s states (%zu digits)", e_str, strlen(e_str));
        } else Warning(info, "vset implementation does not support precise counting");
    }


    RTdeleteTimer(t);

    if (log_active (infoLong) || peak_nodes) {
        log_t l;
        if (peak_nodes) l = info;
        else l = infoLong;
        if (max_lev_count == 0) {
            Print(l, "( %ld final BDD nodes; %ld peak nodes )", n_count, max_vis_count);
        } else {
            Print(l,
                  "( %ld final BDD nodes; %ld peak nodes; %ld peak nodes per level )",
                  n_count, max_vis_count, max_lev_count);
        }

        if (log_active (debug)) {
            Debug("( peak transition cache: %ld nodes; peak group explored: " "%ld nodes )\n",
                  max_trans_count, max_grp_count);
        }
    }
}

static bool debug_output_enabled = false;

/**
 * \brief Computes the subset of v that belongs to player <tt>player</tt>.
 * \param vars the indices of variables of player <tt>player</tt>.
 */
static inline void add_variable_subset(vset_t dst, vset_t src, vdom_t domain, int var_index)
{
    //Warning(info, "add_variable_subset: var_index=%d", var_index);
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    int match[1] = {var_index}; // the variable
    vset_t u = vset_create(domain, -1, NULL);
    vset_copy_match_proj(u, src, p_len, proj, variable_projection, match);

    if (debug_output_enabled && log_active(infoLong))
    {
        double e_count;
        vset_count(u, NULL, &e_count);
        if (e_count > 0) Print(infoLong, "add_variable_subset: %d: %.*g states", var_index, DBL_DIG, e_count);
    }

    vset_union(dst, u);
    vset_destroy(u);
}

/**
 * Tree structure to evaluate the condition of a transition group.
 * If we disable the soundness check of guard-splitting then if we
 * have SPEC_MAYBE_AND_FALSE_IS_FALSE (like mCRL(2) and SCOOP) then
 * (maybe && false == false) or (false && maybe == false) is not checked.
 * If we have !SPEC_MAYBE_AND_FALSE_IS_FALSE (like Java, Promela and DVE) then only
 * (maybe && false == false) is not checked.
 * For guard-splitting ternary logic is used; i.e. (false,true,maybe) = (0,1,2) = (0,1,?).
 * Truth table for SPEC_MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | 0 ? ?
 * Truth table for !SPEC_MAYBE_AND_FALSE_IS_FALSE:
 *      0 1 ?
 *      -----
 *  0 | 0 0 0
 *  1 | 0 1 ?
 *  ? | ? ? ?
 *
 *  Note that if a guard evaluates to maybe then we add it to both guard_false and guard_true, i.e. F \cap T != \emptyset.
 *  Soundness check: vset_is_empty(root(reach_red_s)->true_container \cap root(reach_red_s)->false_container) holds.
 *  Algorithm to carry all maybe states to the root:
 *  \bigcap X = Y \cap Z = (Fy,Ty) \cap (Fz,Tz):
 *   - T = (Ty \cap Tz) U M
 *   - F = Fy U Fz U M
 *   - M = SPEC_MAYBE_AND_FALSE_IS_FALSE  => ((Fy \cap Ty) \ Fz) U ((Fz \cap Tz) \ Fy) &&
 *         !SPEC_MAYBE_AND_FALSE_IS_FALSE => (Fy \cap Ty) U ((Fz \cap Tz) \ Fy)
 */
struct reach_red_s
{
    vset_t true_container;
    vset_t false_container;
    vset_t left_maybe; // temporary vset so that we don't have to create/destroy at each level
    vset_t right_maybe; // temporary vset so that we don't have to create/destroy at each level
    struct reach_red_s *left;
    struct reach_red_s *right;
    int index; // which guard
    int group; // which transition group
};

static struct reach_red_s*
reach_red_prepare(size_t left, size_t right, int group)
{
    struct reach_red_s *result = (struct reach_red_s *)RTmalloc(sizeof(struct reach_red_s));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
    } else {
        result->index = -1;
        result->left = reach_red_prepare(left, (left+right)/2, group);
        result->right = reach_red_prepare((left+right)/2, right, group);
    }
    result->group = group;
    result->true_container = vset_create(domain, -1, NULL);
    if (!no_soundness_check) {
        result->false_container = vset_create(domain, -1, NULL);
        result->left_maybe = vset_create(domain, -1, NULL);
        result->right_maybe = vset_create(domain, -1, NULL);
    }

    return result;
}

static void
reach_red_destroy(struct reach_red_s *s)
{
    if (s->index == -1) {
        reach_red_destroy(s->left);
        reach_red_destroy(s->right);
    }
    vset_destroy(s->true_container);
    if (!no_soundness_check) {
        vset_destroy(s->false_container);
        vset_destroy(s->left_maybe);
        vset_destroy(s->right_maybe);
    }
    RTfree(s);
}

struct reach_s
{
    vset_t container;
    vset_t deadlocks; // only used if dlk_detect
    vset_t ancestors;
    struct reach_s *left;
    struct reach_s *right;
    int index;
    int class;
    int next_count;
    int eg_count;
    struct reach_red_s *red;
    int unsound_group;
};

static struct reach_s*
reach_prepare(size_t left, size_t right)
{
    struct reach_s *result = (struct reach_s *)RTmalloc(sizeof(struct reach_s));
    if (right - left == 1) {
        result->index = left;
        result->left = NULL;
        result->right = NULL;
        if (PINS_USE_GUARDS) result->red = reach_red_prepare(0, GBgetGuard(model, left)->count, left);
        else result->red = NULL;
    } else {
        result->index = -1;
        result->left = reach_prepare(left, (left+right)/2);
        result->right = reach_prepare((left+right)/2, right);
        result->red = NULL;
    }
    result->container = vset_create(domain, -1, NULL);
    result->ancestors = NULL;
    result->deadlocks = NULL;
    result->unsound_group = -1;
    if (inhibit_matrix != NULL || dlk_detect) {
        result->ancestors = vset_create(domain, -1, NULL);
    }
    if (dlk_detect) {
        result->deadlocks = vset_create(domain, -1, NULL);
    }
    return result;
}

static void
reach_destroy(struct reach_s *s)
{
    if (s->index == -1) {
        reach_destroy(s->left);
        reach_destroy(s->right);
    }

    vset_destroy(s->container);
    if (s->ancestors != NULL) vset_destroy(s->ancestors);
    if (s->deadlocks != NULL) vset_destroy(s->deadlocks);

    if (s->red != NULL) reach_red_destroy(s->red);

    RTfree(s);
}

#define reach_bfs_reduce(dummy) CALL(reach_bfs_reduce, dummy)
VOID_TASK_1(reach_bfs_reduce, struct reach_red_s *, dummy)
{
    if (dummy->index >= 0) { // base case
        // check if no states which satisfy other guards
        if (vset_is_empty(dummy->true_container)) return;
        // reduce states in transition group
        int guard = GBgetGuard(model, dummy->group)->guard[dummy->index];
        if (!no_soundness_check) {
            vset_copy(dummy->false_container, dummy->true_container);
            vset_join(dummy->false_container, dummy->false_container, label_false[guard]);
        }
        vset_join(dummy->true_container, dummy->true_container, label_true[guard]);
    } else { // recursive case
        // send set of states downstream
        vset_copy(dummy->left->true_container, dummy->true_container);
        vset_copy(dummy->right->true_container, dummy->true_container);

        // sequentially go left/right (not parallel)
        reach_bfs_reduce(dummy->left);
        reach_bfs_reduce(dummy->right);

        // we intersect every leaf, since we want to reduce the states in the group
        vset_copy(dummy->true_container, dummy->left->true_container);
        vset_intersect(dummy->true_container, dummy->right->true_container);

        if (!no_soundness_check) {

            // compute maybe set
            vset_copy(dummy->left_maybe, dummy->left->false_container);
            vset_intersect(dummy->left_maybe, dummy->left->true_container);
            if (SPEC_MAYBE_AND_FALSE_IS_FALSE) vset_minus(dummy->left_maybe, dummy->right->false_container);

            vset_copy(dummy->right_maybe, dummy->right->false_container);
            vset_intersect(dummy->right_maybe, dummy->right->true_container);
            vset_minus(dummy->right_maybe, dummy->left->false_container);

            vset_union(dummy->left_maybe, dummy->right_maybe);
            vset_clear(dummy->right_maybe);

            // compute false set
            vset_copy(dummy->false_container, dummy->left->false_container);
            vset_union(dummy->false_container, dummy->right->false_container);
            vset_union(dummy->false_container, dummy->left_maybe);
            vset_clear(dummy->left->false_container);
            vset_clear(dummy->right->false_container);

            // compute true set
            vset_union(dummy->true_container, dummy->left_maybe);

            vset_clear(dummy->left_maybe);
        }

        vset_clear(dummy->left->true_container);
        vset_clear(dummy->right->true_container);
    }
}

#define reach_bfs_next(dummy, reach_groups, maybe) CALL(reach_bfs_next, dummy, reach_groups, maybe)
VOID_TASK_3(reach_bfs_next, struct reach_s *, dummy, bitvector_t *, reach_groups, vset_t*, maybe)
{
    if (dummy->index >= 0) {
        if (!bitvector_is_set(reach_groups, dummy->index)) {
            if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
            dummy->next_count = 0;
            dummy->eg_count=0;
            return;
        }

        // Check if in current class...
        if (inhibit_matrix != NULL) {
            if (!dm_is_set(class_matrix, dummy->class, dummy->index)) {
                if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }
        }

        if (dummy->red != NULL) { // we have guard-splitting; reduce the set
            // Reduce current level
            vset_copy(dummy->red->true_container, dummy->container);
            reach_bfs_reduce(dummy->red);

            if (vset_is_empty(dummy->red->true_container)) {
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }

            // soundness check
            if (!no_soundness_check) {
                vset_copy(maybe[dummy->index], dummy->red->true_container);
                vset_intersect(maybe[dummy->index], dummy->red->false_container);

                // we don't abort immediately so that other threads can finish cleanly.
                if (!vset_is_empty(maybe[dummy->index])) dummy->unsound_group = dummy->index;
                vset_clear(maybe[dummy->index]);
                vset_clear(dummy->red->false_container);
            }

            vset_copy(dummy->container, dummy->red->true_container);
            vset_clear(dummy->red->true_container);
        }

        // Expand transition relations
        expand_group_next(dummy->index, dummy->container);
        dummy->eg_count = 1;

        // Compute successor states
        vset_next_fn(dummy->container, dummy->container, group_next[dummy->index]);
        dummy->next_count = 1;

        // Compute ancestor states
        if (dummy->ancestors != NULL) {
            vset_prev(dummy->ancestors, dummy->container, group_next[dummy->index], dummy->ancestors);
            reduce(dummy->index, dummy->ancestors);
        }

        // Remove ancestor states from potential deadlock states
        if (dummy->deadlocks != NULL) vset_minus(dummy->deadlocks, dummy->ancestors);

        // If we don't need ancestor states, clear the set
        if (dummy->ancestors != NULL && inhibit_matrix == NULL) vset_clear(dummy->ancestors);
    } else {
        // Send set of states downstream
        vset_copy(dummy->left->container, dummy->container);
        vset_copy(dummy->right->container, dummy->container);

        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->left->deadlocks, dummy->deadlocks);
            vset_copy(dummy->right->deadlocks, dummy->deadlocks);
        }

        if (dummy->ancestors != NULL) {
            vset_copy(dummy->left->ancestors, dummy->ancestors);
            vset_copy(dummy->right->ancestors, dummy->ancestors);
        }

        dummy->left->class = dummy->class;
        dummy->right->class = dummy->class;

        // Sequentially go left/right (BFS, not PAR)
        reach_bfs_next(dummy->left, reach_groups, maybe);
        reach_bfs_next(dummy->right, reach_groups, maybe);

        // Perform union
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);

        // Clear
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);

        // Intersect deadlocks
        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->deadlocks, dummy->left->deadlocks);
            vset_intersect(dummy->deadlocks, dummy->right->deadlocks);
            vset_clear(dummy->left->deadlocks);
            vset_clear(dummy->right->deadlocks);
        }

        // Merge ancestors
        if (inhibit_matrix != NULL) {
            vset_copy(dummy->ancestors, dummy->left->ancestors);
            vset_union(dummy->ancestors, dummy->right->ancestors);
            vset_clear(dummy->left->ancestors);
            vset_clear(dummy->right->ancestors);
        }

        dummy->next_count = dummy->left->next_count + dummy->right->next_count;
        dummy->eg_count = dummy->left->eg_count + dummy->right->eg_count;

        if (dummy->left->unsound_group > -1) dummy->unsound_group = dummy->left->unsound_group;
        if (dummy->right->unsound_group > -1) dummy->unsound_group = dummy->right->unsound_group;
    }
}

static void
reach_chain_stop() {
    if (!no_exit && ErrorActions > 0) {
        RTstopTimer(reach_timer);
        RTprintTimer(info, reach_timer, "action detection took");
        Warning(info, "Exiting now");
        GBExit(model);
        HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
    }
}

static void
reach_stop(struct reach_s* node) {
    if (node->unsound_group > -1) {
        Warning(info, "Condition in group %d does not always evaluate to true or false", node->unsound_group);
        HREabort(LTSMIN_EXIT_UNSOUND);
    }
    reach_chain_stop();
}

static void
reach_none(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
    long *eg_count, long *next_count, long *guard_count)
{
    (void) visited; (void) visited_old; (void) reach_groups; (void) eg_count; (void) next_count; (void) guard_count;
    Warning(info, "not doing anything");
}

static void
reach_bfs_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                   long *eg_count, long *next_count, long *guard_count)
{
    vset_t current_level = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);

    vset_copy(current_level, visited);
    if (save_sat_levels) vset_minus(current_level, visited_old);

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_is_empty(current_level)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(current_level, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, current_level);

        if (inhibit_matrix != NULL) {
            // class_enabled holds all states in the current level with transitions in class c
            // only use current_level, so clear class_enabled...
            for (int c=0; c<inhibit_class_count; c++) vset_clear(class_enabled[c]);

            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, current_level);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                reach_bfs_next(root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) {
                        vset_minus(label_true[g], label_false[g]);
                    }
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_copy(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
        } else {
            // set container to current level
            vset_copy(root->container, current_level);
            // evaluate all guards
            learn_guards(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, current_level);
            // call next function
            reach_bfs_next(root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) {
                    vset_minus(label_true[g], label_false[g]);
                }
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // set next_level to new states (root->container - visited states)
            vset_copy(next_level, root->container);
            vset_clear(root->container);
            vset_minus(next_level, visited);
        }

        if (sat_strategy == NO_SAT) check_invariants(next_level, level);

        // set current_level to next_level
        vset_copy(current_level, next_level);
        vset_clear(next_level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_union(visited, current_level);
        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(current_level);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }
}

static void
reach_bfs(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
              long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);
    //if (save_sat_levels) vset_minus(current_level, visited_old); // ???

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);

        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(NULL, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, visited);

        if (inhibit_matrix != NULL) {
            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, visited);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                reach_bfs_next(root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) {
                        vset_minus(label_true[g], label_false[g]);
                    }
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_union(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
            vset_union(visited, next_level);
            vset_clear(next_level);
        } else {
            // set container to current level
            vset_copy(root->container, visited);
            // evaluate all guards
            learn_guards(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, visited);
            // call next function
            reach_bfs_next(root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) {
                    vset_minus(label_true[g], label_false[g]);
                }
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // add successors to visited set
            vset_union(visited, root->container);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(old_vis);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }

    return;
    (void)visited_old;
}

/**
 * Parallel reachability implementation
 */

VOID_TASK_3(compute_left_maybe, vset_t, left_maybe, vset_t, left_true, vset_t, right_false)
{
    vset_intersect(left_maybe, left_true);
    if (SPEC_MAYBE_AND_FALSE_IS_FALSE) vset_minus(left_maybe, right_false);
}

VOID_TASK_3(compute_right_maybe, vset_t, right_maybe, vset_t, right_true, vset_t, left_false)
{
    vset_intersect(right_maybe, right_true);
    vset_minus(right_maybe, left_false);
}

VOID_TASK_3(compute_false, vset_t, false_c, vset_t, right_false, vset_t, maybe) {
    vset_union(false_c, right_false);
    vset_union(false_c, maybe);
}

VOID_TASK_1(reach_par_reduce, struct reach_red_s *, dummy)
{
    if (dummy->index >= 0) { // base case
        // check if no states which satisfy other guards
        if (vset_is_empty(dummy->true_container)) return;
        int guard = GBgetGuard(model, dummy->group)->guard[dummy->index];
        if (!no_soundness_check) {
            vset_copy(dummy->false_container, dummy->true_container);
            vset_join_par(dummy->false_container, dummy->false_container, label_false[guard]);
        }
        vset_join_par(dummy->true_container, dummy->true_container, label_true[guard]);
        SYNC(vset_join_par);
        if (!no_soundness_check) SYNC(vset_join_par);
    } else { //recursive case
        // send set of states downstream
        vset_copy(dummy->left->true_container, dummy->true_container);
        vset_copy(dummy->right->true_container, dummy->true_container);

        // go left/right in parallel
        SPAWN(reach_par_reduce, dummy->left);
        SPAWN(reach_par_reduce, dummy->right);
        SYNC(reach_par_reduce);
        SYNC(reach_par_reduce);

        if (!no_soundness_check) {
            // compute maybe set
            vset_copy(dummy->left_maybe, dummy->left->false_container);
            SPAWN(compute_left_maybe, dummy->left_maybe, dummy->left->true_container, dummy->right->false_container);

            vset_copy(dummy->right_maybe, dummy->right->false_container);
            SPAWN(compute_right_maybe, dummy->right_maybe, dummy->right->true_container, dummy->left->false_container);

            SYNC(compute_right_maybe);
            SYNC(compute_left_maybe);

            // compute maybe
            vset_union(dummy->left_maybe, dummy->right_maybe);
            vset_clear(dummy->right_maybe);

            // compute false set
            vset_copy(dummy->false_container, dummy->left->false_container);
            SPAWN(compute_false, dummy->false_container, dummy->right->false_container, dummy->left_maybe);

            // compute true set
            // we intersect every leaf, since we want to reduce the states in the group
            vset_copy(dummy->true_container, dummy->left->true_container);
            vset_intersect(dummy->true_container, dummy->right->true_container);
            vset_union(dummy->true_container, dummy->left_maybe);
            vset_clear(dummy->left_maybe);

            SYNC(compute_false);
            vset_clear(dummy->left->false_container);
            vset_clear(dummy->right->false_container);
        } else {
            // we intersect every leaf, since we want to reduce the states in the group
            vset_copy(dummy->true_container, dummy->left->true_container);
            vset_intersect(dummy->true_container, dummy->right->true_container);
        }

        vset_clear(dummy->left->true_container);
        vset_clear(dummy->right->true_container);
    }
}

VOID_TASK_3(reach_par_next, struct reach_s *, dummy, bitvector_t *, reach_groups, vset_t*, maybe)
{
    if (dummy->index >= 0) {
        if (!bitvector_is_set(reach_groups, dummy->index)) {
            if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
            dummy->next_count = 0;
            dummy->eg_count=0;
            return;
        }

        // Check if in current class...
        if (inhibit_matrix != NULL) {
            if (!dm_is_set(class_matrix, dummy->class, dummy->index)) {
                if (dummy->ancestors != NULL) vset_clear(dummy->ancestors);
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }
        }

        if (dummy->red != NULL) { // we have guard-splitting; reduce the set
            // Reduce current level
            vset_copy(dummy->red->true_container, dummy->container);
            CALL(reach_par_reduce, dummy->red);

            if (vset_is_empty(dummy->red->true_container)) {
                dummy->next_count = 0;
                dummy->eg_count=0;
                return;
            }

            // soundness check
            if (!no_soundness_check) {
                vset_copy(maybe[dummy->index], dummy->red->true_container);
                vset_intersect(maybe[dummy->index], dummy->red->false_container);

                // we don't abort immediately so that other threads can finish cleanly.
                if (!vset_is_empty(maybe[dummy->index])) dummy->unsound_group = dummy->index;
                vset_clear(maybe[dummy->index]);
                vset_clear(dummy->red->false_container);
            }

            vset_copy(dummy->container, dummy->red->true_container);
            vset_clear(dummy->red->true_container);
        }

        // Expand transition relations
        expand_group_next(dummy->index, dummy->container);
        dummy->eg_count = 1;

        // Compute successor states
        vset_next_fn(dummy->container, dummy->container, group_next[dummy->index]);
        dummy->next_count = 1;

        // Compute ancestor states
        if (dummy->ancestors != NULL) {
            vset_prev(dummy->ancestors, dummy->container, group_next[dummy->index], dummy->ancestors);
            reduce(dummy->index, dummy->ancestors);
        }

        // Remove ancestor states from potential deadlock states
        if (dummy->deadlocks != NULL) vset_minus(dummy->deadlocks, dummy->ancestors);

        // If we don't need ancestor states, clear the set
        if (dummy->ancestors != NULL && inhibit_matrix == NULL) vset_clear(dummy->ancestors);
    } else {
        // Send set of states downstream
        vset_copy(dummy->left->container, dummy->container);
        vset_copy(dummy->right->container, dummy->container);

        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->left->deadlocks, dummy->deadlocks);
            vset_copy(dummy->right->deadlocks, dummy->deadlocks);
        }

        if (dummy->ancestors != NULL) {
            vset_copy(dummy->left->ancestors, dummy->ancestors);
            vset_copy(dummy->right->ancestors, dummy->ancestors);
        }

        dummy->left->class = dummy->class;
        dummy->right->class = dummy->class;

        // Go left/right in parallel
        SPAWN(reach_par_next, dummy->left, reach_groups, maybe);
        SPAWN(reach_par_next, dummy->right, reach_groups, maybe);
        SYNC(reach_par_next);
        SYNC(reach_par_next);

        // Perform union
        vset_copy(dummy->container, dummy->left->container);
        vset_union(dummy->container, dummy->right->container);

        // Clear
        vset_clear(dummy->left->container);
        vset_clear(dummy->right->container);

        // Intersect deadlocks
        if (dummy->deadlocks != NULL) {
            vset_copy(dummy->deadlocks, dummy->left->deadlocks);
            vset_intersect(dummy->deadlocks, dummy->right->deadlocks);
            vset_clear(dummy->left->deadlocks);
            vset_clear(dummy->right->deadlocks);
        }

        // Merge ancestors
        if (inhibit_matrix != NULL) {
            vset_copy(dummy->ancestors, dummy->left->ancestors);
            vset_union(dummy->ancestors, dummy->right->ancestors);
            vset_clear(dummy->left->ancestors);
            vset_clear(dummy->right->ancestors);
        }

        dummy->next_count = dummy->left->next_count + dummy->right->next_count;
        dummy->eg_count = dummy->left->eg_count + dummy->right->eg_count;

        if (dummy->left->unsound_group > -1) dummy->unsound_group = dummy->left->unsound_group;
        if (dummy->right->unsound_group > -1) dummy->unsound_group = dummy->right->unsound_group;
    }
}

static void
reach_par(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
              long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);
    //if (save_sat_levels) vset_minus(current_level, visited_old); // ???

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);

        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(NULL, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, visited);

        if (inhibit_matrix != NULL) {
            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, visited);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards_par(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                CALL(reach_par_next, root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                    for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_union(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
            vset_union(visited, next_level);
            vset_clear(next_level);
        } else {
            // set container to current level
            vset_copy(root->container, visited);
            // evaluate all guards
            learn_guards_par(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, visited);
            // call next function
            CALL(reach_par_next, root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // add successors to visited set
            vset_union(visited, root->container);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(old_vis);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }

    return;
    (void)visited_old;
}

static void
reach_par_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
              long *eg_count, long *next_count, long *guard_count)
{
    vset_t current_level = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);

    vset_copy(current_level, visited);
    if (save_sat_levels) vset_minus(current_level, visited_old);

    vset_t maybe[nGrps];
    if (!no_soundness_check) {
        for (int i = 0; i < nGrps; i++) {
            maybe[i] = vset_create(domain, -1, NULL);
        }
    }

    LACE_ME;
    struct reach_s *root = reach_prepare(0, nGrps);

    int level = 0;
    while (!vset_is_empty(current_level)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(current_level, visited, level);
        level++;

        if (dlk_detect) vset_copy(root->deadlocks, current_level);

        if (inhibit_matrix != NULL) {
            // class_enabled holds all states in the current level with transitions in class c
            // only use current_level, so clear class_enabled...
            for (int c=0; c<inhibit_class_count; c++) vset_clear(class_enabled[c]);

            // for every class, compute successors, add to next_level
            for (int c=0; c<inhibit_class_count; c++) {
                // set container to current level minus enabled transitions from all inhibiting classes
                vset_copy(root->container, current_level);
                for (int i=0; i<c; i++) if (dm_is_set(inhibit_matrix,i,c)) vset_minus(root->container, class_enabled[i]);
                // evaluate all guards
                learn_guards_par(root->container, guard_count);
                // set ancestors to container
                vset_copy(root->ancestors, root->container);
                // carry over root->deadlocks from previous iteration
                // set class and call next function
                root->class = c;
                CALL(reach_par_next, root, reach_groups, maybe);
                reach_stop(root);
                if (!no_soundness_check && PINS_USE_GUARDS) {
                    // For the current level the spec is sound.
                    // This means that every maybe is actually false.
                    // We thus remove all maybe's
                    for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                    for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
                }
                // update counters
                *next_count += root->next_count;
                *eg_count += root->eg_count;
                // add enabled transitions to class_enabled
                vset_copy(class_enabled[c], root->ancestors);
                vset_clear(root->ancestors);
                // remove visited states
                vset_minus(root->container, visited);
                // add new states to next_level
                vset_union(next_level, root->container);
                vset_clear(root->container);
            }
        } else {
            // set container to current level
            vset_copy(root->container, current_level);
            // evaluate all guards
            learn_guards_par(root->container, guard_count);
            // set ancestors to container
            if (root->ancestors != NULL) vset_copy(root->ancestors, current_level);
            // call next function
            CALL(reach_par_next, root, reach_groups, maybe);
            reach_stop(root);
            if (!no_soundness_check && PINS_USE_GUARDS) {
                // For the current level the spec is sound.
                // This means that every maybe is actually false.
                // We thus remove all maybe's
                for (int g = 0; g < nGuards; g++) vset_minus_par(label_true[g], label_false[g]);
                for (int g = nGuards-1; g >= 0; g--) SYNC(vset_minus_par);
            }
            // update counters
            *next_count += root->next_count;
            *eg_count += root->eg_count;
            // set next_level to new states (root->container - visited states)
            vset_copy(next_level, root->container);
            vset_clear(root->container);
            vset_minus(next_level, visited);
        }

        if (sat_strategy == NO_SAT) check_invariants(next_level, level);

        // set current_level to next_level
        vset_copy(current_level, next_level);
        vset_clear(next_level);

        if (dlk_detect) {
            deadlock_check(root->deadlocks, reach_groups);
            vset_clear(root->deadlocks);
        }

        vset_union(visited, current_level);
        vset_reorder(domain);
    }

    reach_destroy(root);
    vset_destroy(current_level);
    vset_destroy(next_level);

    if (!no_soundness_check) {
        for(int i = 0; i < nGrps; i++) {
            vset_destroy(maybe[i]);
        }
    }
}

static void
reach_chain_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                     long *eg_count, long *next_count, long *guard_count)
{
    int level = 0;
    vset_t new_states = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t new_reduced = vset_create(domain, -1, NULL);

    vset_t guard_maybe[nGuards];
    vset_t tmp = NULL;
    vset_t false_states = NULL;
    vset_t maybe_states = NULL;
    if (!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            guard_maybe[i] = vset_create(domain, l_projs[i].len, l_projs[i].proj);
        }
        false_states = vset_create(domain, -1, NULL);
        maybe_states = vset_create(domain, -1, NULL);
        tmp = vset_create(domain, -1, NULL);
    }

    vset_copy(new_states, visited);
    if (save_sat_levels) vset_minus(new_states, visited_old);

    LACE_ME;
    while (!vset_is_empty(new_states)) {
        stats_and_progress_report(new_states, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, new_states);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            if (trc_output != NULL) save_level(visited);

            vset_copy(new_reduced, new_states);
            learn_guards_reduce(new_reduced, i, guard_count, guard_maybe, false_states, maybe_states, tmp);

            if (!vset_is_empty(new_reduced)) {
                expand_group_next(i, new_reduced);
                reach_chain_stop();
                (*eg_count)++;
                (*next_count)++;
                vset_next_fn(temp, new_reduced, group_next[i]);
                vset_clear(new_reduced);
                if (dlk_detect) {
                    vset_prev(dlk_temp, temp, group_next[i], deadlocks);
                    reduce(i, dlk_temp);
                    vset_minus(deadlocks, dlk_temp);
                    vset_clear(dlk_temp);
                }

                vset_minus(temp, visited);
                vset_union(new_states, temp);
                vset_clear(temp);
            }
        }

        if (sat_strategy == NO_SAT) check_invariants(new_states, -1);

        // no deadlocks in old new_states
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);

        vset_zip(visited, new_states);
        vset_reorder(domain);
    }

    vset_destroy(new_states);
    vset_destroy(temp);
    vset_destroy(new_reduced);

    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }
}

static void
reach_chain(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                long *eg_count, long *next_count, long *guard_count)
{
    (void)visited_old;

    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t new_reduced = vset_create(domain, -1, NULL);

    vset_t guard_maybe[nGuards];
    vset_t tmp = NULL;
    vset_t false_states = NULL;
    vset_t maybe_states = NULL;
    if (!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            guard_maybe[i] = vset_create(domain, l_projs[i].len, l_projs[i].proj);
        }
        false_states = vset_create(domain, -1, NULL);
        maybe_states = vset_create(domain, -1, NULL);
        tmp = vset_create(domain, -1, NULL);
    }

    LACE_ME;
    while (!vset_equal(visited, old_vis)) {
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            if (trc_output != NULL) save_level(visited);
            vset_copy(new_reduced, visited);
            learn_guards_reduce(new_reduced, i, guard_count, guard_maybe, false_states, maybe_states, tmp);
            expand_group_next(i, new_reduced);
            reach_chain_stop();
            (*eg_count)++;
            (*next_count)++;
            vset_next_fn(temp, new_reduced, group_next[i]);
            vset_clear(new_reduced);
            vset_union(visited, temp);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i],deadlocks);
                reduce(i, dlk_temp);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_clear(temp);
        }

        if (sat_strategy == NO_SAT) check_invariants(visited, -1);

        // no deadlocks in old_vis
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
        vset_reorder(domain);
    }

    vset_destroy(old_vis);
    vset_destroy(temp);
    vset_destroy(new_reduced);

    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
    if(!no_soundness_check && PINS_USE_GUARDS) {
        for(int i=0;i<nGuards;i++) {
            vset_destroy(guard_maybe[i]);
        }
        vset_destroy(tmp);
        vset_destroy(false_states);
        vset_destroy(maybe_states);
    }
}

static void
reach_no_sat(reach_proc_t reach_proc, vset_t visited, bitvector_t *reach_groups,
                 long *eg_count, long *next_count, long *guard_count)
{
    vset_t old_visited = save_sat_levels?vset_create(domain, -1, NULL):NULL;

    reach_proc(visited, old_visited, reach_groups, eg_count, next_count, guard_count);

    if (save_sat_levels) vset_destroy(old_visited);
}

static void
reach_sat_fix(reach_proc_t reach_proc, vset_t visited,
                 bitvector_t *reach_groups, long *eg_count, long *next_count, long *guard_count)
{
    (void) reach_proc;
    (void) guard_count;

    if (PINS_USE_GUARDS)
        Abort("guard-splitting not supported with saturation=sat-fix");

    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    LACE_ME;
    while (!vset_equal(visited, old_vis)) {
        if (trc_output != NULL) save_level(visited);
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        for(int i = 0; i < nGrps; i++){
            if (!bitvector_is_set(reach_groups, i)) continue;
            expand_group_next(i, visited);
            reach_chain_stop();
            (*eg_count)++;
        }
        if (dlk_detect) vset_copy(deadlocks, visited);
        vset_least_fixpoint(visited, visited, group_next, nGrps);
        (*next_count)++;
        check_invariants(visited, level);
        if (dlk_detect) {
            for (int i = 0; i < nGrps; i++) {
                vset_prev(dlk_temp, visited, group_next[i],deadlocks);
                reduce(i, dlk_temp);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            deadlock_check(deadlocks, reach_groups);
        }
        vset_reorder(domain);
    }

    vset_destroy(old_vis);
    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

static void
initialize_levels(bitvector_t *groups, int *empty_groups, int *back,
                      bitvector_t *reach_groups)
{
    int level[nGrps];

    // groups: i = 0 .. nGrps - 1
    // vars  : j = 0 .. N - 1

    // level[i] = first '+' in row (highest in BDD) of group i
    // recast 0 .. N - 1 down to equal groups 0 .. (N - 1) / sat_granularity
    for (int i = 0; i < nGrps; i++) {
        level[i] = -1;

        for (int j = 0; j < N; j++) {
            if (dm_is_set(GBgetDMInfo(model), i, j)) {
                level[i] = (N - j - 1) / sat_granularity;
                break;
            }
        }

        if (level[i] == -1)
            level[i] = 0;
    }

    for (int i = 0; i < nGrps; i++)
        bitvector_set(&groups[level[i]], i);

    // Limit the bit vectors to the groups we are interested in and establish
    // which saturation levels are not used.
    for (int k = 0; k < max_sat_levels; k++) {
        bitvector_intersect(&groups[k], reach_groups);
        empty_groups[k] = bitvector_is_empty(&groups[k]);
    }

    if (back == NULL)
        return;

    // back[k] = last + in any group of level k
    bitvector_t level_matrix[max_sat_levels];

    for (int k = 0; k < max_sat_levels; k++) {
        bitvector_create(&level_matrix[k], N);
        back[k] = max_sat_levels;
    }

    for (int i = 0; i < nGrps; i++) {
        dm_row_union(&level_matrix[level[i]], GBgetDMInfo(model), i);
    }

    for (int k = 0; k < max_sat_levels; k++) {
        for (int j = 0; j < k; j++) {
            bitvector_t temp;
            int empty;

            bitvector_copy(&temp, &level_matrix[j]);
            bitvector_intersect(&temp, &level_matrix[k]);
            empty = bitvector_is_empty(&temp);
            bitvector_free(&temp);

            if (!empty)
                if (j < back[k]) back[k] = j;
        }

        if (back[k] == max_sat_levels && !bitvector_is_empty(&level_matrix[k]))
            back[k] = k + 1;
    }

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_free(&level_matrix[k]);

}

static void
reach_sat_like(reach_proc_t reach_proc, vset_t visited,
                   bitvector_t *reach_groups, long *eg_count, long *next_count, long *guard_count)
{
    bitvector_t groups[max_sat_levels];
    int empty_groups[max_sat_levels];
    int back[max_sat_levels];
    int k = 0;
    int last = -1;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t prev_vis[nGrps];

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_create(&groups[k], nGrps);

    initialize_levels(groups, empty_groups, back, reach_groups);

    for (int i = 0; i < max_sat_levels; i++)
        prev_vis[i] = save_sat_levels?vset_create(domain, -1, NULL):NULL;

    while (k < max_sat_levels) {
        if (k == last || empty_groups[k]) {
            k++;
            continue;
        }

        Warning(infoLong, "Saturating level: %d", k);
        vset_copy(old_vis, visited);
        reach_proc(visited, prev_vis[k], &groups[k], eg_count, next_count,guard_count);
        check_invariants(visited, -1);
        if (save_sat_levels) vset_copy(prev_vis[k], visited);
        if (vset_equal(old_vis, visited))
            k++;
        else {
            last = k;
            k = back[k];
        }
    }

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_free(&groups[k]);

    vset_destroy(old_vis);
    if (save_sat_levels)
        for (int i = 0; i < max_sat_levels; i++) vset_destroy(prev_vis[i]);
}

static void
reach_sat_loop(reach_proc_t reach_proc, vset_t visited,
                   bitvector_t *reach_groups, long *eg_count, long *next_count, long *guard_count)
{
    bitvector_t groups[max_sat_levels];
    int empty_groups[max_sat_levels];
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t prev_vis[nGrps];

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_create(&groups[k], nGrps);

    initialize_levels(groups, empty_groups, NULL, reach_groups);

    for (int i = 0; i < max_sat_levels; i++)
        prev_vis[i] = save_sat_levels?vset_create(domain, -1, NULL):NULL;

    while (!vset_equal(old_vis, visited)) {
        vset_copy(old_vis, visited);
        for (int k = 0; k < max_sat_levels; k++) {
            if (empty_groups[k]) continue;
            Warning(infoLong, "Saturating level: %d", k);
            reach_proc(visited, prev_vis[k], &groups[k], eg_count, next_count,guard_count);
            check_invariants(visited, -1);
            if (save_sat_levels) vset_copy(prev_vis[k], visited);
        }
    }

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_free(&groups[k]);

    vset_destroy(old_vis);
    if (save_sat_levels)
        for (int i = 0; i < max_sat_levels; i++) vset_destroy(prev_vis[i]);
}

static void
reach_sat(reach_proc_t reach_proc, vset_t visited,
          bitvector_t *reach_groups, long *eg_count, long *next_count, long *guard_count)
{
    (void) reach_proc;
    (void) next_count;
    (void) guard_count;

    if (PINS_USE_GUARDS)
        Abort("guard-splitting not supported with saturation=sat");

    if (act_detect != NULL && trc_output != NULL)
        Abort("Action detection with trace generation not supported");

    for (int i = 0; i < nGrps; i++) {
        if (bitvector_is_set(reach_groups, i)) {
            struct expand_info *ctx = RTmalloc(sizeof(struct expand_info));
            ctx->group = i;
            ctx->group_explored = group_explored[i];
            ctx->eg_count = eg_count;

            vrel_set_expand(group_next[i], expand_group_next_projected, ctx);
        }
    }

    if (trc_output != NULL) save_level(visited);
    stats_and_progress_report(NULL, visited, 0);
    vset_least_fixpoint(visited, visited, group_next, nGrps);
    stats_and_progress_report(NULL, visited, 1);

    check_invariants(visited, -1);

    if (dlk_detect) {
        vset_t deadlocks = vset_create(domain, -1, NULL);
        vset_t dlk_temp = vset_create(domain, -1, NULL);
        vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            vset_prev(dlk_temp, visited, group_next[i],deadlocks);
            reduce(i, dlk_temp);
            vset_minus(deadlocks, dlk_temp);
            vset_clear(dlk_temp);
        }
        deadlock_check(deadlocks, reach_groups);
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

typedef struct {
    model_t  model;
    FILE    *tbl_file;
    int      tbl_count;
    int      group;
    int     *src;
} output_context;

static void
etf_edge(void *context, transition_info_t *ti, int *dst, int *cpy)
{
    output_context* ctx = (output_context*)context;

    ctx->tbl_count++;

    for(int i = 0, k = 0 ; i < N; i++) {
        if (dm_is_set(GBgetDMInfo(ctx->model), ctx->group, i)) {
            fprintf(ctx->tbl_file, " %d/%d", ctx->src[k], dst[k]);
            k++;
        } else {
            fprintf(ctx->tbl_file," *");
        }
    }

    for(int i = 0; i < eLbls; i++)
        fprintf(ctx->tbl_file, " %d", ti->labels[i]);

    fprintf(ctx->tbl_file,"\n");
    (void)cpy;
}

static void enum_edges(void *context, int *src)
{
    output_context* ctx = (output_context*)context;

    ctx->src = src;
    GBgetTransitionsShort(model, ctx->group, ctx->src, etf_edge, context);
}

typedef struct {
    FILE *tbl_file;
    int   mapno;
    int   len;
    int  *used;
} map_context;

static void
enum_map(void *context, int *src){
    map_context *ctx = (map_context*)context;
    int val = GBgetStateLabelShort(model, ctx->mapno, src);

    for (int i = 0, k = 0; i < N; i++) {
        if (k < ctx->len && ctx->used[k] == i){
            fprintf(ctx->tbl_file, "%d ", src[k]);
            k++;
        } else {
            fprintf(ctx->tbl_file, "* ");
        }
    }

    fprintf(ctx->tbl_file, "%d\n", val);
}

static void
output_init(FILE *tbl_file)
{
    int state[N];

    GBgetInitialState(model, state);
    fprintf(tbl_file, "begin state\n");

    for (int i = 0; i < N; i++) {
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_name(ltstype, i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_type(ltstype, i));
        fprintf(tbl_file, (i == (N - 1))?"\n":" ");
    }

    fprintf(tbl_file,"end state\n");

    fprintf(tbl_file,"begin edge\n");
    for(int i = 0; i < eLbls; i++) {
        fprint_ltsmin_ident(tbl_file, lts_type_get_edge_label_name(ltstype, i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_edge_label_type(ltstype, i));
        fprintf(tbl_file, (i == (eLbls - 1))?"\n":" ");
    }

    fprintf(tbl_file, "end edge\n");

    fprintf(tbl_file, "begin init\n");

    for(int i = 0; i < N; i++)
        fprintf(tbl_file, "%d%s", state[i], (i == (N - 1))?"\n":" ");

    fprintf(tbl_file,"end init\n");
}

static void
output_trans(FILE *tbl_file)
{
    int tbl_count = 0;
    output_context ctx;

    ctx.model = model;
    ctx.tbl_file = tbl_file;

    for(int g = 0; g < nGrps; g++) {
        ctx.group = g;
        ctx.tbl_count = 0;
        fprintf(tbl_file, "begin trans\n");
        vset_enum(group_explored[g], enum_edges, &ctx);
        fprintf(tbl_file, "end trans\n");
        tbl_count += ctx.tbl_count;
    }

    Warning(info, "Symbolic tables have %d reachable transitions", tbl_count);
}

static void
output_lbls(FILE *tbl_file, vset_t visited)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);

    nGuards = dm_nrows(sl_info);

    if (dm_nrows(sl_info) != lts_type_get_state_label_count(ltstype))
        Warning(error, "State label count mismatch!");

    for (int i = 0; i < nGuards; i++){
        int len = dm_ones_in_row(sl_info, i);
        int used[len];

        // get projection
        for (int pi = 0, pk = 0; pi < dm_ncols (sl_info); pi++) {
            if (dm_is_set (sl_info, i, pi))
                used[pk++] = pi;
        }

        vset_t patterns = vset_create(domain, len, used);
        map_context ctx;

        vset_project(patterns, visited);
        ctx.tbl_file = tbl_file;
        ctx.mapno = i;
        ctx.len = len;
        ctx.used = used;
        fprintf(tbl_file, "begin map ");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_label_name(ltstype,i));
        fprintf(tbl_file, ":");
        fprint_ltsmin_ident(tbl_file, lts_type_get_state_label_type(ltstype,i));
        fprintf(tbl_file,"\n");
        vset_enum(patterns, enum_map, &ctx);
        fprintf(tbl_file, "end map\n");
        vset_destroy(patterns);
    }
}

static void
output_types(FILE *tbl_file)
{
    int type_count = lts_type_get_type_count(ltstype);

    for (int i = 0; i < type_count; i++) {
        Warning(info, "dumping type %s", lts_type_get_type(ltstype, i));
        fprintf(tbl_file, "begin sort ");
        fprint_ltsmin_ident(tbl_file, lts_type_get_type(ltstype, i));
        fprintf(tbl_file, "\n");

        int values = pins_chunk_count (model,i);

        for (int j = 0; j < values; j++) {
            chunk c    = pins_chunk_get (model, i, j);
            size_t len = c.len * 2 + 6;
            char str[len];

            chunk2string(c, len, str);
            fprintf(tbl_file, "%s\n", str);
        }

        fprintf(tbl_file,"end sort\n");
    }
}

static void
do_output(char *etf_output, vset_t visited)
{
    FILE      *tbl_file;
    rt_timer_t  timer    = RTcreateTimer();

    RTstartTimer(timer);
    Warning(info, "writing output");
    tbl_file = fopen(etf_output, "w");

    if (tbl_file == NULL)
        AbortCall("could not open %s", etf_output);

    if (vdom_separates_rw(domain)) {
        /*
         * This part is necessary because the ETF format does not yet support
         * read, write and copy. This part should thus be removed when ETF is
         * extended.
         */
        Warning(info, "Note: ETF format does not yet support read, write and copy.");
        transitions_short = GBgetTransitionsShort;

        for (int i = 0; i < nGrps; i++) {
            vset_destroy(group_explored[i]);

            RTfree(r_projs[i].proj);
            r_projs[i].len   = dm_ones_in_row(GBgetDMInfo(model), i);
            r_projs[i].proj  = (int*)RTmalloc(r_projs[i].len * sizeof(int));
            RTfree(w_projs[i].proj);
            w_projs[i].len   = dm_ones_in_row(GBgetDMInfo(model), i);
            w_projs[i].proj  = (int*)RTmalloc(w_projs[i].len * sizeof(int));

            for(int j = 0, k = 0; j < dm_ncols(GBgetDMInfo(model)); j++) {
                if (dm_is_set(GBgetDMInfo(model), i, j)) {
                    r_projs[i].proj[k] = j;
                    w_projs[i].proj[k++] = j;
                }
            }
            group_explored[i] = vset_create(domain,r_projs[i].len,r_projs[i].proj);
            vset_project(group_explored[i], visited);
        }
    }

    output_init(tbl_file);
    output_trans(tbl_file);
    output_lbls(tbl_file, visited);
    output_types(tbl_file);

    fclose(tbl_file);
    RTstopTimer(timer);
    RTprintTimer(info, timer, "writing output took");
}

static void
unguided(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
             char *etf_output)
{
    (void)etf_output;

    bitvector_t reach_groups;
    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;

    bitvector_create(&reach_groups, nGrps);
    bitvector_invert(&reach_groups);
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    bitvector_free(&reach_groups);
    if (PINS_USE_GUARDS) {
        Warning(info, "Exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
                eg_count, next_count, guard_count);
    } else {
        Warning(info, "Exploration took %ld group checks and %ld next state calls",
                eg_count, next_count);
    }
}

/**
 * Find a group that overlaps with at least one of the groups in found_groups.
 * If a group is found, 1 is returned and the group argument is set to this
 * group. If no group is found, 0 is returned.
 */
static int
find_overlapping_group(bitvector_t *found_groups, int *group)
{
    bitvector_t row_found, row_new;

    bitvector_create(&row_found, N);
    bitvector_create(&row_new, N);

    for (int i = 0; i < nGrps; i++) {
        if (!bitvector_is_set(found_groups, i)) continue;
        bitvector_clear(&row_found);
        dm_row_union(&row_found, GBgetDMInfoRead(model), i);

        for(int j = 0; j < nGrps; j++) {
            if (bitvector_is_set(found_groups, j)) continue;
            bitvector_clear(&row_new);
            dm_row_union(&row_new, GBgetDMInfoMayWrite(model), j);
            bitvector_intersect(&row_new, &row_found);

            if (!bitvector_is_empty(&row_new)) {
                *group=j;
                bitvector_free(&row_found);
                bitvector_free(&row_new);
                return 1;
            }
        }
    }

    bitvector_free(&row_found);
    bitvector_free(&row_new);
    return 0;
}

static int
establish_group_order(int *group_order, int *initial_count)
{
    int group_total = 0;
    bitvector_t found_groups;

    bitvector_create(&found_groups, nGrps);

    int* groups = NULL;
    const int n = GBgroupsOfEdge(model, act_label, act_index, &groups);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Warning(info, "Found \"%s\" potentially in group %d", act_detect, groups[i]);
            group_order[group_total] = groups[i];
            group_total++;
            bitvector_set(&found_groups, groups[i]);
        }
        RTfree(groups);
    } else Abort("No group will ever produce action \"%s\"", act_detect);

    *initial_count = group_total;

    int new_group;

    while(find_overlapping_group(&found_groups, &new_group)){
        group_order[group_total] = new_group;
        group_total++;
        bitvector_set(&found_groups, new_group);
    }

    return group_total;
}

static void
directed(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited,
                   char *etf_output)
{
    int *group_order = RTmalloc(nGrps * sizeof(int));
    int initial_count, total_count;
    bitvector_t reach_groups;

    if (act_detect == NULL)
        Abort("Guided forward search requires action");

    chunk c = chunk_str(act_detect);
    act_index = pins_chunk_put (model, action_typeno, c); // now only used for guidance heuristics

    total_count = establish_group_order(group_order, &initial_count);

    if (total_count == 0)
        Abort("Action %s does not occur", act_detect);

    bitvector_create(&reach_groups, nGrps);
    for (int i = 0; i < initial_count; i++)
        bitvector_set(&reach_groups, group_order[i]);

    long eg_count = 0;
    long next_count = 0;
    long guard_count = 0;

    // Assumption: reach_proc does not return in case action is found
    Warning(info, "Searching for action using initial groups");
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);

    for (int i = initial_count; i < total_count; i++) {
        Warning(info, "Extending action search with group %d", group_order[i]);
        bitvector_set(&reach_groups, group_order[i]);
        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    }

    if (etf_output != NULL || dlk_detect) {
        Warning(info, "Continuing for etf output or deadlock detection");

        for(int i = 0; i < nGrps; i++)
            bitvector_set(&reach_groups, i);

        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count, &guard_count);
    }

    Warning(info, "Exploration took %ld group checks, %ld next state calls and %ld guard evaluation calls",
            eg_count, next_count, guard_count);
    bitvector_free(&reach_groups);
}

static void
init_model(char *file)
{
    Warning(info, "opening %s", file);
    model = GBcreateBase();
    GBsetChunkMap (model, HREgreyboxTableFactory());

    HREbarrier(HREglobal());
    GBloadFile(model, file, &model);

    HREbarrier(HREglobal());

    if (HREme(HREglobal())==0 && !PINS_USE_GUARDS && no_soundness_check) {
        Abort("Option --no-soundness-check is incompatible with --pins-guards=false");
    }

    if (HREme(HREglobal())==0 && log_active(infoLong) && !no_matrix) {
        fprintf(stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined(stderr, model);
    }

    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = GBgetStateLabelInfo(model) == NULL ? 0 : dm_nrows(GBgetStateLabelInfo(model));
    nGrps = dm_nrows(GBgetDMInfo(model));
    max_sat_levels = (N / sat_granularity) + 1;
    if (PINS_USE_GUARDS) {
        nGuards = GBgetStateLabelGroupInfo(model, GB_SL_GUARDS)->count;
        if (HREme(HREglobal())==0) {
            Warning(info, "state vector length is %d; there are %d groups and %d guards", N, nGrps, nGuards);
        }
    } else {
        if (HREme(HREglobal())==0) {
            Warning(info, "state vector length is %d; there are %d groups", N, nGrps);
        }
    }

    int id=GBgetMatrixID(model,"inhibit");
    if (id>=0){
        inhibit_matrix=GBgetMatrix(model,id);
        if (HREme(HREglobal())==0) {
            Warning(infoLong,"inhibit matrix is:");
            if (log_active(infoLong)) dm_print(stderr,inhibit_matrix);
        }
    }
    id = GBgetMatrixID(model,LTSMIN_EDGE_TYPE_ACTION_CLASS);
    if (id>=0){
        class_matrix=GBgetMatrix(model,id);
        if (HREme(HREglobal())==0) {
            Warning(infoLong,"inhibit class matrix is:");
            if (log_active(infoLong)) dm_print(stderr,class_matrix);
        }
    }

    HREbarrier(HREglobal());
}

static void
inv_info_prepare(ltsmin_expr_t e, ltsmin_parse_env_t env, int i)
{
    struct inv_info_s* c;
    switch(e->token) {
    case PRED_NOT:
        inv_info_prepare(e->arg1, env, i);
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_AND:
    case PRED_OR:
        inv_info_prepare(e->arg1, env, i);
        inv_info_prepare(e->arg2, env, i);
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_TRUE:
    case PRED_FALSE:
        c = RTmalloc(sizeof(struct inv_info_s));
        e->destroy_context = inv_info_destroy;
        break;
    case PRED_SVAR: {
        if (e->idx >= N && !inv_bin_par) {
            c = RTmalloc(sizeof(struct inv_info_s) + sizeof(struct inv_svar_s));
            e->destroy_context = inv_svar_destroy;
            struct inv_svar_s* svar = (struct inv_svar_s*) c + sizeof(struct inv_info_s);
            
            c->work = svar;
            svar->tmp = vset_create(domain, -1, NULL);
        } else {
            c = RTmalloc(sizeof(struct inv_info_s));
            e->destroy_context = inv_info_destroy;
        }
        break;
    }
    case PRED_EQ:
    case PRED_NEQ:
    case PRED_LT:
    case PRED_LEQ:
    case PRED_GT:
    case PRED_GEQ: {
        bitvector_t deps;
        bitvector_create(&deps, N);
        set_pins_semantics(model, e, env, &deps, NULL);
        
        const int len = bitvector_n_high(&deps);

        c = RTmalloc(sizeof(struct inv_info_s)
                + sizeof(struct inv_rel_s)
                + sizeof(int[N])
                + sizeof(int[len]));

        struct inv_rel_s* rel = c->work = (struct inv_rel_s*) (c + 1);
        rel->vec = (int*) (rel + 1);
        rel->deps = (int*) (rel->vec + N);

        e->destroy_context = inv_rel_destroy;
        
        GBgetInitialState(model, rel->vec);
        
        rel->len = len;
        bitvector_high_bits(&deps, rel->deps);
        rel->tmp = vset_create(domain, rel->len, rel->deps);
        rel->true_states = vset_create(domain, rel->len, rel->deps);
        rel->false_states = vset_create(domain, rel->len, rel->deps);
        if (!inv_bin_par) rel->shortcut = vset_create(domain, -1, NULL);
        bitvector_free(&deps);
        break;
    }
    default:
        LTSminLogExpr (error, "Unhandled predicate expression: ", e, env);
        HREabort (LTSMIN_EXIT_FAILURE);
    }
    e->context = c;
    c->container = vset_create(domain, inv_proj[i].len, inv_proj[i].proj);
}

static void
init_domain(vset_implementation_t impl) {
    domain = vdom_create_domain(N, impl);

    for (int i = 0; i < dm_ncols(GBgetDMInfo(model)); i++) {
        vdom_set_name(domain, i, lts_type_get_state_name(ltstype, i));
    }

    group_next     = (vrel_t*)RTmalloc(nGrps * sizeof(vrel_t));
    group_explored = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    group_tmp      = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    r_projs        = (proj_info*)RTmalloc(nGrps * sizeof(proj_info));
    w_projs        = (proj_info*)RTmalloc(nGrps * sizeof(proj_info));

    l_projs        = (proj_info*) RTmalloc(sLbls * sizeof(proj_info));
    label_false    = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));
    label_true     = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));
    label_tmp      = (vset_t*)RTmalloc(sLbls * sizeof(vset_t));

    matrix_t* read_matrix;
    matrix_t* write_matrix;

    if (!vdom_separates_rw(domain) && !PINS_USE_GUARDS) {
        read_matrix = GBgetDMInfo(model);
        write_matrix = GBgetDMInfo(model);
        Warning(info, "Using GBgetTransitionsShort as next-state function");
        transitions_short = GBgetTransitionsShort;
    } else if (!vdom_separates_rw(domain) && PINS_USE_GUARDS) {
        read_matrix = GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS));
        write_matrix = GBgetDMInfo(model);
        Warning(info, "Using GBgetActionsShort as next-state function");
        transitions_short = GBgetActionsShort;
    } else if (vdom_separates_rw(domain) && !PINS_USE_GUARDS) {
        read_matrix = GBgetDMInfoRead(model);
        write_matrix = GBgetDMInfoMayWrite(model);
        Warning(info, "Using GBgetTransitionsShortR2W as next-state function");
        transitions_short = GBgetTransitionsShortR2W;
    } else { // vdom_separates_rw(domain) && PINS_USE_GUARDS
        read_matrix = GBgetMatrix(model, GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS));
        write_matrix = GBgetDMInfoMayWrite(model);
        Warning(info, "Using GBgetActionsShortR2W as next-state function");
        transitions_short = GBgetActionsShortR2W;
    }

    if (PINS_USE_GUARDS) {
        if (no_soundness_check) {
            Warning(info, "Guard-splitting: not checking soundness of the specification, this may result in an incorrect state space!");
        } else {
            Warning(info, "Guard-splitting: checking soundness of specification, this may be slow!");
        }
    }

    for(int i = 0; i < nGrps; i++) {
        r_projs[i].len   = dm_ones_in_row(read_matrix, i);
        r_projs[i].proj  = (int*)RTmalloc(r_projs[i].len * sizeof(int));
        w_projs[i].len   = dm_ones_in_row(write_matrix, i);
        w_projs[i].proj  = (int*)RTmalloc(w_projs[i].len * sizeof(int));

        for(int j = 0, k = 0; j < dm_ncols(read_matrix); j++) {
            if (dm_is_set(read_matrix, i, j)) r_projs[i].proj[k++] = j;
        }

        for(int j = 0, k = 0; j < dm_ncols(write_matrix); j++) {
            if (dm_is_set(write_matrix, i, j)) w_projs[i].proj[k++] = j;
        }

        if (HREme(HREglobal())==0)
        {
            if (vdom_separates_rw(domain)) {
                group_next[i]     = vrel_create_rw(domain,r_projs[i].len,r_projs[i].proj,w_projs[i].len,w_projs[i].proj);
            } else {
                group_next[i]     = vrel_create(domain,r_projs[i].len,r_projs[i].proj);
            }

            group_explored[i] = vset_create(domain,r_projs[i].len,r_projs[i].proj);
            group_tmp[i]      = vset_create(domain,r_projs[i].len,r_projs[i].proj);

            if (inhibit_matrix != NULL) {
                inhibit_class_count = dm_nrows(inhibit_matrix);
                class_enabled = (vset_t*)RTmalloc(inhibit_class_count * sizeof(vset_t));
                for(int i=0; i<inhibit_class_count; i++) {
                    class_enabled[i] = vset_create(domain, -1, NULL);
                }
            }
        }
    }

    for (int i = 0; i < sLbls; i++) {

        /* Indeed, we skip unused state labels, but allocate memory for pointers
         * (to vset_t's). Is this bad? Maybe a hashmap is worse. */
        if (bitvector_is_set(&state_label_used, i)) {
            l_projs[i].len     = dm_ones_in_row(GBgetStateLabelInfo(model), i);
            l_projs[i].proj    = (int*) RTmalloc(l_projs[i].len * sizeof(int));

            for (int j = 0, k = 0; j < dm_ncols(GBgetStateLabelInfo(model)); j++) {
                if (dm_is_set(GBgetStateLabelInfo(model), i, j)) l_projs[i].proj[k++] = j;
            }

            if (HREme(HREglobal()) == 0) {
                label_false[i]  = vset_create(domain, l_projs[i].len, l_projs[i].proj);
                label_true[i]   = vset_create(domain, l_projs[i].len, l_projs[i].proj);
                label_tmp[i]    = vset_create(domain, l_projs[i].len, l_projs[i].proj);
            }
        } else {
            label_false[i]  = NULL;
            label_true[i]   = NULL;
            label_tmp[i]    = NULL;
        }
    }

    inv_set = (vset_t*) RTmalloc(sizeof(vset_t) * num_inv);
    for (int i = 0; i < num_inv; i++) {
        inv_set[i] = vset_create(domain, inv_proj[i].len, inv_proj[i].proj);
        inv_info_prepare(inv_expr[i], inv_parse_env[i], i);
    }
}

static void
init_action_detection()
{
    if (act_label == -1)
        Abort("No edge label '%s...' for action detection", LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    int count = 8; // GBchunkCount(model, action_typeno);
    // create vector with 2 values per bucket, i.e. one bit per bucket
    Print(infoLong, "Preparing action cache for %zu action labels.", (size_t)(1ULL << count)-1);
    seen_actions = BVLLcreate (2, count);
    Warning(info, "Detecting actions with prefix \"%s\"", act_detect);
}

static void
init_invariant_detection()
{
    inv_proj = (proj_info*) RTmalloc(sizeof(proj_info) * num_inv);
    inv_expr = (ltsmin_expr_t*) RTmalloc(sizeof(ltsmin_expr_t) * num_inv);
    inv_violated = (int*) RTmallocZero(sizeof(int) * num_inv);
    inv_parse_env = (ltsmin_parse_env_t*) RTmalloc(sizeof(ltsmin_parse_env_t) * num_inv);
    inv_deps = (bitvector_t*) RTmalloc(sizeof(bitvector_t) * num_inv);
    inv_sl_deps = (bitvector_t*) RTmalloc(sizeof(bitvector_t) * num_inv);
    
    for (int i = 0; i < num_inv; i++) {
        inv_parse_env[i] = LTSminParseEnvCreate();
        inv_expr[i] = pred_parse_file(inv_detect[i], inv_parse_env[i], ltstype);
        if (log_active(infoLong)) {
            const char s[] = "Loaded and optimized invariant #%d: ";
            char buf[snprintf(NULL, 0, s, i + 1) + 1];
            sprintf(buf, s, i + 1);
            LTSminLogExpr(infoLong, buf, inv_expr[i], inv_parse_env[i]);
        }
        bitvector_create(&inv_deps[i], N);
        bitvector_create(&inv_sl_deps[i], sLbls);
        set_pins_semantics(model, inv_expr[i], inv_parse_env[i], &inv_deps[i], &inv_sl_deps[i]);
        inv_proj[i].len = bitvector_n_high(&inv_deps[i]);
        inv_proj[i].proj = (int*) RTmalloc(inv_proj[i].len * sizeof(int));
        bitvector_high_bits(&inv_deps[i], inv_proj[i].proj);
    }

    inv_cleanup();

    if (inv_par) label_locks = (int*) RTmallocZero(sizeof(int[sLbls]));
}

/* Naive textbook mu-calculus algorithm
 * Taken from:
 * Model Checking and the mu-calculus, E. Allen Emerson
 * DIMACS Series in Discrete Mathematics, 1997 - Citeseer
 */
static vset_t
mu_compute(ltsmin_expr_t mu_expr, ltsmin_parse_env_t env, vset_t visited, vset_t* mu_var, array_manager_t mu_var_man)
{
    vset_t result = NULL;
    switch(mu_expr->token) {
    case MU_TRUE:
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        return result;
    case MU_FALSE:
        return vset_create(domain, -1, NULL);
    case MU_OR: { // OR
        result = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_t mc = mu_compute(mu_expr->arg2, env, visited, mu_var, mu_var_man);
        vset_union(result, mc);
        vset_destroy(mc);
    } break;
    case MU_AND: { // AND
        result = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_t mc = mu_compute(mu_expr->arg2, env, visited, mu_var, mu_var_man);
        vset_intersect(result, mc);
        vset_destroy(mc);
    } break;
    case MU_NOT: { // NEGATION
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        vset_t mc = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
        vset_minus(result, mc);
        vset_destroy(mc);
    } break;
    case MU_EXIST: { // E
        if (mu_expr->arg1->token == MU_NEXT) {
            vset_t temp = vset_create(domain, -1, NULL);
            result = vset_create(domain, -1, NULL);
            vset_t g = mu_compute(mu_expr->arg1->arg1, env, visited, mu_var, mu_var_man);

            for(int i=0;i<nGrps;i++){
                vset_prev(temp,g,group_next[i],visited);
                reduce(i, temp);
                vset_union(result,temp);
                vset_clear(temp);
            }
            vset_destroy(temp);
        } else {
            Abort("invalid operator following MU_EXIST, expecting MU_NEXT");
        }
    } break;
    case MU_SVAR: {
        if (mu_expr->idx < N) { // state variable
            Abort("Unhandled MU_SVAR");
        } else { // state label
            result = vset_create(domain, -1, NULL);
            vset_join(result, visited, label_true[mu_expr->idx - N]);
        }
    } break;
    case MU_VAR:
        ensure_access(mu_var_man, mu_expr->idx);
        result = vset_create(domain, -1, NULL);
        vset_copy(result, mu_var[mu_expr->idx]);
        break;
    case MU_ALL:
        if (mu_expr->arg1->token == MU_NEXT) {
            // implemented as AX phi = ! EX ! phi

            result = vset_create(domain, -1, NULL);
            vset_copy(result, visited);

            // compute ! phi
            vset_t notphi = vset_create(domain, -1, NULL);
            vset_copy(notphi, visited);
            vset_t phi = mu_compute(mu_expr->arg1->arg1, env, visited, mu_var, mu_var_man);
            vset_minus(notphi, phi);
            vset_destroy(phi);

            vset_t temp = vset_create(domain, -1, NULL);
            vset_t prev = vset_create(domain, -1, NULL);

            // EX !phi
            for(int i=0;i<nGrps;i++){
                vset_prev(temp,notphi,group_next[i],visited);
                reduce(i, temp);
                vset_union(prev,temp);
                vset_clear(temp);
            }
            vset_destroy(temp);

            // and negate result again
            vset_minus(result, prev);
            vset_destroy(prev);
            vset_destroy(notphi);
        } else {
            Abort("invalid operator following MU_ALL, expecting MU_NEXT");
        }
        break;
    case MU_MU:
        {
            ensure_access(mu_var_man, mu_expr->idx);
            // backup old var reference
            vset_t old = mu_var[mu_expr->idx];
            result = mu_var[mu_expr->idx] = vset_create(domain, -1, NULL);
            vset_t tmp = vset_create(domain, -1, NULL);
            do {
                vset_copy(mu_var[mu_expr->idx], tmp);
                vset_clear(tmp);
                tmp = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
            } while (!vset_equal(mu_var[mu_expr->idx], tmp));
            vset_destroy(tmp);
            // new var reference
            mu_var[mu_expr->idx] = old;
        }
        break;
    case MU_NU:
        {
            ensure_access(mu_var_man, mu_expr->idx);
            // backup old var reference
            vset_t old = mu_var[mu_expr->idx];
            result = mu_var[mu_expr->idx] = vset_create(domain, -1, NULL);
            vset_t tmp = vset_create(domain, -1, NULL);
            vset_copy(tmp, visited);
            do {
                vset_copy(mu_var[mu_expr->idx], tmp);
                vset_clear(tmp);
                tmp = mu_compute(mu_expr->arg1, env, visited, mu_var, mu_var_man);
            } while (!vset_equal(mu_var[mu_expr->idx], tmp));
            vset_destroy(tmp);
            // new var reference
            mu_var[mu_expr->idx] = old;
        }
        break;
    case MU_EQ:
    case MU_NEQ:
    case MU_LT:
    case MU_LEQ:
    case MU_GT:
    case MU_GEQ: {
        result = vset_create(domain, -1, NULL);

        bitvector_t deps;
        bitvector_create(&deps, N);

        set_pins_semantics(model, mu_expr, env, &deps, NULL);
        struct rel_expr_info ctx;

        int vec[N];
        GBsetInitialState(model, vec);
        ctx.vec = vec;
        ctx.len = bitvector_n_high(&deps);
        int d[ctx.len];
        bitvector_high_bits(&deps, d);
        bitvector_free(&deps);
        ctx.deps = d;

        ctx.e = mu_expr;
        ctx.env = env;

        vset_t tmp = vset_create(domain, ctx.len, d);
        vset_project(tmp, visited);

        // count when verbose
        if (log_active(infoLong)) {
            double elem_count;
            vset_count(tmp, NULL, &elem_count);
            if (elem_count >= 10000.0 * SPEC_REL_PERF) {
                const char* p = LTSminPrintExpr(mu_expr, env);
                Print(infoLong, "evaluating subformula %s for %.*g states.", p, DBL_DIG, elem_count);
            }
        }

        vset_t true_states = vset_create(domain, ctx.len, d);

        vset_update(true_states, tmp, rel_expr_cb, &ctx);

        vset_join(result, true_states, visited);
        vset_destroy(tmp);
        vset_destroy(true_states);
        break;
    }
    default:
        Abort("encountered unhandled mu operator");
    }
    return result;
}

static array_manager_t* mu_var_mans = NULL;
static vset_t** mu_vars = NULL;

static void
init_mu_calculus()
{
    int total = num_mu + num_ctl_star + num_ctl + num_ltl;
    if (total > 0) {
        mu_parse_env = (ltsmin_parse_env_t*) RTmalloc(sizeof(ltsmin_parse_env_t) * total);
	mu_exprs = (ltsmin_expr_t*) RTmalloc(sizeof(ltsmin_expr_t) * total);
	total = 0;
        for (int i = 0; i < num_mu; i++) {
            mu_parse_env[i] = LTSminParseEnvCreate();
            Warning(info, "parsing mu-calculus formula");
            mu_exprs[i] = mu_parse_file(mu_formulas[i], mu_parse_env[i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Loaded and optimized mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[i], mu_parse_env[i]);
            }
        }
	total += num_mu;
        for (int i = 0; i < num_ctl_star; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing CTL* formula");
            ltsmin_expr_t ctl_star = ctl_parse_file(ctl_star_formulas[i], mu_parse_env[total + i], ltstype);
            Warning(info, "converting CTL* %s to mu-calculus", ctl_star_formulas[i]);
            mu_exprs[total + i] = ctl_star_to_mu(ctl_star);
            if (log_active(infoLong)) {
                const char s[] = "Converted CTL* to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
	total += num_ctl_star;
        for (int i = 0; i < num_ctl; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing CTL formula");
            mu_exprs[total + i] = ctl_parse_file(ctl_formulas[i], mu_parse_env[total + i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Loaded and optimized CTL formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
            Warning(info, "converting CTL to mu-calculus...");
            mu_exprs[total + i] = ctl_to_mu(mu_exprs[total + i], mu_parse_env[total + i], ltstype);
            if (log_active(infoLong)) {
                const char s[] = "Converted CTL to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
	total += num_ctl;
        for (int i = 0; i < num_ltl; i++) {
            mu_parse_env[total + i] = LTSminParseEnvCreate();
            Warning(info, "parsing LTL formula");
            ltsmin_expr_t ltl = ctl_parse_file(ltl_formulas[i], mu_parse_env[total + i], ltstype);
            Warning(info, "converting LTL %s to mu-calculus", ltl_formulas[i]);
            mu_exprs[total + i] = ltl_to_mu(ltl);
            if (log_active(infoLong)) {
                const char s[] = "Converted LTL to mu-calculus formula #%d: ";
                char buf[snprintf(NULL, 0, s, i + 1) + 1];
                sprintf(buf, s, i + 1);
                LTSminLogExpr(infoLong, buf, mu_exprs[total + i], mu_parse_env[total + i]);
            }
        }
	total += num_ltl;

	num_total = total;

        mu_var_mans = (array_manager_t*) RTmalloc(sizeof(array_manager_t) * num_total);
        mu_vars = (vset_t**) RTmalloc(sizeof(vset_t*) * num_total);

        for (int i = 0; i < num_total; i++) {
            // setup var manager
            mu_var_mans[i] = create_manager(65535);
            mu_vars[i] = NULL;
            ADD_ARRAY(mu_var_mans[i], mu_vars[i], vset_t);
        }
    }
}

/**
 * \brief Initialises the data structures for generating symbolic parity games.
 */
void init_spg(model_t model)
{
    lts_type_t type = GBgetLTStype(model);
    var_pos = 0;
    var_type_no = 0;
    for(int i=0; i<N; i++)
    {
        //Printf(infoLong, "%d: %s (%d [%s])\n", i, lts_type_get_state_name(type, i), lts_type_get_state_typeno(type, i), lts_type_get_state_type(type, i));
#ifdef LTSMIN_PBES
        char* str1 = "string"; // for the PBES language module
#else
        char* str1 = "mu"; // for the mu-calculus PINS layer
#endif
        size_t strlen1 = strlen(str1);
        char* str2 = lts_type_get_state_type(type, i);
        size_t strlen2 = strlen(str2);
        if (strlen1==strlen2 && strncmp(str1, str2, strlen1)==0)
        {
            var_pos = i;
            var_type_no = lts_type_get_state_typeno(type, i);
            if (GBhaveMucalc()) {
                true_index = 0; // enforced by mucalc parser (mucalc-grammar.lemon / mucalc-syntax.c)
                false_index = 1;
            } else { // required for the PBES language module.
                true_index = pins_chunk_put (model, var_type_no, chunk_str("true"));
                false_index = pins_chunk_put (model, var_type_no, chunk_str("false"));
            }
        }
    }
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    variable_projection = vproj_create(domain, p_len, proj);

    num_vars = pins_chunk_count (model, var_type_no); // number of propositional variables
    if (GBhaveMucalc()) {
        num_vars = GBgetMucalcNodeCount(); // number of mu-calculus subformulae
    }
    Print(infoLong, "init_spg: var_type_no=%d, num_vars=%zu", var_type_no, num_vars);
    priority = RTmalloc(num_vars * sizeof(int)); // priority of variables
    player = RTmalloc(num_vars * sizeof(int)); // player of variables
    for(size_t i=0; i<num_vars; i++)
    {
        lts_type_t type = GBgetLTStype(model);
        int state_length = lts_type_get_state_length(type);
        // create dummy state with variable i:
        int state[state_length];
        for(int j=0; j < state_length; j++)
        {
            state[j] = 0;
        }
        state[var_pos] = i;
        int label = GBgetStateLabelLong(model, PG_PRIORITY, state); // priority
        priority[i] = label;
        if (label < min_priority) {
            min_priority = label;
        }
        if (label > max_priority) {
            max_priority = label;
        }
        //Print(infoLong, "  label %d (priority): %d", 0, label);
        label = GBgetStateLabelLong(model, PG_PLAYER, state); // player
        player[i] = label;
        //Print(infoLong, "  label %d (player): %d", 1, label);
    }
    true_states = vset_create(domain, -1, NULL);
    false_states = vset_create(domain, -1, NULL);
}

/**
 * \brief Creates a symbolic parity game from the generated LTS.
 */
parity_game* compute_symbolic_parity_game(vset_t visited, int* src)
{
    Print(infoShort, "Computing symbolic parity game.");
    debug_output_enabled = true;

    // num_vars and player have been pre-computed by init_pbes.
    parity_game* g = spg_create(domain, N, nGrps, min_priority, max_priority);
    for(int i=0; i < N; i++)
    {
        g->src[i] = src[i];
    }
    vset_copy(g->v, visited);
    for(size_t i = 0; i < num_vars; i++)
    {
        // players
        Print(infoLong, "Adding nodes for var %zu (player %d).", i, player[i]);
        add_variable_subset(g->v_player[player[i]], g->v, g->domain, i);
        // priorities
        add_variable_subset(g->v_priority[priority[i]], g->v, g->domain, i);
    }
    if (log_active(infoLong))
    {
        for(int p = 0; p < 2; p++)
        {
            long   n_count;
            double elem_count;
            vset_count(g->v_player[p], &n_count, &elem_count);
            Print(infoLong, "player %d: %ld nodes, %.*g elements.", p, n_count, DBL_DIG, elem_count);
        }
        for(int p = min_priority; p <= max_priority; p++)
        {
            long   n_count;
            double elem_count;
            vset_count(g->v_priority[p], &n_count, &elem_count);
            Print(infoLong, "priority %d: %ld nodes, %.*g elements.", p, n_count, DBL_DIG, elem_count);
        }
    }
    for(int i = 0; i < nGrps; i++)
    {
        g->e[i] = group_next[i];
    }
    return g;
}

#define CHECK_MU(s, i) \
    if (mu_par) check_mu_par((s), (i)); \
    else check_mu((s), (i));

#define check_mu_go(v, i, s) CALL(check_mu_go, (v), (i), (s))
VOID_TASK_3(check_mu_go, vset_t, visited, int, i, int*, init)
{
    vset_t x = mu_compute(mu_exprs[i], mu_parse_env[i], visited, mu_vars[i], mu_var_mans[i]);
    if (x != NULL) {
        double e_count;
        vset_count(x, NULL, &e_count);
	char* formula = NULL;
	// recall: mu-formulas, ctl-star formulas, ctl-formulas, ltl-formulas
	if (i < num_mu) {
            formula = mu_formulas[i];
	} else if (i < num_mu + num_ctl_star) {
            formula = ctl_star_formulas[i - num_mu];
	} else if (i < num_mu + num_ctl_star + num_ctl) {
	    formula = ctl_formulas[i - num_mu - num_ctl_star];
	} else if (i < num_mu + num_ctl_star + num_ltl) {
            formula = ltl_formulas[i - num_mu - num_ctl_star - num_ctl];
	} else {
            Warning(error, "Number of formulas doesn't match (%d+%d+%d+%d)", num_mu, num_ctl_star, num_ctl, num_ltl);
        }

        Warning(info, "Formula %s holds for %.*g states,", formula, DBL_DIG, e_count);
        Warning(info, "the initial state is %sin the set", vset_member(x, init) ? "" : "not ");
        vset_destroy(x);
    }
}

static void
check_mu(vset_t visited, int* init)
{
    if (num_total > 0) {
        Print(infoLong, "Starting mu-calculus model checking.");
        learn_labels(visited);
        for (int i = 0; i < num_total; i++) {
            LACE_ME;
            check_mu_go(visited, i, init);
        }
    }
}

static void
check_mu_par(vset_t visited, int* init)
{
    LACE_ME;
    if (num_total > 0) {
        Print(infoLong, "Starting parallel mu-calculus model checking.");
        learn_labels_par(visited);
        for (int i = 0; i < num_total; i++) {
            SPAWN(check_mu_go, visited, i, init);
        }

        for (int i = 0; i < num_total; i++) SYNC(check_mu_go);
    }
}

VOID_TASK_2(run_reachability, vset_t, states, char*, etf_output)
{
    sat_proc_t sat_proc = NULL;
    reach_proc_t reach_proc = NULL;
    guided_proc_t guided_proc = NULL;

    switch (strategy) {
    case BFS_P:
        reach_proc = reach_bfs_prev;
        break;
    case PAR:
        reach_proc = reach_par;
        break;
    case PAR_P:
        reach_proc = reach_par_prev;
        break;
    case BFS:
        reach_proc = reach_bfs;
        break;
    case CHAIN_P:
        reach_proc = reach_chain_prev;
        break;
    case CHAIN:
        reach_proc = reach_chain;
        break;
    case NONE:
        reach_proc = reach_none;
        break;
    }

    switch (sat_strategy) {
    case NO_SAT:
        sat_proc = reach_no_sat;
        break;
    case SAT_LIKE:
        sat_proc = reach_sat_like;
        break;
    case SAT_LOOP:
        sat_proc = reach_sat_loop;
        break;
    case SAT_FIX:
        sat_proc = reach_sat_fix;
        break;
    case SAT:
        sat_proc = reach_sat;
        break;
    }

    switch (guide_strategy) {
    case UNGUIDED:
        guided_proc = unguided;
        break;
    case DIRECTED:
        guided_proc = directed;
        break;
    }

    RTstartTimer(reach_timer);
    guided_proc(sat_proc, reach_proc, states, etf_output);
    RTstopTimer(reach_timer);
}

static char *files[2];

struct args_t
{
    int argc;
    char **argv;
};

VOID_TASK_1(init_hre, hre_context_t, context)
{
    if (LACE_WORKER_ID != 0) {
        HREprocessSet(context);
        HREglobalSet(context);
    }
}

VOID_TASK_1(actual_main, void*, arg)
{
    int argc = ((struct args_t*)arg)->argc;
    char **argv = ((struct args_t*)arg)->argv;

    /* initialize HRE */
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a symbolic reachability analysis of <model>\n"
                  "The optional output of this analysis is an ETF "
                      "representation of the input\n\nOptions");
    lts_lib_setup(); // add options for LTS library
    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    /* initialize HRE on other workers */
    TOGETHER(init_hre, HREglobal());

    /* check for unsupported options */
    if (PINS_POR != PINS_POR_NONE) Abort("Partial-order reduction and symbolic model checking are not compatible.");
    if (inhibit_matrix != NULL && sat_strategy != NO_SAT) Abort("Maximal progress is incompatibale with saturation.");
    if (files[1] != NULL && strcmp(files[1] + strlen(files[1]) - 4, ".etf") != 0) Abort("Only ETF output format is supported.");

    /* turn off Lace for now to speed up while not using parallelism */
    lace_suspend();

    /* initialize the model and PINS wrappers */
    init_model(files[0]);

    /* initialize action detection */
    act_label = lts_type_find_edge_label_prefix (ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    if (act_label != -1) action_typeno = lts_type_get_edge_label_typeno(ltstype, act_label);
    if (act_detect != NULL) init_action_detection();

    bitvector_create(&state_label_used, sLbls);
    if (inv_detect != NULL) init_invariant_detection();
    else if (PINS_USE_GUARDS) {
        for (int i = 0; i < nGuards; i++) {
            bitvector_set(&state_label_used, i);
        }
    }

    /* turn on Lace again (for Sylvan) */
    if (vset_default_domain==VSET_Sylvan || vset_default_domain==VSET_LDDmc) {
        lace_resume();
    }

    int *src;
    vset_t initial;

    if (next_union) vset_next_fn = vset_next_union_src;

    if (transitions_load_filename != NULL) {
        FILE *f = fopen(transitions_load_filename, "r");
        if (f == 0) Abort("Cannot open '%s' for reading!", transitions_load_filename);

        domain = vdom_create_domain_from_file(f, VSET_IMPL_AUTOSELECT);

        /* Call hook */
        vset_pre_load(f, domain);

        /* Read initial state */
        initial = vset_load(f, domain);

        /* Read number of transitions and all transitions */
        if (fread(&nGrps, sizeof(int), 1, f)!=1) Abort("Invalid file format.");
        group_next = (vrel_t*)RTmalloc(nGrps * sizeof(vrel_t));
        for(int i = 0; i < nGrps; i++) group_next[i] = vrel_load_proj(f, domain);
        for(int i = 0; i < nGrps; i++) vrel_load(f, group_next[i]);

        /* Call hook */
        vset_post_load(f, domain);

        /* Done! */
        fclose(f);

        /* Load state into src and initialize globals */
        N = vdom_vector_size(domain);
        src = (int*)alloca(sizeof(int)*N);
        vset_example(initial, src);
        // we do not need group_explored, group_tmp, projs
        group_explored = group_tmp = NULL;
        r_projs = w_projs = NULL;

        Print(infoShort, "Loaded transition relations from '%s'...", files[0]);
        expand_groups = 0;
    } else {
        init_domain(VSET_IMPL_AUTOSELECT);

        initial = vset_create(domain, -1, NULL);
        src = (int*)alloca(sizeof(int)*N);
        GBgetInitialState(model, src);
        vset_add(initial, src);

        Print(infoShort, "got initial state");
        expand_groups = 1;
    }

    /* if writing .dot files, open directory first */
    if (dot_dir != NULL) {
        DIR* dir = opendir(dot_dir);
        if (dir) {
            closedir(dir);
        } else if (ENOENT == errno) {
            Abort("Option 'dot-dir': directory '%s' does not exist", dot_dir);
        } else {
            Abort("Option 'dot-dir': failed opening directory '%s'", dot_dir);
        }
    }

    init_mu_calculus();

    /* determine if we need to generate a symbolic parity game */
#ifdef LTSMIN_PBES
    bool spg = true;
#else
    bool spg = GBhaveMucalc() ? true : false;
#endif

    /* if spg, then initialize labeling stuff before reachability */
    if (spg) {
        Print(infoShort, "Generating a Symbolic Parity Game (SPG).");
        init_spg(model);
    }

    /* create timer */
    reach_timer = RTcreateTimer();

    /* fix level 0 */
    vset_t visited = vset_create(domain, -1, NULL);
    vset_copy(visited, initial);

    /* check the invariants at level 0 */
    check_invariants(visited, 0);

    /* run reachability */
    CALL(run_reachability, visited, files[1]);

    /* report states */
    final_stat_reporting(visited);

    /* save vset/vrel data */
    if (transitions_save_filename != NULL) {
        FILE *f = fopen(transitions_save_filename, "w");
        if (f == NULL) Abort("Cannot open '%s' for writing!", transitions_save_filename);

        /* Call hook */
        vset_pre_save(f, domain);

        /* Write domain */
        vdom_save(f, domain);

        /* Write initial state */
        vset_save(f, initial);

        /* Write number of transitions and all transitions */
        fwrite(&nGrps, sizeof(int), 1, f);
        for (int i=0; i<nGrps; i++) vrel_save_proj(f, group_next[i]);
        for (int i=0; i<nGrps; i++) vrel_save(f, group_next[i]);

        /* Write reachable states (optional) */
        fwrite(&save_reachable, sizeof(int), 1, f);
        if (save_reachable) vset_save(f, visited);

        /* Call hook */
        vset_post_save(f, domain);

        /* Done! */
        fclose(f);

        Print(infoShort, "Transition relations written to '%s'\n", transitions_save_filename);
    }

    CHECK_MU(visited, src);

    /* save LTS */
    if (files[1] != NULL) {
        do_output(files[1], visited);
    }

    /* optionally print counts of all group_next and group_explored sets */
    long   n_count;
    double e_count;

    long total_node_count = 0;
    long explored_total_node_count = 0;
    double explored_total_vector_count = 0;
    for(int i=0; i<nGrps; i++) {

        vrel_count(group_next[i], &n_count, &e_count);
        Print(infoLong, "group_next[%d]: %.*g short vectors %ld nodes", i, DBL_DIG, e_count, n_count);
        total_node_count += n_count;

        vset_count(group_explored[i], &n_count, &e_count);
        Print(infoLong, "group_explored[%d]: %.*g short vectors, %ld nodes", i, DBL_DIG, e_count, n_count);
        explored_total_node_count += n_count;
        explored_total_vector_count += e_count;
    }
    Print(info, "group_next: %ld nodes total", total_node_count);
    Print(info, "group_explored: %ld nodes, %.*g short vectors total", explored_total_node_count, DBL_DIG, explored_total_vector_count);

    if (PINS_USE_GUARDS) {
        long total_false = 0;
        long total_true = 0;
        explored_total_vector_count = 0;
        for(int i=0;i<nGuards; i++) {
            vset_count(label_false[i], &n_count, &e_count);
            Print(infoLong, "guard_false[%d]: %.*g short vectors, %ld nodes", i, DBL_DIG, e_count, n_count);
            total_false += n_count;
            explored_total_vector_count += e_count;

            vset_count(label_true[i], &n_count, &e_count);
            Print(infoLong, "guard_true[%d]: %.*g short vectors, %ld nodes", i, DBL_DIG, e_count, n_count);
            total_true += n_count;
            explored_total_vector_count += e_count;
        }
        Print(info, "guard_false: %ld nodes total", total_false);
        Print(info, "guard_true: %ld nodes total", total_true);
        Print(info, "guard: %.*g short vectors total", DBL_DIG, explored_total_vector_count);
    }

    if (spg) { // converting the LTS to a symbolic parity game, save and solve.
        vset_destroy(true_states);
        vset_destroy(false_states);
        if (pg_output || pgsolve_flag) {
            rt_timer_t compute_pg_timer = RTcreateTimer();
            RTstartTimer(compute_pg_timer);
            parity_game * g = compute_symbolic_parity_game(visited, src);
            RTstopTimer(compute_pg_timer);
            RTprintTimer(info, compute_pg_timer, "computing symbolic parity game took");
            if (pg_output) {
                Print(info,"Writing symbolic parity game to %s.",pg_output);
                FILE *f = fopen(pg_output, "w");
                spg_save(f, g);
                fclose(f);
            }
            if (pgsolve_flag) {
                spgsolver_options* spg_options = spg_get_solver_options();
                rt_timer_t pgsolve_timer = RTcreateTimer();
                Print(info, "Solving symbolic parity game for player %d.", spg_options->player);
                RTstartTimer(pgsolve_timer);
                recursive_result strategy;
                parity_game* copy = NULL;
                if (spg_options->check_strategy) {
                    copy = spg_copy(g);
                }
                bool result = spg_solve(g, &strategy, spg_options);
                Print(info, " ");
                Print(info, "The result is: %s.", result ? "true":"false");
                RTstopTimer(pgsolve_timer);
                Print(info, " ");
                RTprintTimer(info, reach_timer,               "reachability took   ");
                RTprintTimer(info, compute_pg_timer,    "computing game took ");
                RTprintTimer(info, pgsolve_timer,       "solving took        ");
                if (spg_options->strategy_filename != NULL)
                {
                    Print(info, "Writing winning strategies to %s", spg_options->strategy_filename);
                    FILE* f = fopen(spg_options->strategy_filename, "w");
                    result_save(f, strategy);
                    fclose(f);
                }
                if (spg_options->check_strategy)
                {
                    check_strategy(copy, &strategy, spg_options->player, result, 10);
                }
            } else {
                spg_destroy(g);
            }
        }
        if (player != 0) {
            RTfree(player);
            RTfree(priority);
        }
    }

    /* in case other Lace threads were still suspended... */
    if (vset_default_domain!=VSET_Sylvan && vset_default_domain!=VSET_LDDmc) {
        lace_resume();
    } else if (SYLVAN_STATS) {
        sylvan_stats_report(stderr, 0);
    }

    GBExit(model);
}

int
main (int argc, char *argv[])
{
    poptContext optCon = poptGetContext(NULL, argc, (const char**)argv, lace_options, 0);
    while(poptGetNextOpt(optCon) != -1 ) { /* ignore errors */ }
    poptFreeContext(optCon);

#if !SPEC_MT_SAFE
    if (lace_n_workers != 1) lace_n_workers = 1;
    Warning(info, "Falling back to 1 LACE worker, since front-end is not thread-safe.");
#endif
    
#if defined(PROB)
    if (lace_n_workers != 1) lace_n_workers = 1;
    Warning(info, "Falling back to 1 LACE worker, since the ProB front-end is not yet compatible with HRE.");
#endif

    struct args_t args = (struct args_t){argc, argv};
    lace_init(lace_n_workers, lace_dqsize);
    lace_startup(lace_stacksize, TASK(actual_main), (void*)&args);

    return 0;
}
