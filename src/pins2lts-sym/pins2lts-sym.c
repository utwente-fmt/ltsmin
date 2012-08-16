#include <hre/config.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/property-semantics.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-syntax.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <spg-lib/spg-solve.h>
#include <vset-lib/vector_set.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>


static ltsmin_expr_t mu_expr = NULL;
static char* ctl_formula = NULL;
static char* mu_formula  = NULL;

static char* trc_output = NULL;
static int   dlk_detect = 0;
static char* act_detect = NULL;
static char* inv_detect = NULL;
static int   no_exit = 0;
static int   act_index;
static int   act_label;
static int   sat_granularity = 10;
static int   save_sat_levels = 0;

#if defined(PBES)
static int   pgsolve_flag = 0;
static int   pgreduce_flag = 0;
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
#endif

static enum { BFS_P , BFS , CHAIN_P, CHAIN } strategy = BFS_P;

static char* order = "bfs-prev";
static si_map_entry ORDER[] = {
    {"bfs-prev", BFS_P},
    {"bfs", BFS},
    {"chain-prev", CHAIN_P},
    {"chain", CHAIN},
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
        } else {
            Warning(info, "Exploration order is %s", order);
        }
        strategy = res;

        res = linear_search(SATURATION, saturation);
        if (res < 0) {
            Warning(error, "unknown saturation strategy %s", saturation);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else {
            Warning(info, "Saturation strategy is %s", saturation);
        }
        sat_strategy = res;

        res = linear_search(GUIDED, guidance);
        if (res < 0) {
            Warning(error, "unknown guided search strategy %s", guidance);
            HREexitUsage(LTSMIN_EXIT_FAILURE);
        } else {
            Warning(info, "Guided search strategy is %s", guidance);
        }
        guide_strategy = res;

        if (trc_output != NULL && !dlk_detect && act_detect == NULL)
            Warning(info, "Ignoring trace output");

        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        Abort("unexpected call to reach_popt");
    }
}

