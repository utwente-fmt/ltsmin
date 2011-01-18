#include <config.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>

#include <stdint.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <stringindex.h>
#include <limits.h>
#include "dynamic-array.h"

#include "archive.h"
#include "runtime.h"
#include "treedbs.h"
#include "vector_set.h"
#include "dfs-stack.h"
#include "is-balloc.h"
#include "bitset.h"
#include "scctimer.h"
#include "dbs-ll.h"

#if defined(MCRL)
#include "mcrl-greybox.h"
#endif
#if defined(MCRL2)
#include "mcrl2-greybox.h"
#endif
#if defined(NIPS)
#include "nips-greybox.h"
#endif
#if defined(ETF)
#include "etf-greybox.h"
#endif
#if defined(DIVINE)
#include "dve-greybox.h"
#endif
#if defined(DIVINE2)
#include "dve2-greybox.h"
#endif
#if defined(SPINJA)
#include "spinja-greybox.h"
#endif

static lts_enum_cb_t output_handle=NULL;

static char* trc_output=NULL;
static int dlk_detect=0;
static lts_enum_cb_t trace_handle=NULL;
static lts_output_t trace_output=NULL;

static array_manager_t state_man=NULL;
static uint32_t *parent_ofs=NULL;

static treedbs_t dbs=NULL;
static int write_lts;
static int matrix=0;
static int write_state=0;
static size_t max = UINT_MAX;

typedef enum { UseGreyBox , UseBlackBox } mode_t;
static mode_t call_mode=UseBlackBox;

static char *arg_strategy = "bfs";
static enum { Strat_BFS, Strat_DFS, Strat_NDFS, Strat_SCC } strategy = Strat_BFS;
static char *arg_state_db = "tree";
static enum { DB_DBSLL, DB_TreeDBS, DB_Vset } state_db = DB_TreeDBS;

static si_map_entry strategies[] = {
    {"bfs",  Strat_BFS},
    {"dfs",  Strat_DFS},
    {"ndfs", Strat_NDFS},
    {"scc", Strat_SCC}, // couvreur
    {NULL, 0}
};

static si_map_entry db_types[]={
    {"table", DB_DBSLL},
    {"tree", DB_TreeDBS},
    {"vset", DB_Vset},
    {NULL, 0}
};

static void
state_db_popt (poptContext con, enum poptCallbackReason reason,
               const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
            if (trc_output)
                dlk_detect = 1;

            int db = linear_search (db_types, arg_state_db);
            if (db < 0) {
                Warning (error, "unknown vector storage mode type %s", arg_state_db);
                RTexitUsage (EXIT_FAILURE);
            }
            state_db = db;

            int s = linear_search (strategies, arg_strategy);
            if (s < 0) {
                Warning (error, "unknown search mode %s", arg_strategy);
                RTexitUsage (EXIT_FAILURE);
            }
            strategy = s;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal (1, error, "unexpected call to state_db_popt");
}

static  struct poptOption development_options[] = {
	{ "grey", 0 , POPT_ARG_VAL , &call_mode , UseGreyBox , "make use of GetTransitionsLong calls" , NULL },
	{ "matrix", 0 , POPT_ARG_VAL, &matrix,1,"Print the dependency matrix and quit",NULL},
	{ "write-state" , 0 , POPT_ARG_VAL , &write_state, 1 , "write the full state vector" , NULL },
	POPT_TABLEEND
};

static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)state_db_popt , 0 , NULL , NULL },
	{ "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL },
	{ "trace" , 0 , POPT_ARG_STRING , &trc_output , 0 , "file to write trace to" , "<lts output>" },
	{ "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_state_db , 0 ,
		"select the data structure for storing states", "<table|tree|vset>"},
	{ "strategy" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_strategy , 0 ,
		"select the search strategy", "<bfs|dfs|ndfs|scc>"},
	{ "max" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &max , 0 ,"maximum search depth", "<int>"},
#if defined(MCRL)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl_options , 0 , "mCRL options", NULL },
#endif
#if defined(MCRL2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl2_options , 0 , "mCRL2 options", NULL },
#endif
#if defined(NIPS)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, nips_options , 0 , "NIPS options", NULL },
#endif
#if defined(ETF)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, etf_options , 0 , "ETF options", NULL },
#endif
#if defined(DIVINE)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, dve_options , 0 , "DiVinE options", NULL },
#endif
#if defined(DIVINE2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, dve2_options , 0 , "DiVinE 2.2 options", NULL },
#endif
#if defined(SPINJA)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, spinja_options , 0 , "SPINJA options", NULL },
#endif
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, development_options , 0 , "Development options" , NULL },
	POPT_TABLEEND
};

static vdom_t domain;
static vset_t visited_set;
static vset_t being_explored_set;
static vset_t next_set;
static bitset_t dfs_open_set;
static dfs_stack_t stack;

static bitset_t    ndfs_state_color;
static dfs_stack_t blue_stack;
static dfs_stack_t red_stack;

static vset_t ndfs_cyan;
static vset_t ndfs_blue;
static vset_t ndfs_red;

static bitset_t         scc_current;
static dfs_stack_t      scc_remove;
static dfs_stack_t      scc_roots;
static array_manager_t  scc_dfsnum_man = NULL;
static int             *scc_dfsnum = NULL;
static int              scc_count = 0;

static int N;
static int K;
static int state_labels;
static int edge_labels;
static int visited=1;
static int explored=0;
static size_t ntransitions = 0;

static void
maybe_write_state (model_t model, const int *idx, const int *state)
{
    if (write_lts) {
        int                 labels[state_labels];
        if (state_labels)
            GBgetStateLabelsAll (model, (int *)state, labels);
        if (write_state || idx == NULL) {
            assert (state != NULL);
            enum_state (output_handle, 0 , (int *)state, labels);
        } else {
            assert (idx != NULL);
            if (state_labels) enum_state (output_handle, 0, (int *)idx, labels);
        }
    }
}

