#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <dm/dm.h>
#include <dynamic-array.h>
#include <greybox.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <ltsmin-grammar.h>
#include <ltsmin-syntax.h>
#include <ltsmin-tl.h>
#include <runtime.h>
#include <scctimer.h>
#include <spec-greybox.h>
#include <stringindex.h>
#include <vector_set.h>

#define diagnostic(...) {\
    if (RTverbosity >= 2)\
        fprintf(stderr, __VA_ARGS__);\
}

static ltsmin_expr_t mu_expr = NULL;
static char* ctl_formula = NULL;
static char* mu_formula  = NULL;

static char* trc_output = NULL;
static int   dlk_detect = 0;
static char* act_detect = NULL;
static int   act_detect_table;
static int   act_detect_index;
static int   sat_granularity = 10;
static int   save_sat_levels = 0;

static enum { BFS_P , BFS , CHAIN_P, CHAIN } strategy = BFS_P;

static char* order = "bfs-prev";
static const si_map_entry ORDER[] = {
    {"bfs-prev", BFS_P},
    {"bfs", BFS},
    {"chain-prev", CHAIN_P},
    {"chain", CHAIN},
    {NULL, 0}
};

static enum { NO_SAT, SAT_LIKE, SAT_LOOP, SAT_FIX, SAT } sat_strategy = NO_SAT;

static char* saturation = "none";
static const si_map_entry SATURATION[] = {
    {"none", NO_SAT},
    {"sat-like", SAT_LIKE},
    {"sat-loop", SAT_LOOP},
    {"sat-fix", SAT_FIX},
    {"sat", SAT},
    {NULL, 0}
};

static void
reach_popt(poptContext con, enum poptCallbackReason reason,
               const struct poptOption * opt, const char * arg, void * data)
{
    (void)con; (void)opt; (void)arg; (void)data;

    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        Fatal(1, error, "unexpected call to vset_popt");
    case POPT_CALLBACK_REASON_POST: {
        int res;

        res = linear_search(ORDER, order);
        if (res < 0) {
            Warning(error, "unknown exploration order %s", order);
            RTexitUsage(EXIT_FAILURE);
        } else {
            Warning(info, "Exploration order is %s", order);
        }
        strategy = res;

        res = linear_search(SATURATION, saturation);
        if (res < 0) {
            Warning(error, "unknown saturation strategy %s", saturation);
            RTexitUsage(EXIT_FAILURE);
        } else {
            Warning(info, "Saturation strategy is %s", saturation);
        }
        sat_strategy = res;

        if (trc_output != NULL && !dlk_detect && act_detect == NULL)
            Warning(info, "Ignoring trace output");

        return;
    }
    case POPT_CALLBACK_REASON_OPTION:
        Fatal(1, error, "unexpected call to reach_popt");
    }
}