static  struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)reach_popt , 0 , NULL , NULL },
    { "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , "<bfs-prev|bfs|chain-prev|chain>" },
    { "saturation" , 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &saturation , 0 , "select the saturation strategy" , "<none|sat-like|sat-loop|sat-fix|sat>" },
    { "sat-granularity" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sat_granularity , 0 , "set saturation granularity","<number>" },
    { "save-sat-levels", 0, POPT_ARG_VAL, &save_sat_levels, 1, "save previous states seen at saturation levels", NULL },
    { "guidance", 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &guidance, 0 , "select the guided search strategy" , "<unguided|directed>" },
    { "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
    { "action" , 0 , POPT_ARG_STRING , &act_detect , 0 , "detect action" , "<action>" },
    { "invariant", 'i', POPT_ARG_STRING, &inv_detect, 1, "detect deadlocks", NULL },
    { "no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
    { "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts-file>.gcf" },
    { "mu" , 0 , POPT_ARG_STRING , &mu_formula , 0 , "file with a mu formula" , "<mu-file>.mu" },
    { "ctl" , 0 , POPT_ARG_STRING , &ctl_formula , 0 , "file with a ctl* formula" , "<ctl-file>.ctl" },
#if defined(PBES)
#ifdef HAVE_LIBSPG
    { "pg-solve" , 0 , POPT_ARG_NONE , &pgsolve_flag, 0, "Solve the generated parity game (only for symbolic tool).","" },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, spg_solve_options , 0, "Symbolic parity game solver options", NULL},
    { "pg-reduce" , 0 , POPT_ARG_NONE , &pgreduce_flag, 0, "Reduce the generated parity game on-the-fly (only for symbolic tool).","" },
    { "pg-write" , 0 , POPT_ARG_STRING , &pg_output, 0, "file to write symbolic parity game to","<pg-file>.spg" },
#endif
#endif
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
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
static int nGrps;
static int max_sat_levels;
static proj_info *projs;
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

typedef void (*reach_proc_t)(vset_t visited, vset_t visited_old,
                             bitvector_t *reach_groups,
                             long *eg_count, long *next_count);

typedef void (*sat_proc_t)(reach_proc_t reach_proc, vset_t visited,
                           bitvector_t *reach_groups,
                           long *eg_count, long *next_count);

typedef void (*guided_proc_t)(sat_proc_t sat_proc, reach_proc_t reach_proc,
                              vset_t visited, char *etf_output);

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

  if (sLbls != 0)
      GBgetStateLabelsAll(model, state, labels);

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
write_trace_next(void *arg, transition_info_t *ti, int *dst)
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

    GBgetTransitionsAll(model, src, write_trace_next, &ctx);

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

            if (int_level == max_int_level) {
                max_int_level += 32;
                int_levels = RTrealloc(int_levels,
                                           sizeof(vset_t[max_int_level]));

                for(int i = int_level; i < max_int_level; i++)
                    int_levels[i] = vset_create(domain, -1, NULL);
            }

            for (int i=0; i < nGrps; i++) {
                vset_prev(temp, int_levels[int_level - 1], group_next[i]);
                vset_union(int_levels[int_level], temp);
            }

            vset_copy(temp, levels[prev_level]);
            vset_minus(temp, int_levels[int_level]);
        } while (vset_equal(levels[prev_level], temp));

        if (current_state + int_level >= max_states) {
            int old_max_states = max_states;

            max_states = current_state + int_level + 1024;
            states = RTrealloc(states,sizeof(int*[max_states]));

            for(int i = old_max_states; i < max_states; i++)
                states[i] = RTmalloc(sizeof(int[N]));
        }

        // here: temp = levels[prev_level] - int_levels[int_level]
        vset_copy(src_set, levels[prev_level]);
        vset_minus(src_set, temp);
        vset_example(src_set, states[current_state + int_level - 1]);
        vset_clear(src_set);

        // find the states that give us a trace to states[current_state - 1]
        for(int i = int_level - 1; i > 0; i--) {
            vset_add(src_set, states[current_state + i]);

            for(int j = 0; j < nGrps; j++) {
                vset_next(temp, src_set, group_next[j]);
                vset_union(dst_set, temp);
            }

            vset_copy(temp, dst_set);
            vset_minus(temp, int_levels[i]);
            vset_minus(dst_set, temp);
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
find_trace(int trace_end[][N], int end_count, int level, vset_t *levels)
{
    // Find initial state and open output file
    int             init_state[N];
    lts_file_t      trace_output;
    lts_type_t      ltstype = GBgetLTStype(model);

    GBgetInitialState(model, init_state);

    trace_output = lts_file_create(trc_output, ltstype, 1, lts_vset_template());
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
    RTprintTimer(info, timer, "constructing trace took");

    // Close output file
    lts_file_close(trace_output);
}

struct find_action_info {
    int  group;
    int *dst;
};

static void
find_action_cb(void* context, int* src)
{
    struct find_action_info* ctx = (struct find_action_info*)context;
    int group=ctx->group;
    int trace_end[2][N];

    for (int i = 0; i < N; i++) {
        trace_end[0][i] = src[i];
        trace_end[1][i] = src[i];
    }

    // Set dst of the last step of the trace to its proper value
    for (int i = 0; i < projs[group].len; i++)
        trace_end[0][projs[group].proj[i]] = ctx->dst[i];

    // src and dst may both be new, e.g. in case of chaining
    if (vset_member(levels[global_level - 1], src)) {
        Warning(debug, "source found at level %d", global_level - 1);
        find_trace(trace_end, 2, global_level, levels);
    } else {
        Warning(debug, "source not found at level %d", global_level - 1);
        find_trace(trace_end, 2, global_level + 1, levels);
    }

    Warning(info, "exiting now");
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

struct group_add_info {
    int    group;
    int   *src;
    int   *explored;
    vset_t set;
    vrel_t rel;
};

static void
group_add(void *context, transition_info_t *ti, int *dst)
{
    struct group_add_info *ctx = (struct group_add_info*)context;

    vrel_add(ctx->rel, ctx->src, dst);

    if (act_detect != NULL && ti->labels[act_label] == act_index) {
        Warning(info, "found action: %s", act_detect);

        if (trc_output == NULL){
            Warning(info, "exiting now");
            HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
        }

        struct find_action_info action_ctx;
        int group = ctx->group;

        action_ctx.group = group;
        action_ctx.dst = dst;
        vset_enum_match(ctx->set,projs[group].len, projs[group].proj,
                            ctx->src, find_action_cb, &action_ctx);
    }
}

static void
explore_cb(void *context, int *src)
{
    struct group_add_info *ctx = (struct group_add_info*)context;

    ctx->src = src;
    GBgetTransitionsShort(model, ctx->group, src, group_add, context);
    (*ctx->explored)++;

    if ((*ctx->explored) % 10000 == 0) {
        Warning(infoLong, "explored %d short vectors for group %d",
                    *ctx->explored, ctx->group);
    }
}

static inline void
expand_group_next(int group, vset_t set)
{
    struct group_add_info ctx;
    int explored = 0;

    ctx.group = group;
    ctx.set = set;
    ctx.rel = group_next[group];
    ctx.explored = &explored;
    vset_project(group_tmp[group], set);
    vset_zip(group_explored[group], group_tmp[group]);
    vset_enum(group_tmp[group], explore_cb, &ctx);
    vset_clear(group_tmp[group]);
}

static void
deadlock_check(vset_t deadlocks, bitvector_t *reach_groups)
{
    if (vset_is_empty(deadlocks))
        return;

    vset_t next_temp = vset_create(domain, -1, NULL);
    vset_t prev_temp = vset_create(domain, -1, NULL);

    Warning(debug, "Potential deadlocks found");

    for (int i = 0; i < nGrps; i++) {
        if (bitvector_is_set(reach_groups, i)) continue;
        expand_group_next(i, deadlocks);
        vset_next(next_temp, deadlocks, group_next[i]);
        vset_prev(prev_temp, next_temp, group_next[i]);
        vset_minus(deadlocks, prev_temp);
    }

    vset_destroy(next_temp);
    vset_destroy(prev_temp);

    if (vset_is_empty(deadlocks))
        return;

    Warning(info, "deadlock found");

    if (trc_output) {
        int dlk_state[1][N];

        vset_example(deadlocks, dlk_state[0]);
        find_trace(dlk_state, 1, global_level, levels);
    }

    Warning(info, "exiting now");
    HREabort(LTSMIN_EXIT_COUNTER_EXAMPLE);
}

static inline void
get_vset_size(vset_t set, long *node_count, double *elem_approximation,
                  char *elem_str, ssize_t str_len)
{
    bn_int_t elem_count;
    int      len;

    vset_count(set, node_count, &elem_count);
    len = bn_int2string(elem_str, str_len, &elem_count);

    if (len >= str_len)
        Abort("Error converting number to string");

    *elem_approximation = bn_int2double(&elem_count);

    bn_clear(&elem_count);
}

static inline void
get_vrel_size(vrel_t rel, long *node_count, double *elem_approximation,
                  char *elem_str, ssize_t str_len)
{
    bn_int_t elem_count;
    int      len;

    vrel_count(rel, node_count, &elem_count);
    len = bn_int2string(elem_str, str_len, &elem_count);

    if (len >= str_len)
        Abort("Error converting number to string");

    *elem_approximation = bn_int2double(&elem_count);

    bn_clear(&elem_count);
}

static void
stats_and_progress_report(vset_t current, vset_t visited, int level)
{
    long   n_count;
    char   elem_str[1024];
    double e_count;
    
    if (sat_strategy == NO_SAT || log_active(infoLong)) Print(infoShort, "level %d is finished",level);
    if (log_active(infoLong)) {
      if (current != NULL) {
        get_vset_size(current, &n_count, &e_count, elem_str, sizeof(elem_str));
        Print(infoLong, "level %d has %s (~%1.2e) states ( %ld nodes )",
	      level, elem_str, e_count, n_count);
        if (n_count > max_lev_count)
	  max_lev_count = n_count;
      }
      get_vset_size(visited, &n_count, &e_count, elem_str, sizeof(elem_str));
      Print(infoLong, "visited %d has %s (~%1.2e) states ( %ld nodes )",
	    level, elem_str, e_count, n_count);
      
      if (n_count > max_vis_count)
        max_vis_count = n_count;
      
      if (log_active(debug)) {
        Debug("transition caches ( grp nds elts ):");
	
        for (int i = 0; i < nGrps; i++) {
	  get_vrel_size(group_next[i], &n_count, &e_count, elem_str,
			sizeof(elem_str));
	  Debug("( %d %ld %s ) ", i, n_count, elem_str);
	  
	  if (n_count > max_trans_count)
	    max_trans_count = n_count;
        }
	
        Debug("\ngroup explored    ( grp nds elts ): ");
	
        for (int i = 0; i < nGrps; i++) {
	  get_vset_size(group_explored[i], &n_count, &e_count, elem_str,
			sizeof(elem_str));
	  Debug("( %d %ld %s ) ", i, n_count, elem_str);
	  
	  if (n_count > max_grp_count)
	    max_grp_count = n_count;
        }
      }
    }
}

static void
final_stat_reporting(vset_t visited, rt_timer_t timer)
{
    long   n_count;
    char   elem_str[1024];
    double e_count;

    RTprintTimer(info,timer, "reachability took");

    if (dlk_detect)
        Warning(info, "No deadlocks found");

    if (act_detect != NULL)
        Warning(info, "Action \"%s\" not found", act_detect);

    get_vset_size(visited, &n_count, &e_count, elem_str, sizeof(elem_str));
    Print(infoShort, "state space has %s (~%1.2e) states, %ld BDD nodes", elem_str, e_count,n_count);

    if (log_active(infoLong)) {
      if (max_lev_count == 0) {
        Print(infoLong, "( %ld final BDD nodes; %ld peak nodes )",
	      n_count, max_vis_count);
      } else {
        Print(infoLong, "( %ld final BDD nodes; %ld peak nodes; "
	      "%ld peak nodes per level )",
	      n_count, max_vis_count, max_lev_count);
      }
      
      if (log_active(debug)) {
	Debug("( peak transition cache: %ld nodes; peak group explored: "
	      "%ld nodes )\n", max_trans_count, max_grp_count);
      }
    }
}

#if defined(PBES)
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
        chunk c = GBchunkGet(model, var_type_no, var_index);
        if (c.len == 0) {
            Abort("lookup of %d failed", var_index);
        }
        char s[c.len + 1];
        for (unsigned int i = 0; i < c.len; i++) {
            s[i] = c.data[i];
        }
        s[c.len] = 0;
        long   n_count;
        char   elem_str[1024];
        double e_count;
        get_vset_size(u, &n_count, &e_count, elem_str, sizeof(elem_str));
        if (e_count > 0) Print(infoLong, "add_variable_subset: %s:  %s (~%1.2e) states", s, elem_str, e_count);
    }

    vset_union(dst, u);
    vset_destroy(u);
}
#endif

#if defined(PBES)
static inline void reduce_parity_game(vset_t next_level, vset_t visited, vset_t true_states, vset_t false_states)
{
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    int true_match[1] = {true_index}; // the variable
    int false_match[1] = {false_index}; // the variable
    long   n_count;
    char   elem_str[1024];
    double e_count;
    vset_t tmp = vset_create(domain, -1, NULL);

    vset_t next_level_prev = vset_create(domain, -1, NULL);
    for (int i = 0; i < nGrps; i++) {
        vset_prev(tmp, next_level, group_next[i]);
        vset_union(next_level_prev, tmp);
        vset_clear(tmp);
    }
    vset_intersect(next_level_prev, visited);

    vset_copy_match_proj(true_states, next_level, p_len, proj, variable_projection, true_match);
    if (log_active(infoLong))
    {
        //get_vset_size(true_states, &n_count, &e_count, elem_str, sizeof(elem_str));
        //Print(infoLong, "reduce_parity_game: %s (~%1.2e) TRUE states", elem_str, e_count);
    }
    vset_t true_prev = vset_create(domain, -1, NULL);
    for (int i = 0; i < nGrps; i++) {
        vset_prev(tmp, true_states, group_next[i]);
        vset_union(true_prev, tmp);
        vset_clear(tmp);
    }
    vset_intersect(true_prev, next_level_prev);
    if (log_active(infoLong))
    {
        //get_vset_size(true_prev, &n_count, &e_count, elem_str, sizeof(elem_str));
        //Print(infoLong, "reduce_parity_game: %s (~%1.2e) TRUE prev states", elem_str, e_count);
    }

    vset_copy_match_proj(false_states, next_level, p_len, proj, variable_projection, false_match);
    if (log_active(infoLong))
    {
        //get_vset_size(false_states, &n_count, &e_count, elem_str, sizeof(elem_str));
        //Print(infoLong, "reduce_parity_game: %s (~%1.2e) FALSE states", elem_str, e_count);
    }
    vset_t false_prev = vset_create(domain, -1, NULL);
    for (int i = 0; i < nGrps; i++) {
        vset_prev(tmp, false_states, group_next[i]);
        vset_union(false_prev, tmp);
        vset_clear(tmp);
    }
    vset_intersect(false_prev, next_level_prev);
    if (log_active(infoLong))
    {
        //get_vset_size(false_prev, &n_count, &e_count, elem_str, sizeof(elem_str));
        //Print(infoLong, "reduce_parity_game: %s (~%1.2e) FALSE prev states", elem_str, e_count);
    }

    // compute which states in visited are AND/OR states
    // computes AND states with FALSE successor, OR states with TRUE successor.
    vset_t v_player[2];
    for(int p=0; p<2; p++) {
        v_player[p] = vset_create(domain, -1, NULL);
    }
    for(size_t i = 0; i < num_vars; i++)
    {
        if (i != false_index && i != true_index)
        {
            //Print(infoLong, "Adding nodes for var %d (player %d).", i, player[i]);
            add_variable_subset(v_player[player[i]], (player[i] == 1) ? false_prev : true_prev, domain, i); // AND is player 1, OR is player 0
        }
    }

    // compute successors of true_prev are in next_level that are not 'true' states.
    //       These can be discarded.
    // P == v_player[0] \setunion v_player[1]
    // R == visited
    // now compute next(P) - next(R-P) - {true, false}

    vset_t P_next = vset_create(domain, -1, NULL);
    for(int p=0; p<2; p++) {
        get_vset_size(v_player[p], &n_count, &e_count, elem_str, sizeof(elem_str));
        //Print(infoLong, "reduce_parity_game: %s (~%1.2e) %s states with a successor %s.", elem_str, e_count, (p==1)?"AND":"OR", (p==1)?"FALSE":"TRUE");
        for (int i = 0; i < nGrps; i++) {
            vset_next(tmp, v_player[p], group_next[i]);
            vset_union(P_next, tmp);
            vset_clear(tmp);
        }
    }
    vset_intersect(P_next, next_level);

    if (!vset_is_empty(P_next))
    {
        vset_t R_minus_P = vset_create(domain, -1, NULL);
        vset_copy(R_minus_P, visited);
        for(int p=0; p<2; p++) {
            vset_minus(R_minus_P, v_player[p]);
        }

        vset_t R_minus_P_next = vset_create(domain, -1, NULL);
        for (int i = 0; i < nGrps; i++) {
            vset_next(tmp, R_minus_P, group_next[i]);
            vset_union(R_minus_P_next, tmp);
            vset_clear(tmp);
        }

        if (log_active(infoLong))
        {
            get_vset_size(P_next, &n_count, &e_count, elem_str, sizeof(elem_str));
            Print(infoLong, "reduce_parity_game: %s (~%1.2e) P_next states.", elem_str, e_count);
            get_vset_size(R_minus_P_next, &n_count, &e_count, elem_str, sizeof(elem_str));
            Print(infoLong, "reduce_parity_game: %s (~%1.2e) R_minus_P_next states.", elem_str, e_count);
        }
        vset_minus(P_next, R_minus_P_next);
        if (log_active(infoLong))
        {
            get_vset_size(P_next, &n_count, &e_count, elem_str, sizeof(elem_str));
            Print(infoLong, "reduce_parity_game: %s (~%1.2e) [P_next - R_minus_P_next]", elem_str, e_count);
        }
        vset_minus(P_next, true_states);
        vset_minus(P_next, false_states);
        if (log_active(infoShort))
        {
            get_vset_size(P_next, &n_count, &e_count, elem_str, sizeof(elem_str));
            Print(infoShort, "reduce_parity_game: %s (~%1.2e) states can be skipped.", elem_str, e_count);
        }

        vset_minus(next_level, P_next);

        vset_destroy(R_minus_P);
        vset_destroy(R_minus_P_next);
    }

    vset_destroy(P_next);
    for(int p=0; p<2; p++) {
        vset_destroy(v_player[p]);
    }
    vset_destroy(true_prev);
    vset_destroy(false_prev);
    vset_destroy(next_level_prev);
    vset_destroy(tmp);

    vset_clear(true_states);
    vset_clear(false_states);
}
#endif

static void
reach_bfs_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                   long *eg_count, long *next_count)
{
    int level = 0;
    vset_t current_level = vset_create(domain, -1, NULL);
    vset_t next_level = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    vset_copy(current_level, visited);
    if (save_sat_levels) vset_minus(current_level, visited_old);

    while (!vset_is_empty(current_level)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(current_level, visited, level);
        level++;
        for (int i = 0; i < nGrps; i++){
            if (!bitvector_is_set(reach_groups, i)) continue;
            expand_group_next(i, current_level);
            (*eg_count)++;
        }
        if (dlk_detect) vset_copy(deadlocks, current_level);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups,i)) continue;
            (*next_count)++;
            vset_next(temp, current_level, group_next[i]);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i]);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_minus(temp, visited);
            vset_union(next_level, temp);
            vset_clear(temp);
        }
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);