static void *
new_string_index (void *context)
{
    (void)context;
    return SIcreate ();
}

/* Transition Callbacks */
static void
vector_next (void *arg, transition_info_t *ti, int *dst)
{
    int                 src_ofs = *(int *)arg;
    if (!vset_member (visited_set, dst)) {
        visited++;
        vset_add (visited_set, dst);
        vset_add (next_set, dst);
    }
    if (write_lts) enum_seg_vec (output_handle, 0, src_ofs, dst, ti->labels);
    ++ntransitions;
}

static void
index_next (void *arg, transition_info_t *ti, int *dst)
{
    int                 src_ofs = *(int *)arg;
    int                 idx = TreeFold (dbs, dst);
    if (idx >= visited) {
        visited = idx + 1;
        if (trc_output) {
            ensure_access(state_man,idx);
            parent_ofs[idx]=src_ofs;
        }
    }
    if (write_lts) enum_seg_seg (output_handle, 0, src_ofs, 0, idx, ti->labels);
    ++ntransitions;
}

static inline void get_state(int state_no, int *state)
{
    TreeUnfold(dbs, state_no, state);
}

static void write_trace_state(model_t model, int src_no, int *state){
    Warning(debug,"dumping state %d",src_no);
    int labels[state_labels];
    if (state_labels) GBgetStateLabelsAll(model,state,labels);
    enum_state(trace_handle,0,state,labels);
}

struct write_trace_step_s {
    int src_no;
    int dst_no;
    int* dst;
    int found;
};

static void write_trace_next(void*arg,transition_info_t*ti,int*dst){
    struct write_trace_step_s*ctx=(struct write_trace_step_s*)arg;
    if(ctx->found) return;
    for(int i=0;i<N;i++) {
        if (ctx->dst[i]!=dst[i]) return;
    }
    ctx->found=1;
    enum_seg_seg(trace_handle,0,ctx->src_no,0,ctx->dst_no,ti->labels);
}

static void write_trace_step(model_t model, int src_no,int*src,int dst_no,int*dst){
    Warning(debug,"finding edge for state %d",src_no);
    struct write_trace_step_s ctx;
    ctx.src_no=src_no;
    ctx.dst_no=dst_no;
    ctx.dst=dst;
    ctx.found=0;
    GBgetTransitionsAll(model,src,write_trace_next,&ctx);
    if (ctx.found==0) Fatal(1,error,"no matching transition found");
}

static void write_trace(model_t model, size_t trace_size, uint32_t *trace)
{
    // write initial state
    size_t i = 0;
    int step = 0;
    int src[N];
    int dst[N];
    get_state(0, dst);
    write_trace_state(model, 0, dst);

    i++;
    while(i < trace_size)
    {
        for(int j=0; j < N; ++j)
            src[j] = dst[j];
        get_state(trace[i], dst);

        // write step
        write_trace_step(model, step, src, step + 1, dst);
        // write dst_idx
        write_trace_state(model, trace[i], dst);

        i++;
        step++;
    }
}

static void find_trace_to(model_t model, int dst_idx, int level)
{
    uint32_t *trace = (uint32_t*)RTmalloc(sizeof(uint32_t) * level);

    if (trace == NULL)
        Fatal(1, error, "unable to allocate memory for trace");

    int i = level - 1;
    int curr_idx = dst_idx;
    trace[i] = curr_idx;
    while(curr_idx != 0)
    {
        i--;
        curr_idx = parent_ofs[curr_idx];
        trace[i] = curr_idx;
    }

    // write trace
    write_trace(model, level - i, &trace[i]);

    RTfree(trace);

    return;
}

static void find_trace(model_t model, int dst_idx, int level) {
    mytimer_t timer = SCCcreateTimer();
    SCCstartTimer(timer);
    find_trace_to(model, dst_idx, level);
    SCCstopTimer(timer);
    SCCreportTimer(timer,"constructing the trace took");
}

static void
find_dfs_stack_trace_tree(model_t model, dfs_stack_t stack)
{
    size_t              trace_len = dfs_stack_nframes(stack);
    size_t              i = trace_len - 1;
    uint32_t           *trace = (uint32_t*)RTmalloc(sizeof(uint32_t) * trace_len);

    // gather trace
    while(dfs_stack_nframes(stack))
    {
        dfs_stack_leave(stack);
        int* idx = dfs_stack_pop(stack);
        trace[i] = *idx;
        i--;
    }

    // write it
    write_trace(model, trace_len, trace);

    RTfree(trace);
}

static void
find_dfs_stack_trace_vset(model_t model, dfs_stack_t stack)
{
    size_t              trace_len = dfs_stack_nframes(stack);
    size_t              i = trace_len - 1;
    uint32_t           *trace = (uint32_t*)RTmalloc(sizeof(uint32_t) * trace_len);
    int                 init_state[N];

    // create treedbs
    dbs = TreeDBScreate(N);

    // load initial state
    GBgetInitialState(model,init_state);

    // initial state should be idx 0
    dbs=TreeDBScreate(N);
    int idx;
    if((idx=TreeFold(dbs,init_state))!=0){
        Fatal(1,error,"unexpected index for initial state: %d", idx);
    }

    // gather trace
    while(dfs_stack_nframes(stack))
    {
        dfs_stack_leave(stack);
        int* state = dfs_stack_pop(stack);
        trace[i] = TreeFold(dbs, state);
        i--;
    }

    // write it
    write_trace(model, trace_len, trace);

    RTfree(trace);
}

static void
vector_next_dfs2 (void *arg, transition_info_t *ti, int *dst)
{
    int                 *src = (int *)arg;
    dfs_stack_push (stack, dst);
    if (write_lts) enum_vec_vec (output_handle, src, dst, ti->labels);
    ++ntransitions;
}