static  struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)reach_popt , 0 , NULL , NULL },
    { "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "set the exploration strategy to a specific order" , "<bfs-prev|bfs|chain-prev|chain>" },
    { "saturation" , 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &saturation , 0 , "select the saturation strategy" , "<none|sat-like|sat-loop|sat-fix|sat>" },
    { "sat-granularity" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &sat_granularity , 0 , "set saturation granularity","<number>" },
    { "save-sat-levels", 0, POPT_ARG_VAL, &save_sat_levels, 1, "save previous states seen at saturation levels", NULL },
    { "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
    { "action" , 0 , POPT_ARG_STRING , &act_detect , 0 , "detect action" , "<action>" },
    { "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts-file>.gcf" },
    { "mu" , 0 , POPT_ARG_STRING , &mu_formula , 0 , "file with a mu formula" , "<mu-file>.mu" },
    { "ctl*" , 0 , POPT_ARG_STRING , &ctl_formula , 0 , "file with a ctl* formula" , "<ctl*-file>.ctl" },
    SPEC_POPT_OPTIONS,
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options",NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
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
                              vset_t visited);

static void *
new_string_index (void *context)
{
    (void)context;
    return SIcreate ();
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
write_trace_state(lts_enum_cb_t trace_handle, int src_no, int *state) {
  int labels[sLbls];

  Warning(debug, "dumping state %d", src_no);

  if (sLbls != 0)
      GBgetStateLabelsAll(model,state,labels);

  enum_state(trace_handle, 0, state,labels);
}

struct write_trace_step_s {
    lts_enum_cb_t trace_handle;
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
    enum_seg_seg(ctx->trace_handle, 0, ctx->src_no, 0, ctx->dst_no, ti->labels);
}

static void
write_trace_step(lts_enum_cb_t trace_handle, int src_no, int *src,
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

    if (ctx.found ==0)
        Fatal(1, error, "no matching transition found");
}

static void
write_trace(lts_enum_cb_t trace_handle, int **states, int total_states)
{
    // output starting from initial state, which is in states[total_states-1]

    for(int i = total_states - 1; i > 0; i--) {
        int current_step = total_states-i-1;

        write_trace_state(trace_handle, current_step, states[i]);
        write_trace_step(trace_handle, current_step, states[i],
                             current_step + 1, states[i-1]);
    }

    write_trace_state(trace_handle, total_states - 1, states[0]);
}

static void
find_trace_to(int trace_end[][N], int end_count, int level, vset_t *levels,
                  lts_enum_cb_t trace_handle)
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

        vset_add(int_levels[0], states[current_state-1]);

        // search backwards from states[current_state-1] to prev_level
        do {
            int_level++;

            if(int_level == max_int_level) {
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

        // find the states that give us a trace to states[current_state-1]
        for(int i = int_level - 1; i > 0; i--) {
            vset_add(src_set, states[current_state+i]);

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
    int           init_state[N];
    lts_output_t  trace_output;
    lts_enum_cb_t trace_handle;

    GBgetInitialState(model, init_state);
    trace_output = lts_output_open(trc_output, model, 1, 0, 1, "vsi", NULL);
    lts_output_set_root_vec(trace_output, (uint32_t*)init_state);
    lts_output_set_root_idx(trace_output, 0, 0);
    trace_handle = lts_output_begin(trace_output, 0, 0, 0);

    // Generate trace
    mytimer_t  timer = SCCcreateTimer();

    SCCstartTimer(timer);
    find_trace_to(trace_end, end_count, level, levels, trace_handle);
    SCCstopTimer(timer);
    SCCreportTimer(timer, "constructing trace took");

    // Close output file
    lts_output_end(trace_output, trace_handle);
    lts_output_close(&trace_output);
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

    Fatal(1, info, "exiting now");
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

    if (act_detect != NULL && ti->labels[0] == act_detect_index) {
        diagnostic("\n");
        Warning(info, "found action: %s", act_detect);

        if (trc_output == NULL)
            Fatal(1, info, "exiting now");

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

    if ((*ctx->explored) % 1000 == 0 && RTverbosity >= 2) {
        Warning(info, "explored %d short vectors for group %d",
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

    Fatal(1,info,"exiting now");
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
        Fatal(1, error, "Error converting number to string");

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
        Fatal(1, error, "Error converting number to string");

    *elem_approximation = bn_int2double(&elem_count);

    bn_clear(&elem_count);
}

static void
stats_and_progress_report(vset_t current, vset_t visited, int level)
{
    long   n_count;
    char   elem_str[1024];
    double e_count;

    if (current != NULL) {
        get_vset_size(current, &n_count, &e_count, elem_str, sizeof(elem_str));
        Warning(info, "level %d has %s (~%1.2e) states ( %ld nodes )",
                    level, elem_str, e_count, n_count);

        if (n_count > max_lev_count)
            max_lev_count = n_count;
    }

    get_vset_size(visited, &n_count, &e_count, elem_str, sizeof(elem_str));
    Warning(info, "visited %d has %s (~%1.2e) states ( %ld nodes )",
                level, elem_str, e_count, n_count);

    if (n_count > max_vis_count)
        max_vis_count = n_count;

    if (RTverbosity >= 2) {
        fprintf(stderr, "transition caches ( grp nds elts ): ");

        for (int i = 0; i < nGrps; i++) {
            get_vrel_size(group_next[i], &n_count, &e_count, elem_str,
                              sizeof(elem_str));
            fprintf(stderr, "( %d %ld %s ) ", i, n_count, elem_str);

            if (n_count > max_trans_count)
                max_trans_count = n_count;
        }

        fprintf(stderr,"\ngroup explored    ( grp nds elts ): ");

        for (int i = 0; i < nGrps; i++) {
            get_vset_size(group_explored[i], &n_count, &e_count, elem_str,
                              sizeof(elem_str));
            fprintf(stderr, "( %d %ld %s ) ", i, n_count, elem_str);

            if (n_count > max_grp_count)
                max_grp_count = n_count;
        }

        fprintf(stderr, "\n");
    }
}

static void
final_stat_reporting(vset_t visited, mytimer_t timer)
{
    long   n_count;
    char   elem_str[1024];
    double e_count;

    SCCreportTimer(timer, "reachability took");

    if (dlk_detect)
        Warning(info, "No deadlocks found");

    if (act_detect != NULL)
        Warning(info, "Action \"%s\" not found", act_detect);

    get_vset_size(visited, &n_count, &e_count, elem_str, sizeof(elem_str));
    Warning(info, "state space has %s (~%1.2e) states", elem_str, e_count);

    if (max_lev_count == 0) {
        Warning(info, "( %ld final BDD nodes; %ld peak nodes )",
                    n_count, max_vis_count);
    } else {
        Warning(info, "( %ld final BDD nodes; %ld peak nodes; "
                          "%ld peak nodes per level )",
                    n_count, max_vis_count, max_lev_count);
    }

    diagnostic("( peak transition cache: %ld nodes; peak group explored: "
                   "%ld nodes )\n", max_trans_count, max_grp_count);
}

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
            diagnostic("\rexploring group %4d/%d", i, nGrps);
            expand_group_next(i, current_level);
            (*eg_count)++;
        }
        diagnostic("\rexploration complete             \n");
        if (dlk_detect) vset_copy(deadlocks, current_level);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups,i)) continue;
            diagnostic("\rlocal next %4d/%d", i, nGrps);
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
        diagnostic("\rlocal next complete       \n");
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
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
    (void) visited_old;

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
            diagnostic("\rexploring group %4d/%d", i, nGrps);
            expand_group_next(i, visited);
            (*eg_count)++;
        }
        diagnostic("\rexploration complete             \n");
        if (dlk_detect) vset_copy(deadlocks, visited);
        for (int i = 0; i < nGrps; i++) {
            if (!bitvector_is_set(reach_groups,i)) continue;
            diagnostic("\rlocal next %4d/%d", i, nGrps);
            (*next_count)++;
            vset_next(temp, old_vis, group_next[i]);
            if (dlk_detect) {
                vset_prev(dlk_temp, temp, group_next[i]);
                vset_minus(deadlocks, dlk_temp);
                vset_clear(dlk_temp);
            }
            vset_union(visited, temp);
        }
        diagnostic("\rlocal next complete       \n");
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
            diagnostic("\rgroup %4d/%d", i, nGrps);
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
        diagnostic("\rround %d complete       \n", level);
        // no deadlocks in old new_states
        if (dlk_detect) deadlock_check(deadlocks, reach_groups);
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
    (void) visited_old;

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
            diagnostic("\rgroup %4d/%d", i, nGrps);
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
        diagnostic("\rround %d complete       \n", level);
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
            diagnostic("\rexploring group %4d/%d", i, nGrps);
            expand_group_next(i, visited);
            (*eg_count)++;
        }
        diagnostic("\rexploration complete             \n");
        if (dlk_detect) vset_copy(deadlocks, visited);
        vset_least_fixpoint(visited, visited, group_next, nGrps);
        (*next_count)++;
        diagnostic("\rround %d complete       \n", level);
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
    for (int i = 0; i < nGrps; i++)
        for (int j = 0; j < N; j++)
            if (dm_is_set(GBgetDMInfo(model), i, j)) {
                level[i] = (N - j - 1) / sat_granularity;
                break;
            }

    for (int i = 0; i < nGrps; i++)
        bitvector_set(&groups[level[i]], i);

    // Limit the bit vectors to the groups we are interested in and establish
    // which saturation levels are not used.
    for (int k = 0; k < max_sat_levels; k++) {
        bitvector_intersect(&groups[k], reach_groups);
        empty_groups[k] = bitvector_is_empty(&groups[k]);
    }

    // Level diagnostic
    diagnostic("level:");
    for (int i = 0; i < nGrps; i++)
        diagnostic(" %d", level[i]);
    diagnostic("\n");

    if (back == NULL)
        return;

    // back[k] = last + in any group of level k
    bitvector_t level_matrix[(N - 1) / sat_granularity + 1];

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

    // Back diagnostic
    diagnostic("back:");
    for (int k = 0; k < max_sat_levels; k++)
        diagnostic(" %d", back[k]);
    diagnostic("\n");
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

    for (int i = 0; i < nGrps; i++)
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
        for (int i = 0; i < nGrps; i++) vset_destroy(prev_vis[i]);
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

    for (int i = 0; i < nGrps; i++)
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
        for (int i = 0; i < nGrps; i++) vset_destroy(prev_vis[i]);
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
    mytimer_t  timer    = SCCcreateTimer();

    SCCstartTimer(timer);
    Warning(info, "writing output");
    tbl_file = fopen(etf_output, "w");

    if (tbl_file == NULL)
        FatalCall(1, error,"could not open %s", etf_output);

    output_init(tbl_file);
    output_trans(tbl_file);
    output_lbls(tbl_file, visited);
    output_types(tbl_file);

    fclose(tbl_file);
    SCCstopTimer(timer);
    SCCreportTimer(timer, "writing output took");
}

static void
unguided(sat_proc_t sat_proc, reach_proc_t reach_proc, vset_t visited)
{
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

static void
init_model(char *file)
{
    Warning(info, "opening %s", file);
    model = GBcreateBase();
    GBsetChunkMethods(model, new_string_index, NULL, (int2chunk_t)SIgetC,
                          (chunk2int_t)SIputC, (get_count_t)SIgetCount);

    GBloadFile(model, file, &model);

    if (RTverbosity >= 2) {
        fprintf(stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined(stderr, model);
    }

    ltstype = GBgetLTStype(model);
    N = lts_type_get_state_length(ltstype);
    eLbls = lts_type_get_edge_label_count(ltstype);
    sLbls = lts_type_get_state_label_count(ltstype);
    nGrps = dm_nrows(GBgetDMInfo(model));
    max_sat_levels = (N - 1) / sat_granularity + 1;
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
    if (eLbls!=1) Abort("action detection assumes precisely one edge label");
    chunk c = chunk_str(act_detect);
    //table number of first edge label.
    act_detect_table=lts_type_get_edge_label_typeno(ltstype,0);
    act_detect_index=GBchunkPut(model,act_detect_table,c);
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
            Fatal(1,error, "Expecting == with state variable on the left side!\n");
        if (!mu_expr->arg1->token == MU_NUM)
            Fatal(1,error, "Expecting == with int on the right side!\n");
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
        Fatal(1,error, "unhandled MU_NEXT");
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
            Fatal(1,error, "invalid operator following MU_EXIST, expecting MU_NEXT");
        }
    } break;
    case MU_NUM:
        Fatal(1,error, "unhandled MU_NUM");
        break;
    case MU_SVAR:
        Fatal(1,error, "unhandled MU_SVAR");
        break;
    case MU_EVAR:
        Fatal(1, error, "unhandled MU_EVAR");
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
            Fatal(1,error, "invalid operator following MU_ALL, expecting MU_NEXT");
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
        Fatal(1,error, "encountered unhandled mu operator");
    }
    return result;
}