#if defined(PBES)
        if (pgreduce_flag) reduce_parity_game(next_level, visited, true_states, false_states);
#endif

        vset_union(visited, next_level);
        vset_copy(current_level, next_level);
        vset_clear(next_level);
        vset_reorder(domain);
    }

    vset_destroy(current_level);
    vset_destroy(next_level);
    vset_destroy(temp);
    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

static void
reach_bfs(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
              long *eg_count, long *next_count)
{
    (void)visited_old;

    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    while (!vset_equal(visited, old_vis)) {
        if (trc_output != NULL) save_level(visited);
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups,i)) continue;
            expand_group_next(i, visited);
            (*eg_count)++;
        }
        if (dlk_detect) vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups,i)) continue;
            (*next_count)++;
            vset_next(temp, old_vis, group_next[i]);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i]);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_union(visited, temp);
        }
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
        vset_clear(temp);
        vset_reorder(domain);
    }

    vset_destroy(old_vis);
    vset_destroy(temp);
    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

static void
reach_chain_prev(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                     long *eg_count, long *next_count)
{
    int level = 0;
    vset_t new_states = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    vset_copy(new_states, visited);
    if (save_sat_levels) vset_minus(new_states, visited_old);

    while (!vset_is_empty(new_states)) {
        if (trc_output != NULL) save_level(visited);
        stats_and_progress_report(new_states, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, new_states);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            expand_group_next(i, new_states);
            (*eg_count)++;
            (*next_count)++;
            vset_next(temp, new_states, group_next[i]);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i]);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_minus(temp, visited);
            vset_union(new_states, temp);
            vset_clear(temp);
        }
        // no deadlocks in old new_states
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);