static void
vector_next_dfs (void *arg, transition_info_t *ti, int *dst)
{
    int                 src_ofs = *(int *)arg;
    if (!vset_member (being_explored_set, dst)) {
        ++visited;
        dfs_stack_push (stack, dst);
    }
    if (write_lts) enum_seg_vec (output_handle, 0, src_ofs, dst, ti->labels);
    ++ntransitions;
}

static void
index_next_dfs (void *arg, transition_info_t *ti, int *dst)
{
    int                 src_ofs = *(int *)arg;
    int                 idx = TreeFold (dbs, dst);
    dfs_stack_push (stack, &idx);
    if (idx >= visited) {
        visited = idx + 1;
        bitset_set (dfs_open_set, idx);
    }
    if (write_lts) enum_seg_seg (output_handle, 0, src_ofs, 0, idx, ti->labels);
    ++ntransitions;
}


/* Exploration */
static void
bfs_explore_state_index (void *context, int idx, int *src, int level)
{
    model_t             model = (model_t)context;
    maybe_write_state (model, &idx, src);
    int count = 0;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, src, index_next, &idx);
        break;
    case UseGreyBox:
        for (int i = 0; i < K; i++) {
            count += GBgetTransitionsLong (model, i, src, index_next, &idx);
        }
        break;
    }
    if (count == 0 && dlk_detect) {
        Warning(info,"deadlock found in state %d", idx);
        if (trc_output) {
            trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
            {
                int init_state[N];
                get_state(0, init_state);
                lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
                lts_output_set_root_idx(trace_output,0,0);
            }
            trace_handle=lts_output_begin(trace_output,0,1,0);
            find_trace(model, idx, level);
            lts_output_end(trace_output,trace_handle);
            lts_output_close(&trace_output);
        }
        Fatal(1,info, "exiting now");
    }

    explored++;
    if (explored % 1000 == 0 && RTverbosity >= 2)
        Warning (info, "explored %d visited %d trans %zu",
                 explored, visited, ntransitions);
}

static void
bfs_explore_state_vector (void *context, int *src)
{
    model_t             model = (model_t)context;
    maybe_write_state (model, NULL, src);
	int                 count = 0;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, src, vector_next, &explored);
        break;
    case UseGreyBox:
        for (int i = 0; i < K; i++) {
            count += GBgetTransitionsLong (model, i, src, vector_next, &explored);
        }
        break;
    }
	if (count == 0 && dlk_detect) {
		Warning(info,"deadlock found!");
		Fatal(1,info, "exiting now");
	}
    explored++;
    if (explored % 1000 == 0 && RTverbosity >= 2)
        Warning (info, "explored %d visited %d trans %zu",
                 explored, visited, ntransitions);
}

static void
dfs_explore_state_vector2 (model_t model, const int *src, int *o_next_group)
{
	int                 count = 0;
    if (*o_next_group == 0)
        maybe_write_state (model, NULL, src);
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, (int *)src, vector_next_dfs2, (void *)src);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && !dfs_stack_frame_size(stack); ++i) {
            count += GBgetTransitionsLong (model, i, (int *)src, vector_next_dfs2, (void *)src);
        }
        break;
    }
    if (count == 0 && *o_next_group == 0 && dlk_detect) {
        Warning(info,"deadlock found!");
        if (trc_output) {
            trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
            {
                int init_state[N];
                GBgetInitialState(model, init_state);
                lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
                lts_output_set_root_idx(trace_output,0,0);
            }
            trace_handle=lts_output_begin(trace_output,0,0,0);
            find_dfs_stack_trace_vset(model, stack);
            lts_output_end(trace_output,trace_handle);
            lts_output_close(&trace_output);
        }
        Fatal(1,info, "exiting now");
    }
    if (i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %zu",
                     explored, visited, ntransitions);
    }
    *o_next_group = i;
}

static void
dfs_explore_state_vector (model_t model, int src_idx, const int *src,
                          int *o_next_group)
{
	int                 count = 0;
    if (*o_next_group == 0)
        maybe_write_state (model, NULL, src);
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, (int *)src, vector_next_dfs, &src_idx);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && !dfs_stack_frame_size(stack); ++i) {
            count += GBgetTransitionsLong (model, i, (int *)src, vector_next_dfs, &src_idx);
        }
        break;
    }
    if (count == 0 && *o_next_group == 0 && dlk_detect) {
        Warning(info,"deadlock found!");
        if (trc_output) {
            trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
            {
                int init_state[N];
                GBgetInitialState(model, init_state);
                lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
                lts_output_set_root_idx(trace_output,0,0);
            }
            trace_handle=lts_output_begin(trace_output,0,0,0);
            find_dfs_stack_trace_vset(model, stack);
            lts_output_end(trace_output,trace_handle);
            lts_output_close(&trace_output);
        }
        Fatal(1,info, "exiting now");
    }
    if (i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %zu",
                     explored, visited, ntransitions);
    }
    *o_next_group = i;
}

static void
dfs_explore_state_index (model_t model, int idx, int *o_next_group)
{
    int                 state[N];
	int                 count = 0;
    TreeUnfold (dbs, idx, state);
    if (*o_next_group == 0)
        maybe_write_state (model, &idx, state);
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        count = GBgetTransitionsAll (model, state, index_next_dfs, &idx);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && dfs_stack_frame_size (stack) == 0; ++i) {
            count += GBgetTransitionsLong (model, i, state, index_next_dfs, &idx);
        }
        break;
    }
    if (count == 0 && *o_next_group == 0 && dlk_detect) {
        Warning(info,"deadlock found!");
        if (trc_output) {
            trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
            {
                int init_state[N];
                get_state(0, init_state);
                lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
                lts_output_set_root_idx(trace_output,0,0);
            }
            trace_handle=lts_output_begin(trace_output,0,0,0);
            find_dfs_stack_trace_tree(model, stack);
            lts_output_end(trace_output,trace_handle);
            lts_output_close(&trace_output);
        }
        Fatal(1,info, "exiting now");
    }
    if (i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %zu",
                     explored, visited, ntransitions);
    }
    *o_next_group = i;
}