int
main (int argc, char *argv[])
{
    char *files[2];
    RTinitPopt(&argc, &argv, options, 1, 2, files, NULL, "<model> [<etf>]",
                   "Perform a symbolic reachability analysis of <model>\n"
                       "The optional output of this analysis is an ETF "
                           "representation of the input\n\nOptions");

    vset_implementation_t vset_impl = VSET_IMPL_AUTOSELECT;

    sat_proc_t sat_proc = NULL;
    reach_proc_t reach_proc = NULL;
    guided_proc_t guided_proc = unguided;

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

    vset_t visited;

    init_model(files[0]);
    init_domain(vset_impl, &visited);
    if (act_detect != NULL) init_action();

    // temporal logics
    if (mu_formula) {
        mu_expr = mu_parse_file(ltstype, mu_formula);
#if 0
        char buf[1024];
        ltsmin_expr_print_mu(mu_expr, buf);
        printf("computing: %s\n",buf);
#endif
    } else if (ctl_formula) {
        ltsmin_expr_t ctl = ctl_parse_file(ltstype, ctl_formula);
        mu_expr = ctl_star_to_mu(ctl);
        mu_formula = ctl_formula;
    }

    int src[N];

    GBgetInitialState(model, src);
    vset_add(visited, src);
    Warning(info, "got initial state");

    mytimer_t timer = SCCcreateTimer();

    SCCstartTimer(timer);
    guided_proc(sat_proc, reach_proc, visited);
    SCCstopTimer(timer);

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

    exit (EXIT_SUCCESS);
}