#if defined(PBES)
        if (pgreduce_flag) reduce_parity_game(new_states, visited, true_states, false_states);
#endif

        vset_zip(visited, new_states);
        vset_reorder(domain);
    }

    vset_destroy(new_states);
    vset_destroy(temp);
    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

static void
reach_chain(vset_t visited, vset_t visited_old, bitvector_t *reach_groups,
                long *eg_count, long *next_count)
{
    (void)visited_old;

    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t temp = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    while (!vset_equal(visited, old_vis)) {
        if (trc_output != NULL) save_level(visited);
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        if (dlk_detect) vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups, i)) continue;
            expand_group_next(i, visited);
            (*eg_count)++;
            (*next_count)++;
            vset_next(temp, visited, group_next[i]);
            vset_union(visited, temp);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i]);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
        }
        // no deadlocks in old_vis
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
        vset_reorder(domain);
    }

    vset_destroy(old_vis);
    vset_destroy(temp);
    if (dlk_detect) {
        vset_destroy(deadlocks);
        vset_destroy(dlk_temp);
    }
}

static void
reach_no_sat(reach_proc_t reach_proc, vset_t visited, bitvector_t *reach_groups,
                 long *eg_count, long *next_count)
{
    vset_t old_visited = save_sat_levels?vset_create(domain, -1, NULL):NULL;

    reach_proc(visited, old_visited, reach_groups, eg_count, next_count);

    if (save_sat_levels) vset_destroy(old_visited);
}