static void
dfs_explore (model_t model, int *src, size_t *o_depth)
{
    enum { SD_NEXT_GROUP, SD_SRC_IDX, SD__SIZE };
    isb_allocator_t     buffer;
    size_t              depth = 0;
    int                 next_group = 0;
    int                 write_idx = 0;
    int                 src_idx = 0;
    int                *fvec;
    switch (state_db) {
    case DB_Vset:
        buffer = isba_create (SD__SIZE);
        domain = vdom_create_default (N);
        being_explored_set = vset_create (domain, 0, NULL);
        stack = dfs_stack_create (N);
        dfs_stack_push (stack, src);
        while ((src = dfs_stack_top (stack)) || dfs_stack_nframes (stack)) {
            if (src == NULL) {
                dfs_stack_leave (stack);
                int *sd = isba_pop_int (buffer);
                next_group = sd[SD_NEXT_GROUP];
                src_idx = sd[SD_SRC_IDX];
                continue;
            }
            if (next_group == 0) {
                if (vset_member (being_explored_set, src) ||
                    dfs_stack_nframes (stack) > max)
                    next_group = K;
                else {
                    vset_add (being_explored_set, src);
                    src_idx = write_idx++;
                }
            }

            if (next_group < K) {
                dfs_stack_enter (stack);
                dfs_explore_state_vector (model, src_idx, src, &next_group);
                isba_push_int (buffer, (int[SD__SIZE]){next_group, src_idx});
                if (dfs_stack_nframes (stack) > depth) {
                    depth = dfs_stack_nframes (stack);
                    if (RTverbosity >= 1)
                        Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                                 depth, visited, ntransitions);
                }
            } else {
                dfs_stack_pop (stack);
            }
            next_group = 0;
        }
        break;

    case DB_TreeDBS:
        buffer = isba_create (1);
        /* Store folded states on the stack, at the cost of having to
           unfold them */
        stack = dfs_stack_create (1);
        dfs_open_set = bitset_create (128,128);
        dbs = TreeDBScreate (N);
        int                 idx = TreeFold (dbs, src);
        fvec = &idx;
        dfs_stack_push (stack, fvec);
        bitset_set (dfs_open_set, *fvec);
        while ((fvec = dfs_stack_top (stack)) || dfs_stack_nframes (stack)) {
            if (fvec == NULL) {
                dfs_stack_leave (stack);
                next_group = *isba_pop_int (buffer);
                continue;
            }
            if (next_group == 0) {
                if (!bitset_test (dfs_open_set, *fvec) ||
                    dfs_stack_nframes (stack) > max)
                    next_group = K;
                else
                    bitset_clear (dfs_open_set, *fvec);
            }

            if (next_group < K) {
                dfs_stack_enter (stack);
                dfs_explore_state_index (model, *fvec, &next_group);
                isba_push_int (buffer, &next_group);
                if (dfs_stack_nframes (stack) > depth) {
                    depth = dfs_stack_nframes (stack);
                    if (RTverbosity >= 1)
                        Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                                 depth, visited, ntransitions);
                }
            } else {
                dfs_stack_pop (stack);
            }
            next_group = 0;
        }
        break;
    case DB_DBSLL:
        stack = dfs_stack_create (N);
        dbs_ll_t dbsll = DBSLLcreate(N);
        buffer = isba_create (1);
        int                 index;
        fvec = src;
        dfs_stack_push (stack, fvec);//dummy
                isba_push_int (buffer, &next_group);
        dfs_stack_enter (stack);
        dfs_stack_push (stack, fvec);
        while (dfs_stack_nframes (stack)) {
            while ((fvec = dfs_stack_top (stack))) {
                if (next_group) break;
                /* explore stack frame for an new state */
                if (!DBSLLlookup_ret(dbsll, fvec, &index)) {
                    ++visited;
                    break;
                } else {
                    dfs_stack_pop (stack);
                }
            }
            if (fvec == NULL) {
                dfs_stack_leave (stack);
                next_group = *isba_pop_int (buffer);
                continue;
            }
            if (next_group < K) {
                dfs_stack_enter (stack);
                dfs_explore_state_vector2 (model, fvec, &next_group);
                isba_push_int (buffer, &next_group);
                if (dfs_stack_nframes (stack) > depth) {
                    depth = dfs_stack_nframes (stack);
                    if (RTverbosity >= 1)
                        Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                                 depth, visited, ntransitions);
                }
            } else {
                dfs_stack_pop (stack);
            }
            next_group = 0;
        }
    break;
    default:
        Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
               strategies[strategy].key, db_types[state_db].key);
    }
    *o_depth = depth;
}

/* State colors are encoded using one bitset, where two consecutive bits
 * describe four colors: (thus state 0 uses bit 0 and 1, state 1, 2 and 3, ..)
 * Colors:
 * While: 0 0) first generated by next state call
 * Cyan:  0 1) A state whose blue search has not been terminated
 * Blue:  1 0) A state that has finished its blue search and has not yet been reached
 *        in a red search
 * Red:   1 1) A state that has been considered in both the blue and the red search
 */
typedef enum {NDFS_WHITE, NDFS_CYAN, NDFS_BLUE, NDFS_RED} ndfs_color_t;

static ndfs_color_t ndfs_get_color(int src_idx)
{
    if (bitset_test (ndfs_state_color, src_idx*2)) {
        if (bitset_test(ndfs_state_color, src_idx*2+1))
            return NDFS_RED;
        else
            return NDFS_BLUE;
    } else {
        if (bitset_test(ndfs_state_color, src_idx*2+1))
            return NDFS_CYAN;
        else
            return NDFS_WHITE;
    }
}

static void ndfs_set_color(int src_idx, ndfs_color_t color)
{
    switch (color) {
        case NDFS_WHITE:
            bitset_clear(ndfs_state_color, src_idx*2);
            bitset_clear(ndfs_state_color, src_idx*2+1);
            break;
        case NDFS_CYAN:
            bitset_clear(ndfs_state_color, src_idx*2);
            bitset_set(ndfs_state_color, src_idx*2+1);
            break;
        case NDFS_BLUE:
            bitset_set(ndfs_state_color, src_idx*2);
            bitset_clear(ndfs_state_color, src_idx*2+1);
            break;
        case NDFS_RED:
            bitset_set(ndfs_state_color, src_idx*2);
            bitset_set(ndfs_state_color, src_idx*2+1);
            break;
    }
}

#if 0
/* for debugging only! */
static void
ndfs_print_state(treedbs_t dbs, int src_idx)
{
    char color[] = {'W', 'C', 'B', 'R'};
    int src[N];
    TreeUnfold (dbs, src_idx, src);
    for(int i=0; i < N; i++) {
        printf("%d ", src[i]);
    }
    printf(" [%c]\n", color[ndfs_get_color(src_idx)]);
}
#endif

typedef struct ndfs_context
{
    model_t model;
    int* src;
} ndfs_context_t;

static void
ndfs_report_cycle(model_t model)
{
    Warning(info,"accepting cycle found!");
    if (trc_output) {
        trace_output=lts_output_open(trc_output,model,1,0,1,"vsi",NULL);
        {
            int init_state[N];
            TreeUnfold(dbs, 0, init_state);
            lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
            lts_output_set_root_idx(trace_output,0,0);
        }
        dfs_stack_enter(stack);
        trace_handle=lts_output_begin(trace_output,0,0,0);
        find_dfs_stack_trace_tree(model, stack);
        lts_output_end(trace_output,trace_handle);
        lts_output_close(&trace_output);
    }
    Fatal(1,info, "exiting now");
}

static void
ndfs_tree_red_next (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    ndfs_context_t*     ctx = (ndfs_context_t*) arg;
    (void) ctx;
    int                 idx = TreeFold (dbs, dst);
    ndfs_color_t        idx_color = ndfs_get_color(idx);
    if (idx_color == NDFS_CYAN) {
        // push this state on the stack for trace
        dfs_stack_push (red_stack, &idx);
        ndfs_report_cycle(ctx->model);
    } else if (idx_color == NDFS_BLUE) {
        ndfs_set_color(idx, NDFS_RED);
        dfs_stack_push (red_stack, &idx);
    }
}

static void
ndfs_tree_blue_next (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    ndfs_context_t*     ctx = (ndfs_context_t*) arg;
    int                 idx = TreeFold (dbs, dst);
    ndfs_color_t        idx_color = ndfs_get_color(idx);
    if (idx_color == NDFS_CYAN &&
       (GBbuchiIsAccepting(ctx->model, ctx->src) ||
        GBbuchiIsAccepting(ctx->model, dst))) {
        // push last state on the stack for trace
        dfs_stack_push (blue_stack, &idx);
        ndfs_report_cycle(ctx->model);
    } else if (idx_color == NDFS_WHITE) {
        dfs_stack_push (blue_stack, &idx);
    }
    if (idx >= visited) visited = idx + 1;
    ++ntransitions;
}

static void
ndfs_vset_red_next (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    ndfs_context_t*     ctx = (ndfs_context_t*) arg;
    (void) ctx;
    if (vset_member(ndfs_blue, dst)) {
        if (!vset_member(ndfs_red, dst)) {
            vset_add(ndfs_blue, dst); // needed ?
            vset_add(ndfs_red, dst);
            dfs_stack_push (red_stack, dst);
        }
    } else {
        if (vset_member(ndfs_cyan, dst)) {
            Fatal(1, error, "accepting cycle found!");
        }
    }
}

static void
ndfs_vset_blue_next (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    ndfs_context_t*     ctx = (ndfs_context_t*) arg;
    if (!vset_member (ndfs_cyan, dst)) {
        dfs_stack_push (blue_stack, dst);
    } else {
        // if not blue, it is cyan
        if (!vset_member(ndfs_blue, dst) &&
           (GBbuchiIsAccepting(ctx->model, ctx->src) ||
            GBbuchiIsAccepting(ctx->model, dst))) {
            Fatal(1, error, "accepting cycle found!");
        }
    }
    ++ntransitions;
}

static void
ndfs_expand (model_t model, int* state, int *o_next_group, TransitionCB red_blue_cb, int count)
{
    ndfs_context_t ctx = {model, state};
    int                 i = *o_next_group;
    switch (call_mode) {
    case UseBlackBox:
        GBgetTransitionsAll (model, state, red_blue_cb, &ctx);
        i = K;
        break;
    case UseGreyBox:
        /* try to find at least one transition */
        for (; i < K && dfs_stack_frame_size (stack) == 0; ++i) {
            GBgetTransitionsLong (model, i, state, red_blue_cb, &ctx);
        }
        break;
    }
    if (count && i == K) {
        ++explored;
        if (explored % 1000 == 0 && RTverbosity >= 2)
            Warning (info, "explored %d visited %d trans %zu",
                     explored, visited, ntransitions);
    }
    *o_next_group = i;
}

static void
ndfs_tree_red(model_t model, isb_allocator_t buffer)
{
    int                 next_group = 0;
    int*                fvec = NULL;
    int*                fvec_start = dfs_stack_top(red_stack);

    while ((fvec = dfs_stack_top (red_stack)) || dfs_stack_nframes (red_stack)) {
        if (fvec == NULL) {
            dfs_stack_leave (red_stack);
            next_group = *isba_pop_int (buffer);
            continue;
        }
        if (next_group == 0) {
            if (ndfs_get_color(*fvec) == NDFS_RED ||
                dfs_stack_nframes (blue_stack) > max) {
                next_group = K;
            }
        }

        if (next_group < K) {
            int                 state[N];
            TreeUnfold(dbs, *fvec, state);
            dfs_stack_enter (red_stack);
            ndfs_expand(model, state, &next_group, ndfs_tree_red_next, 0);
            isba_push_int (buffer, &next_group);
        } else {
            ndfs_set_color(*fvec, NDFS_RED);
            dfs_stack_pop (red_stack);
            // does this work correctly on cycles?
            if (fvec == fvec_start) break;
        }
        next_group = 0;
    }
}