static void
reach_sat_fix(reach_proc_t reach_proc, vset_t visited,
                 bitvector_t *reach_groups, long *eg_count, long *next_count)
{
    (void) reach_proc;
    int level = 0;
    vset_t old_vis = vset_create(domain, -1, NULL);
    vset_t deadlocks = dlk_detect?vset_create(domain, -1, NULL):NULL;
    vset_t dlk_temp = dlk_detect?vset_create(domain, -1, NULL):NULL;

    while (!vset_equal(visited, old_vis)) {
        if (trc_output != NULL) save_level(visited);
        vset_copy(old_vis, visited);
        stats_and_progress_report(NULL, visited, level);
        level++;
        for(int i = 0; i < nGrps; i++){
            if (!bitvector_is_set(reach_groups, i)) continue;
            expand_group_next(i, visited);
            (*eg_count)++;
        }
        if (dlk_detect) vset_copy(deadlocks, visited);
        vset_least_fixpoint(visited, visited, group_next, nGrps);
        (*next_count)++;
        if (dlk_detect) {
            for (int i = 0; i < nGrps; i++) {
                vset_prev(dlk_temp, visited, group_next[i]);
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
        bitvector_t row;

        bitvector_create(&row, N);
        dm_bitvector_row(&row, GBgetDMInfo(model), i);
        bitvector_union(&level_matrix[level[i]], &row);
        bitvector_free(&row);
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
                   bitvector_t *reach_groups, long *eg_count, long *next_count)
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

        Warning(info, "Saturating level: %d", k);
        vset_copy(old_vis, visited);
        reach_proc(visited, prev_vis[k], &groups[k], eg_count, next_count);
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
                   bitvector_t *reach_groups, long *eg_count, long *next_count)
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
            Warning(info, "Saturating level: %d", k);
            reach_proc(visited, prev_vis[k], &groups[k], eg_count, next_count);
            if (save_sat_levels) vset_copy(prev_vis[k], visited);
        }
    }

    for (int k = 0; k < max_sat_levels; k++)
        bitvector_free(&groups[k]);

    vset_destroy(old_vis);
    if (save_sat_levels)
        for (int i = 0; i < max_sat_levels; i++) vset_destroy(prev_vis[i]);
}

struct expand_info {
    int group;
    vset_t group_explored;
    long *eg_count;
};

static inline void
expand_group_next_projected(vrel_t rel, vset_t set, void *context)
{
    struct expand_info *expand_ctx = (struct expand_info*)context;
    struct group_add_info group_ctx;
    int group = expand_ctx->group;
    vset_t group_explored = expand_ctx->group_explored;
    int explored = 0;

    group_ctx.group = group;
    group_ctx.set = NULL;
    group_ctx.rel = rel;
    group_ctx.explored = &explored;
    (*expand_ctx->eg_count)++;
    vset_zip(group_explored, set);
    vset_enum(set, explore_cb, &group_ctx);
}

static void
reach_sat(reach_proc_t reach_proc, vset_t visited,
          bitvector_t *reach_groups, long *eg_count, long *next_count)
{
    (void) reach_proc;

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
    (*next_count)++;
    stats_and_progress_report(NULL, visited, 1);

    if (dlk_detect) {
        vset_t deadlocks = vset_create(domain, -1, NULL);
        vset_t dlk_temp = vset_create(domain, -1, NULL);
        vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            vset_prev(dlk_temp, visited, group_next[i]);
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
etf_edge(void *context, transition_info_t *ti, int *dst)
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

    sLbls = dm_nrows(sl_info);

    if (dm_nrows(sl_info) != lts_type_get_state_label_count(ltstype))
        Warning(error, "State label count mismatch!");

    for (int i = 0; i < sLbls; i++){
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

        int values = GBchunkCount(model,i);

        for (int j = 0; j < values; j++) {
            chunk c    = GBchunkGet(model, i, j);
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

    bitvector_create(&reach_groups, nGrps);
    bitvector_invert(&reach_groups);
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count);
    bitvector_free(&reach_groups);
    Warning(info, "Exploration took %ld group checks and %ld next state calls",
                eg_count, next_count);
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
        dm_bitvector_row(&row_found, GBgetDMInfoRead(model), i);

        for(int j = 0; j < nGrps; j++) {
            if (bitvector_is_set(found_groups, j)) continue;
            dm_bitvector_row(&row_new, GBgetDMInfoWrite(model), j);
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

    for (int i = 0; i < nGrps; i++){
        if (GBtransitionInGroup(model, &act_index, i)) {
            Warning(info, "Found \"%s\" potentially in group %d", act_detect,i);
            group_order[group_total] = i;
            group_total++;
            bitvector_set(&found_groups, i);
        }
    }

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

    total_count = establish_group_order(group_order, &initial_count);

    if (total_count == 0)
        Abort("Action %s does not occur", act_detect);

    bitvector_create(&reach_groups, nGrps);
    for (int i = 0; i < initial_count; i++)
        bitvector_set(&reach_groups, group_order[i]);

    long eg_count = 0;
    long next_count = 0;

    // Assumption: reach_proc does not return in case action is found
    Warning(info, "Searching for action using initial groups");
    sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count);

    for (int i = initial_count; i < total_count; i++) {
        Warning(info, "Extending action search with group %d", group_order[i]);
        bitvector_set(&reach_groups, group_order[i]);
        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count);
    }

    if (etf_output != NULL || dlk_detect) {
        Warning(info, "Continuing for etf output or deadlock detection");

        for(int i = 0; i < nGrps; i++)
            bitvector_set(&reach_groups, i);

        sat_proc(reach_proc, visited, &reach_groups, &eg_count, &next_count);
    }

    Warning(info, "Exploration took %ld group checks and %ld next state calls",
            eg_count, next_count);
    bitvector_free(&reach_groups);
}

static void
init_model(char *file)
{
    Warning(info, "opening %s", file);
    model = GBcreateBase();
    GBsetChunkMethods(model,HREgreyboxNewmap,HREglobal(),HREgreyboxI2C,HREgreyboxC2I,HREgreyboxCount);

    GBloadFile(model, file, &model);

    if (log_active(infoLong)) {
        fprintf(stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined(stderr, model);
    }

    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = lts_type_get_state_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));
    max_sat_levels = (N / sat_granularity) + 1;
    Warning(info, "state vector length is %d; there are %d groups", N, nGrps);
}

static void
init_domain(vset_implementation_t impl, vset_t *visited)
{
    domain = vdom_create_domain(N, impl);
    *visited = vset_create(domain, -1, NULL);

    group_next     = (vrel_t*)RTmalloc(nGrps * sizeof(vrel_t));
    group_explored = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    group_tmp      = (vset_t*)RTmalloc(nGrps * sizeof(vset_t));
    projs          = (proj_info*)RTmalloc(nGrps * sizeof(proj_info));

    for(int i = 0; i < nGrps; i++) {
        projs[i].len  = dm_ones_in_row(GBgetDMInfo(model), i);
        projs[i].proj = (int*)RTmalloc(projs[i].len * sizeof(int));

        // temporary replacement for e_info->indices[i]
        for(int j = 0, k = 0; j < dm_ncols(GBgetDMInfo(model)); j++) {
            if (dm_is_set(GBgetDMInfo(model), i, j))
                projs[i].proj[k++] = j;
        }

        group_next[i]     = vrel_create(domain,projs[i].len,projs[i].proj);
        group_explored[i] = vset_create(domain,projs[i].len,projs[i].proj);
        group_tmp[i]      = vset_create(domain,projs[i].len,projs[i].proj);
    }
}

static void
init_action()
{
    // table number of first edge label
    act_label = 0;
    if (lts_type_get_edge_label_count(ltstype) == 0 ||
            strncmp(lts_type_get_edge_label_name(ltstype, act_label),
                    "action", 6) != 0)
        Abort("No edge label 'action...' for action detection");
    int typeno = lts_type_get_edge_label_typeno(ltstype, act_label);
    chunk c = chunk_str(act_detect);
    act_index = GBchunkPut(model, typeno, c);
    Warning(info, "Detecting action \"%s\"", act_detect);
}

static vset_t
get_svar_eq_int_set (int state_idx, int state_match, vset_t visited)
{
  vset_t result=vset_create(domain, -1, NULL);
  int proj[1] = {state_idx};
  int match[1] = {state_match};
  vset_copy_match(result, visited, 1, proj, match);

  return result;
}

static array_manager_t mu_var_man = NULL;
static vset_t* mu_var = NULL;

/* Naive textbook mu-calculus algorithm
 * Taken from:
 * Model Checking and the mu-calculus, E. Allen Emerson
 * DIMACS Series in Discrete Mathematics, 1997 - Citeseer
 */