static void
ndfs_tree_blue(model_t model, size_t *o_depth)
{
    isb_allocator_t     buffer;
    size_t              depth = 0;
    int                 next_group = 0;
    buffer = isba_create (1);
    int*                fvec = NULL;
    int                 state[N];

    /* Store folded states on the stack, at the cost of having to
       unfold them */
    while ((fvec = dfs_stack_top (blue_stack)) || dfs_stack_nframes (blue_stack)) {
        if (fvec == NULL) {
            dfs_stack_leave (blue_stack);
            next_group = *isba_pop_int (buffer);
            continue;
        }
        if (next_group == 0) {
            if (ndfs_get_color(*fvec) != NDFS_WHITE ||
                dfs_stack_nframes (blue_stack) > max) {
                next_group = K;
            } else {
                ndfs_set_color(*fvec, NDFS_CYAN);
            }
        }

        // unfold here
        TreeUnfold(dbs, *fvec, state);
        if (next_group < K) {
            dfs_stack_enter (blue_stack);
            ndfs_expand(model, state, &next_group, ndfs_tree_blue_next, 1);
            isba_push_int (buffer, &next_group);
            if (dfs_stack_nframes (blue_stack) > depth) {
                depth = dfs_stack_nframes (blue_stack);
                if (RTverbosity >= 1)
                    Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                             depth, visited, ntransitions);
            }
        } else {
            // state is popped
            {
                if (GBbuchiIsAccepting(model, state)) {
                    // note red_stack == blue_stack, state
                    // is popped by red search
                    ndfs_tree_red(model, buffer);
                } else {
                    ndfs_set_color(*fvec, NDFS_BLUE);
                    dfs_stack_pop (blue_stack);
                }
            }
        }
        next_group = 0;
    }
    *o_depth = depth;
}

static void
ndfs_vset_red(model_t model, isb_allocator_t buffer)
{
    int                 next_group = 0;
    int*                src = NULL;
    int*                src_start = dfs_stack_top(red_stack);

    while ((src = dfs_stack_top (red_stack)) || dfs_stack_nframes (red_stack)) {
        if (src == NULL) {
            dfs_stack_leave (red_stack);
            next_group = *isba_pop_int (buffer);
            continue;
        }
        if (next_group == 0) {
            if (vset_member(ndfs_red, src) ||
                dfs_stack_nframes (blue_stack) > max) {
                next_group = K;
            }
        }

        if (next_group < K) {
            dfs_stack_enter (red_stack);
            ndfs_expand(model, src, &next_group, ndfs_vset_red_next, 0);
            isba_push_int (buffer, &next_group);
        } else {
            vset_add (ndfs_red, src);
            vset_add (ndfs_blue, src);
            dfs_stack_pop (red_stack);
            // does this work correctly on cycles?
            if (src == src_start) break;
        }
        next_group = 0;
    }
}

static void
ndfs_vset_blue(model_t model, int* src, size_t *o_depth)
{
    size_t              depth = 0;
    int                 next_group = 0;
    isb_allocator_t     buffer = isba_create (1);
    while ((src = dfs_stack_top (blue_stack)) || dfs_stack_nframes (blue_stack)) {
        if (src == NULL) {
            dfs_stack_leave (blue_stack);
            next_group = *isba_pop_int (buffer);
            continue;
        }
        if (next_group == 0) {
            // vset_member (ndfs_red) implies vset_member(ndfs_blue)
            // which implies vset_member(ndfs_cyan), hence not white
            if (vset_member (ndfs_cyan, src))
                next_group = K;
            else {
                vset_add (ndfs_cyan, src);
                visited++;
            }
        }

        if (next_group < K) {
            dfs_stack_enter (blue_stack);
            ndfs_expand(model, src, &next_group, ndfs_vset_blue_next, 1);
            isba_push_int (buffer, &next_group);
            if (dfs_stack_nframes (blue_stack) > depth) {
                depth = dfs_stack_nframes (blue_stack);
                if (RTverbosity >= 1)
                    Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                             depth, visited, ntransitions);
            }
        } else {
            {
                if (GBbuchiIsAccepting(model, src)) {
                    // note red_stack == blue_stack, state
                    // is popped by red search
                    ndfs_vset_red(model, buffer);
                } else {
                    vset_add (ndfs_blue, src);
                    //vset_add (ndfs_cyan) state must already be in cyan
                    dfs_stack_pop (blue_stack);
                }
            }
        }
        next_group = 0;
    }
    *o_depth = depth;
}

/* NDFS exploration for checking ltl properties
 * Algorithm taken from:
 * A Note on On-The-Fly Verification Algorithms
 * Stefan Schwoon and Javier Esparza
 */
static void
ndfs_explore (model_t model, int *src, size_t *o_depth)
{
    if (max != UINT_MAX) Fatal(1, error, "undefined behaviour for max with NDFS");
    switch (state_db) {
    case DB_Vset:
        // vset ndfs_cyan, ndfs_blue, ndfs_red
        // encoding ([..] = optional check)
        // white = !vset_member(cyan) [ && !vset_member(blue) && !vset_member(red) ]
        // blue  = !vset_member(red) && vset_member(blue) [ && vset_member(cyan) ]
        // cyan  = !vset_member(blue) && vset_member(cyan) [ && !vset_member(red) ]
        // red  == vset_member(red) [ && vset_member(blue) && vset_member(cyan) ]
        visited = 0;
        domain = vdom_create_default (N);
        ndfs_cyan = vset_create (domain, 0, NULL);
        ndfs_blue = vset_create (domain, 0, NULL);
        ndfs_red = vset_create (domain, 0, NULL);
        stack = blue_stack = red_stack = dfs_stack_create (N);
        dfs_stack_push (blue_stack, src);
        ndfs_vset_blue(model, src, o_depth);
        break;

    case DB_TreeDBS: {
        stack = blue_stack = red_stack = dfs_stack_create (1);
        ndfs_state_color = bitset_create (128,128);
        dbs = TreeDBScreate (N);
        int                 idx = TreeFold (dbs, src);
        dfs_stack_push (blue_stack, &idx);
        ndfs_tree_blue(model, o_depth);
        } break;

    default:
        Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
               strategies[strategy].key, db_types[state_db].key);
    }
}