static vset_t
mu_compute (ltsmin_expr_t mu_expr, vset_t visited)
{
    vset_t result = NULL;
    switch(mu_expr->token) {
    case MU_TRUE:
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        return result;
    case MU_FALSE:
        return vset_create(domain, -1, NULL);
    case MU_EQ: { // svar == int
        /* Currently MU_EQ works only in the context of an SVAR/INTEGER pair */
        if (!mu_expr->arg1->token == MU_SVAR)
            Abort("Expecting == with state variable on the left side!\n");
        if (!mu_expr->arg1->token == MU_NUM)
            Abort("Expecting == with int on the right side!\n");
        result = get_svar_eq_int_set(mu_expr->arg1->idx, mu_expr->arg2->idx, visited);
    } break;
    case MU_OR: { // OR
        result = mu_compute(mu_expr->arg1, visited);
        vset_t mc = mu_compute(mu_expr->arg2, visited);
        vset_union(result, mc);
        vset_destroy(mc);
    } break;
    case MU_AND: { // AND
        result = mu_compute(mu_expr->arg1, visited);
        vset_t mc = mu_compute(mu_expr->arg2, visited);
        vset_intersect(result, mc);
        vset_destroy(mc);
    } break;
    case MU_NOT: { // NEGATION
        result = vset_create(domain, -1, NULL);
        vset_copy(result, visited);
        vset_t mc = mu_compute(mu_expr->arg1, visited);
        vset_minus(result, mc);
        vset_destroy(mc);
    } break;
    case MU_NEXT: // X
        Abort("unhandled MU_NEXT");
        break;
    case MU_EXIST: { // E
        if (mu_expr->arg1->token == MU_NEXT) {
            vset_t temp = vset_create(domain, -1, NULL);
            result = vset_create(domain, -1, NULL);
            vset_t g = mu_compute(mu_expr->arg1->arg1, visited);

            for(int i=0;i<nGrps;i++){
                vset_prev(temp,g,group_next[i]);
                vset_union(result,temp);
                vset_clear(temp);
            }
            // destroy..
            vset_destroy(temp);
            // this is somewhat strange, but it appears that vset_prev generates
            // states that are never visited before? can this happen or is this a bug?
            // when?
            // in order to prevent this, intersect with visited
            vset_intersect(result, visited);
        } else {
            Abort("invalid operator following MU_EXIST, expecting MU_NEXT");
        }
    } break;
    case MU_NUM:
        Abort("unhandled MU_NUM");
        break;
    case MU_SVAR:
        Abort("unhandled MU_SVAR");
        break;
    case MU_EVAR:
        Abort("unhandled MU_EVAR");
        break;
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
            vset_t phi = mu_compute(mu_expr->arg1->arg1, visited);
            vset_minus(notphi, phi);
            vset_destroy(phi);

            vset_t temp = vset_create(domain, -1, NULL);
            vset_t prev = vset_create(domain, -1, NULL);

            // EX !phi
            for(int i=0;i<nGrps;i++){
                vset_prev(temp,notphi,group_next[i]);
                vset_union(prev,temp);
                vset_clear(temp);
            }
            vset_destroy(temp);
            // intersect: see EX
            vset_intersect(prev, visited);

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
                tmp = mu_compute(mu_expr->arg1, visited);
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
                tmp = mu_compute(mu_expr->arg1, visited);
            } while (!vset_equal(mu_var[mu_expr->idx], tmp));
            vset_destroy(tmp);
            // new var reference
            mu_var[mu_expr->idx] = old;
        }
        break;
    default:
        Abort("encountered unhandled mu operator");
    }
    return result;
}

#if defined(PBES)
void init_pbes()
{
    num_vars = GBchunkCount(model, var_type_no); // number of propositional variables
    priority = RTmalloc(num_vars * sizeof(int)); // priority of variables
    player = RTmalloc(num_vars * sizeof(int)); // player of variables
    for(size_t i=0; i<num_vars; i++)
    {
        /*
        if (log_active(infoLong))
        {
            chunk c = GBchunkGet(model, var_type_no, i);
            if (c.len == 0) {
                Abort("lookup of %d failed", i);
            }
            char s[c.len + 1];
            for (unsigned int i = 0; i < c.len; i++) {
                s[i] = c.data[i];
            }
            s[c.len] = 0;
            Print(infoLong, "Variable %d: %s", i, s);
        }
        */
        lts_type_t type = GBgetLTStype(model);
        int state_length = lts_type_get_state_length(type);
        // create dummy state with variable i:
        int state[state_length];
        for(int j=0; j < state_length; j++)
        {
            state[j] = 0;
        }
        state[var_pos] = i;
        int label = GBgetStateLabelLong(model, 0, state); // priority
        priority[i] = label;
        if (label < min_priority)
        {
            min_priority = label;
        }
        if (label > max_priority)
        {
            max_priority = label;
        }
        //Print(infoLong, "  label %d (priority): %d", 0, label);
        label = GBgetStateLabelLong(model, 1, state); // player
        player[i] = label;
        //Print(infoLong, "  label %d (player): %d", 1, label);
    }
}
#endif

#if defined(PBES)
#if defined(HAVE_LIBSPG)
/**
 * \brief Creates a symbolic parity game
 */
parity_game* compute_symbolic_parity_game(vset_t visited, int* src)
{
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
        //Print(infoLong, "Adding nodes for var %d (player %d).", i, player[i]);
        add_variable_subset(g->v_player[player[i]], g->v, g->domain, i);
        // priorities
        add_variable_subset(g->v_priority[priority[i]], g->v, g->domain, i);
    }
    if (log_active(infoShort))
    {
        for(int p = 0; p < 2; p++)
        {
            long   n_count;
            bn_int_t elem_count;
            size_t size = 20;
            char s[size];
            vset_count(g->v_player[p], &n_count, &elem_count);
            bn_int2string(s, size, &elem_count);
            bn_clear(&elem_count);
            Print(infoShort, "player %d: %d nodes, %s elements.", p, n_count, s);
        }
        for(int p = min_priority; p <= max_priority; p++)
        {
            long   n_count;
            bn_int_t elem_count;
            size_t size = 20;
            char s[size];
            vset_count(g->v_priority[p], &n_count, &elem_count);
            bn_int2string(s, size, &elem_count);
            bn_clear(&elem_count);
            Print(infoShort, "priority %d: %d nodes, %s elements.", p, n_count, s);
        }
    }
    for(int i = 0; i < nGrps; i++)
    {
        g->e[i] = group_next[i];
    }
    return g;
}
#endif
#endif



int
main (int argc, char *argv[])
{
    char *files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Perform a symbolic reachability analysis of <model>\n"
                  "The optional output of this analysis is an ETF "
                      "representation of the input\n\nOptions");
    lts_lib_setup();
    HREinitStart(&argc,&argv,1,2,files,"<model> [<etf>]");

    vset_implementation_t vset_impl = VSET_IMPL_AUTOSELECT;
    vset_t visited;

    init_model(files[0]);
    init_domain(vset_impl, &visited);
    if (act_detect != NULL) init_action();
    if (inv_detect) Abort("Invariant violation detection is not implemented.");
    if (no_exit) Abort("Error counting (--no-exit) is not implemented.");

    sat_proc_t sat_proc = NULL;
    reach_proc_t reach_proc = NULL;
    guided_proc_t guided_proc = NULL;

    switch (strategy) {
    case BFS_P:
        reach_proc = reach_bfs_prev;
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

    // temporal logics
    if (mu_formula) {
        mu_expr = parse_file(mu_formula, mu_parse_file, model);
#if 0
        char buf[1024];
        ltsmin_expr_print_mu(mu_expr, buf);
        printf("computing: %s\n",buf);
#endif
    } else if (ctl_formula) {
        ltsmin_expr_t ctl = parse_file(ctl_formula, ctl_parse_file, model);
        mu_expr = ctl_star_to_mu(ctl);
        mu_formula = ctl_formula;
    }

    int src[N];

    GBgetInitialState(model, src);
    vset_add(visited, src);
    Warning(info, "got initial state");

    if (mu_expr) { // run a small test to check correctness of mu formula
        vset_t x = mu_compute(mu_expr, visited);
        vset_destroy(x);
    }
#if defined(PBES)
    lts_type_t type = GBgetLTStype(model);
    var_pos = 0;
    var_type_no = 0;
    for(int i=0; i<N; i++)
    {
        //Printf(infoLong, "%d: %s (%d [%s])\n", i, lts_type_get_state_name(type, i), lts_type_get_state_typeno(type, i), lts_type_get_state_type(type, i));
        char* str1 = "string";
        size_t strlen1 = strlen(str1);
        char* str2 = lts_type_get_state_type(type, i);
        size_t strlen2 = strlen(str2);
        if (strlen1==strlen2 && strncmp(str1, str2, strlen1)==0)
        {
            var_pos = i;
            var_type_no = lts_type_get_state_typeno(type, i);
            true_index = GBchunkPut(model, var_type_no, chunk_str("true"));
            false_index = GBchunkPut(model, var_type_no, chunk_str("false"));
        }
    }
    int p_len = 1;
    int proj[1] = {var_pos}; // position 0 encodes the variable
    variable_projection = vproj_create(domain, p_len, proj);
#endif

#if defined(PBES)
    init_pbes();
    true_states = vset_create(domain, -1, NULL);
    false_states = vset_create(domain, -1, NULL);
#endif

    rt_timer_t timer = RTcreateTimer();

    RTstartTimer(timer);
    guided_proc(sat_proc, reach_proc, visited, files[1]);
    RTstopTimer(timer);

    if (mu_expr) {
        // setup var manager
        mu_var_man = create_manager(65535);
        ADD_ARRAY(mu_var_man, mu_var, vset_t);

        vset_t x = mu_compute(mu_expr, visited);

        if (x) {
            long   n_count;
            char   elem_str[1024];
            double e_count;

            get_vset_size(x, &n_count, &e_count, elem_str, sizeof(elem_str));
            Warning(info, "mu formula holds for %s (~%1.2e) states\n",
                        elem_str, e_count);
            Warning(info, " the initial state is %sin the set\n",
                        vset_member(x, src) ? "" : "not ");
            vset_destroy(x);
        }
    }

    final_stat_reporting(visited, timer);

    if (files[1] != NULL)
        do_output(files[1], visited);

#if defined(PBES)
    vset_destroy(true_states);
    vset_destroy(false_states);

#ifdef HAVE_LIBSPG
    if (pg_output || pgsolve_flag) {
        RTresetTimer(timer);
        RTstartTimer(timer);
        parity_game * g = compute_symbolic_parity_game(visited, src);
        RTstopTimer(timer);
        RTprintTimer(info, timer, "computing symbolic parity game took");
        if (pg_output) {
            Warning(info,"Writing symbolic parity game to %s",pg_output);
            FILE *f = fopen(pg_output, "w");
            spg_save(f, g);
            fclose(f);
        }
        if (pgsolve_flag)
        {
            spgsolver_options* spg_options = spg_get_solver_options();
            rt_timer_t pgsolve_timer = RTcreateTimer();
            RTstartTimer(pgsolve_timer);
            bool result = spg_solve(g, spg_options);
            Warning(info, "");
            Warning(info, "The result is: %s", result ? "true":"false");
            RTstopTimer(pgsolve_timer);
            Warning(info, "");
            RTprintTimer(info, timer, "generation took");
            RTprintTimer(info, pgsolve_timer, "solving took");
        }
        else
        {
            spg_destroy(g);
        }
    }
#endif
    if (player != 0)
    {
        RTfree(player);
        RTfree(priority);
    }
#endif

    HREexit (LTSMIN_EXIT_SUCCESS);
}