static void
scc_tree_remove (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    (void)arg;
    int                 idx = TreeFold (dbs, dst);
    if (bitset_test(scc_current, idx)) {
        bitset_clear(scc_current, idx);
        dfs_stack_push(scc_remove, &idx);
    }
}

static void
scc_tree_next (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    (void)arg;
    int                 idx = TreeFold (dbs, dst);
    ensure_access(scc_dfsnum_man, idx);
    int                 dfsnum = scc_dfsnum[idx];
    if (dfsnum == 0) {
        dfs_stack_push (stack, &idx);
    } else {
        if (bitset_test(scc_current, idx)) {
            // fix roots
            int *root;
            do {
                root = dfs_stack_pop(scc_roots);
                // if accepting?
                if (scc_dfsnum[*root] & 0x01)
                    Fatal(1, error, "accepting cycle found!");
            } while (scc_dfsnum[*root] > dfsnum);
            dfs_stack_push(scc_roots, root);
        }
    }
    if (idx >= visited) visited = idx + 1;
    ++ntransitions;
}

static void
scc_tree(model_t model, size_t *o_depth)
{
    isb_allocator_t     buffer;
    size_t              depth = 0;
    int                 next_group = 0;
    buffer = isba_create (1);
    int*                fvec = NULL;
    int                 state[N];

    /* Store folded states on the stack, at the cost of having to
       unfold them */
    while ((fvec = dfs_stack_top (stack)) || dfs_stack_nframes (stack)) {
        if (fvec == NULL) {
            dfs_stack_leave (stack);
            next_group = *isba_pop_int (buffer);
            continue;
        }
        ensure_access(scc_dfsnum_man, *fvec);
        if (next_group == 0) {
            if (scc_dfsnum[*fvec] ||
                dfs_stack_nframes (stack) > max) {
                next_group = K;
            } else {
                scc_count+=2;
                scc_dfsnum[*fvec] = scc_count;
                bitset_set(scc_current, *fvec);
                dfs_stack_push(scc_roots, fvec);
            }
        }

        if (next_group < K) {
            // unfold here
            TreeUnfold(dbs, *fvec, state);
            // check accepting, encode using 1 bit of scc_dfsnum
            if (next_group ==0)
                if (GBbuchiIsAccepting(model, state)) scc_dfsnum[*fvec]++;
            dfs_stack_enter (stack);
            ndfs_expand(model, state, &next_group, scc_tree_next, 1);
            isba_push_int (buffer, &next_group);
            if (dfs_stack_nframes (stack) > depth) {
                depth = dfs_stack_nframes (stack);
                if (RTverbosity >= 1)
                    Warning (info, "new depth reached %d. Visited %d states and %zu transitions",
                             depth, visited, ntransitions);
            }
        } else {
            if (bitset_test(scc_current, *fvec)) {
                bitset_clear(scc_current, *fvec);
                int *root;
                if ((root = dfs_stack_top(scc_roots))) {
                    if (*root == *fvec) {
                        dfs_stack_pop(scc_roots);
                        dfs_stack_push(scc_remove, root);
                        while ((root = dfs_stack_top(scc_remove))) {
                            dfs_stack_pop(scc_remove);
                            int next_grp = 0;
                            TreeUnfold(dbs, *root, state);
                            // note: not in dfs order, but should work anyway
                            while(next_grp != K) {
                                ndfs_expand(model, state, &next_grp, scc_tree_remove, 0);
                            }
                        }
                    }
                }
            }
            dfs_stack_pop (stack);
        }
        next_group = 0;
    }
    *o_depth = depth;
}

/* SCC exploration for checking ltl properties
 * Algorithm taken from:
 * A Note on On-The-Fly Verification Algorithms
 * Stefan Schwoon and Javier Esparza
 */
static void
scc_explore (model_t model, int *src, size_t *o_depth)
{
    // adding test in scc_remove and bailing out when scc_dfsnum[idx] == 0 should be enought to fix max param
    if (max != UINT_MAX) Fatal(1, error, "undefined behaviour for max with SCC");
    switch (state_db) {
    case DB_Vset:
        Fatal(1, error, "scc not implemented for vset");
        break;

    case DB_TreeDBS: {
        stack = dfs_stack_create (1);
        scc_remove = dfs_stack_create (1);
        scc_roots = dfs_stack_create (1);
        scc_current = bitset_create (128,128);
        scc_dfsnum_man=create_manager(65536);
        ADD_ARRAY(scc_dfsnum_man, scc_dfsnum, int);
        dbs = TreeDBScreate (N);
        int                 idx = TreeFold (dbs, src);
        dfs_stack_push (stack, &idx);
        scc_tree(model, o_depth);
        } break;

    default:
        Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
               strategies[strategy].key, db_types[state_db].key);
    }
}
/* Main */
static void
init_write_lts (lts_output_t *p_output,
                const char *filename, int db_type,
                model_t model, int *src)
{
    lts_output_t output = NULL;
    switch (db_type) {
    case DB_TreeDBS:
        output = lts_output_open ((char *)filename, model, 1, 0, 1,
                                   write_state ? "vsi" : "-ii", NULL);
        if (write_state)
            lts_output_set_root_vec (output, (uint32_t *)src);
        break;
    case DB_Vset:
        output = lts_output_open ((char *)filename, model, 1, 0, 1, "viv", NULL);
        lts_output_set_root_vec (output, (uint32_t *)src);
        break;
    }
    lts_output_set_root_idx (output, 0, 0);
    output_handle = lts_output_begin (output, 0, 1, 0);
    *p_output = output;
}

int main(int argc, char *argv[]){
	char           *files[2];
        lts_output_t    output = NULL;
	RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<model> [<lts>]",
		"Perform an enumerative reachability analysis of <model>\n\n"
		"Options");
	if (files[1]) {
		Warning(info,"Writing output to %s",files[1]);
		write_lts=1;
	} else {
		Warning(info,"No output, just counting the number of states");
		write_lts=0;
	}

	Warning(info,"loading model from %s",files[0]);
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

	if (matrix) {
	  GBprintDependencyMatrix(stdout,model);
	  exit (EXIT_SUCCESS);
	}
	if (RTverbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	  fprintf(stderr,"Read Dependency Matrix:\n");
	  GBprintDependencyMatrixRead(stderr,model);
	  fprintf(stderr,"Write Dependency Matrix:\n");
	  GBprintDependencyMatrixWrite(stderr,model);
	}
    if (trc_output && strategy == Strat_BFS)
    {
        state_man=create_manager(65536);
        ADD_ARRAY(state_man, parent_ofs, uint32_t);
    }
	lts_type_t ltstype=GBgetLTStype(model);
    if (RTverbosity >=2) {
        lts_type_print(info,ltstype);
    }
    N=lts_type_get_state_length(ltstype);
	K= dm_nrows(GBgetDMInfo(model));
	Warning(info,"length is %d, there are %d groups",N,K);
	state_labels=lts_type_get_state_label_count(ltstype);
	edge_labels=lts_type_get_edge_label_count(ltstype);
	Warning(info,"There are %d state labels and %d edge labels",state_labels,edge_labels);
	if (state_labels&&write_lts&&!write_state) {
		Fatal(1,error,"Writing state labels without state vectors is unsupported. "
                      "Writing of state vector is enabled with option --write-state");
	}
	int src[N];
	GBgetInitialState(model,src);
	Warning(info,"got initial state");

        if (write_lts) init_write_lts (&output, files[1], state_db, model, src);

        size_t level = 0;
	switch (strategy) {
        case Strat_BFS:
            switch (state_db) {
            case DB_Vset:
		if (trc_output) Fatal(1, error, "--trace not supported for vset, use tree");
		domain=vdom_create_default(N);
		visited_set=vset_create(domain,0,NULL);
		next_set=vset_create(domain,0,NULL);
		vset_add(visited_set,src);
		vset_add(next_set,src);
		vset_t current_set=vset_create(domain,0,NULL);
		while (!vset_is_empty(next_set)){
		  if (RTverbosity >= 1)
		    Warning(info,"level %d has %d states, explored %d states %zu transitions",
			    level,(visited-explored),explored,ntransitions);
		  if (level == max) break;
		  level++;
		  vset_copy(current_set,next_set);
		  vset_clear(next_set);
		  vset_enum(current_set,bfs_explore_state_vector,model);
		}
		bn_int_t e_count;
		long nodes;
                char string[1024];
		int size;
		vset_count(visited_set,&nodes,&e_count);
                size = bn_int2string(string,sizeof string,&e_count);
		if(size >= (ssize_t)sizeof string) Fatal(1,error,"Error converting number to string");
	    	Warning(info,"%s reachable states represented symbolically with %ld nodes",string,nodes);
                bn_clear(&e_count);
		break;
            case DB_DBSLL:
                Fatal(1, error, "State storage and search strategy combination not implemented");
            case DB_TreeDBS:
                dbs=TreeDBScreate(N);
                int idx;
                if((idx=TreeFold(dbs,src))!=0){
                    Fatal(1,error,"unexpected index for initial state: %d", idx);
                }
                int limit=explored;
                while(explored<visited){
                  if (limit==explored){
                    if (RTverbosity >= 1)
                      Warning(info,"level %d has %d states, explored %d states %zu transitions",
                          level,(visited-explored),explored,ntransitions);
                    limit=visited;
                    level++;
                    if (level == max) break;
                  }
                  TreeUnfold(dbs,explored,src);
                  bfs_explore_state_index(model,explored,src,level);
                }
                break;
            default:
                Fatal (1, error, "Unsupported combination: strategy=%s, state=%s",
                       strategies[strategy].key, db_types[state_db].key);
            }
            Warning(info,"state space has %zu levels %d states %zu transitions",
                    level,visited,ntransitions);
            break;
        case Strat_DFS: {
            size_t depth = 0;
            dfs_explore (model, src, &depth);
            Warning (info, "state space has depth %zu, %d states %zu transitions",
                    depth, visited, ntransitions);
            break;
        }
        case Strat_NDFS: {
            // exception for Strat_NDFS, only works in combination with ltl formula
            if (GBgetAcceptingStateLabelIndex(model) < 0) {
                Abort("NDFS search only works in combination with an accepting state label"
                      " (see LTL options)");
            }
            size_t depth = 0;
            ndfs_explore(model, src, &depth);
            Warning (info, "state space has depth %zu, %d states %zu transitions",
                    depth, visited, ntransitions);
            break;
        }
        case Strat_SCC: {
            // exception for Strat_SCC, only works in combination with ltl formula
            if (GBgetAcceptingStateLabelIndex(model) < 0) {
                Abort("NDFS search only works in combination with an accepting state label"
                      " (see LTL options)");
            }
            size_t depth = 0;
            scc_explore(model, src, &depth);
            Warning (info, "state space has depth %zu, %d states %zu transitions",
                    depth, visited, ntransitions);
            break;
        }
	}

	if (write_lts){
		lts_output_end(output,output_handle);
		Warning(info,"finishing the writing");
		lts_output_close(&output);
	}
	return 0;
}
