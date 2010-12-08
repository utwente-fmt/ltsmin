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

typedef enum { UseGreyBox , UseBlackBox } mode_t;

static struct {
    //lts_enum_cb_t output_handle=NULL;

    model_t model;
    char* trc_output;
    lts_enum_cb_t trace_handle;
    lts_output_t trace_output;
    int dlk_detect;

    char *ltl_semantics;
    int   ltl_type;
    char *ltl_file;

    //array_manager_t state_man=NULL;
    //uint32_t *parent_ofs=NULL;

    size_t  threshold;

    //int write_lts;
    int matrix;
    int write_state;
    size_t max;

    mode_t call_mode;

    char *arg_strategy;
    enum { Strat_BFS, Strat_DFS, Strat_SCC } strategy;
    char *arg_state_db;
    enum { DB_DBSLL, DB_TreeDBS, DB_Vset } state_db;
} opt = {
    .model = NULL,
    .trc_output = NULL,
    .trace_handle = NULL,
    .trace_output = NULL,
    .dlk_detect = 0,
    .ltl_semantics = "spin",
    .ltl_type = PINS_LTL_SPIN,
    .ltl_file = NULL,
    .threshold = 100000,
    .matrix=0,
    .write_state=0,
    .max = UINT_MAX,
    .call_mode = UseBlackBox,
    .arg_strategy = "bfs",
    .strategy = Strat_BFS,
    .arg_state_db = "tree",
    .state_db = DB_TreeDBS
};

static si_map_entry strategies[] = {
    {"bfs",  Strat_BFS},
    {"dfs",  Strat_DFS},
    {"scc",  Strat_SCC},
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
               const struct poptOption *popt, const char *arg, void *data)
{
    (void)con; (void)popt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
            int db = linear_search (db_types, opt.arg_state_db);
            if (db < 0) {
                Warning (error, "unknown vector storage mode type %s", opt.arg_state_db);
                RTexitUsage (EXIT_FAILURE);
            }
            opt.state_db = db;

            int s = linear_search (strategies, opt.arg_strategy);
            if (s < 0) {
                Warning (error, "unknown search mode %s", opt.arg_strategy);
                RTexitUsage (EXIT_FAILURE);
            }
            opt.strategy = s;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal (1, error, "unexpected call to state_db_popt");
}

static  struct poptOption development_options[] = {
	{ "grey", 0 , POPT_ARG_VAL , &opt.call_mode , UseGreyBox , "make use of GetTransitionsLong calls" , NULL },
	{ "matrix", 0 , POPT_ARG_VAL, &opt.matrix,1,"Print the dependency matrix and quit",NULL},
	{ "write-state" , 0 , POPT_ARG_VAL , &opt.write_state, 1 , "write the full state vector" , NULL },
	POPT_TABLEEND
};

static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)state_db_popt , 0 , NULL , NULL },
	{ "deadlock" , 'd' , POPT_ARG_VAL , &opt.dlk_detect , 1 , "detect deadlocks" , NULL },
	{ "trace" , 0 , POPT_ARG_STRING , &opt.trc_output , 0 , "file to write trace to" , "<lts output>" },
	{ "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &opt.arg_state_db , 0 ,
		"select the data structure for storing states", "<table|tree|vset>"},
	{ "strategy" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &opt.arg_strategy , 0 ,
		"select the search strategy", "<bfs|dfs>"},
	{ "max" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &opt.max , 0 ,"maximum search depth", "<int>"},
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
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, development_options , 0 , "Development options" , NULL },
	POPT_TABLEEND
};

static struct {
    size_t N;
    size_t K;
    size_t state_labels;
    size_t edge_labels;
    size_t max_depth;
    size_t depth;
    size_t visited;
    size_t explored;
    size_t ntransitions;
} global = {
    .visited = 0,
    .explored = 0,
    .depth = 0,
    .max_depth = 0,
    .ntransitions = 0,
};


static void *
new_string_index (void *context)
{
    (void)context;
    Warning (info, "creating a new string index");
    return SIcreate ();
}

typedef struct gsea_state {
    int* state;
    int  count;
    union {
        struct {
            int hash_idx;
        } table;
        struct {
            int tree_idx;
        } tree;
        struct {
            int nothing;
        } vset;
    };
} gsea_state_t;

typedef union gsea_store {
    struct {
        treedbs_t dbs;
        size_t level_bound;
    } tree;
    struct {
        vdom_t domain;
        vset_t closed_set;
        vset_t current_set;
        vset_t next_set;
    } vset;
    struct {
        dbs_ll_t dbs;

        // this should probably go somewhere else
        struct {
            bitset_t         current;
            dfs_stack_t      active;
            dfs_stack_t      roots;
            array_manager_t  dfsnum_man;
            int             *dfsnum;
            int              count;
        } scc;
    } table;
} gsea_store_t;

typedef union gsea_queue {
    struct {
        dfs_stack_t stack;
        bitset_t closed_set;

        // queue/store specific callbacks
        void (*push)(gsea_state_t*, void*);
        void (*pop)(gsea_state_t*, void*);
        void (*peek)(gsea_state_t*, void*);
            // peeks a state representation from the stack
        // closed test for the stack
        int (*closed)(gsea_state_t*, int is_backtrack, void*);
            // defaults to return is_backtrack || bitset_test(gc.queue.filo.closed_set, .._idx)
        // stack representation to state representation
        void (*stack_to_state)(gsea_state_t*, void*);
            // defaults to treeunfold on idx
        // state representation to stack representation
        void (*state_to_stack)(gsea_state_t*, void*);
            // defaults to treefold, set idx
    } filo;
    /*
    struct {
        queue_t queue;
    } fifo;
    */

} gsea_queue_t;

typedef void(*gsea_void)(gsea_state_t*,void*);
typedef int(*gsea_int)(gsea_state_t*,void*);
typedef void (*foreach_open_cb)(gsea_state_t*, void*);

static void gsea_process(void* arg, transition_info_t *ti, int *dst);
static void gsea_foreach_open(foreach_open_cb open_cb, void* arg);
static void gsea_finished(void* arg);
static void gsea_progress(void* arg);

typedef struct gsea_context {
    // init
    void* (*init)(gsea_state_t*);

    // foreach open function
    void (*foreach_open)( foreach_open_cb, void*);

    // open insert
    int (*has_open)(gsea_state_t*, void*);
    int (*open_insert_condition)(gsea_state_t*, void*);

    // open set
    void (*open_insert)(gsea_state_t*, void*);
    void (*open_delete)(gsea_state_t*, void*);
    int  (*open)(gsea_state_t*, void*);
    void (*open_extract)(gsea_state_t*, void*);

    // closed set
    void (*closed_insert)(gsea_state_t*, void*);
    void (*closed_delete)(gsea_state_t*, void*);
    int  (*closed)(gsea_state_t*, void*);
    void (*closed_extract)(gsea_state_t*, void*);

    // state info
    void (*pre_state_next)(gsea_state_t*, void*);
    void (*post_state_next)(gsea_state_t*, void*);
    void (*state_next)(gsea_state_t*, void*);
    int  (*state_backtrack)(gsea_state_t*, void*);

    // search for state
    int  (*goal_reached)(gsea_state_t*, void*);
    void (*goal_trace)(gsea_state_t*, void*);

    void (*report_progress)(void*);
    void (*report_finished)(void*);

    gsea_store_t store;
    gsea_queue_t queue;
    void* context;

    // placeholders
    void (*dlk_placeholder)(gsea_state_t*, void*);
    int  (*max_placeholder)(gsea_state_t*, void*);

} gsea_context_t;

static gsea_context_t gc;


/* debug */
void print_state(gsea_state_t* state)
{
    for(size_t i=0; i < global.N; i++) {
        printf("%d ", state->state[i]);
    } printf("\n");
}


/****************************
 * ___  __        __   ___  *
 *  |  |__)  /\  /  ` |__   *
 *  |  |  \ /~~\ \__, |___  *
 ****************************/

static void
write_trace_state(model_t model, int src_no, int *state){
    Warning(debug,"dumping state %d",src_no);
    int labels[global.state_labels];
    if (global.state_labels) GBgetStateLabelsAll(model,state,labels);
    enum_state(opt.trace_handle,0,state,labels);
}

struct write_trace_step_s {
    int* src_state;
    int* dst_state;
    int found;
    int depth;
};

static void write_trace_next(void*arg,transition_info_t*ti,int*dst){
    struct write_trace_step_s*ctx=(struct write_trace_step_s*)arg;
    if(ctx->found) return;
    for(size_t i=0;i<global.N;i++) {
        if (ctx->dst_state[i]!=dst[i]) return;
    }
    ctx->found=1;
    enum_seg_seg(opt.trace_handle,0,ctx->depth-1,0,ctx->depth,ti->labels);
}

static void write_trace_step(model_t model, int*src,int*dst, int depth){
    //Warning(debug,"finding edge for state %d",src_no);
    struct write_trace_step_s ctx;
    ctx.src_state=src;
    ctx.dst_state=dst;
    ctx.found=0;
    ctx.depth=depth;
    GBgetTransitionsAll(model,src,write_trace_next,&ctx);
    if (ctx.found==0) Fatal(1,error,"no matching transition found");
}









/* GSEA run configurations */

/********************
 *   __   ___  __   *
 *  |__) |__  /__`  *
 *  |__) |    .__/  *
 ********************/

/* TREE configuration */
static void
bfs_tree_open_insert(gsea_state_t* state, void* arg)
{
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    if ((size_t)state->tree.tree_idx >= global.visited)
        global.visited++;
    return;
    (void)arg;
}

static void
bfs_tree_open_extract(gsea_state_t* state, void* arg)
{
    state->tree.tree_idx = global.explored;
    state->state = gc.context;
    TreeUnfold(gc.store.tree.dbs, global.explored, gc.context);
    return;
    (void)arg;
}

static void
bfs_tree_closed_insert(gsea_state_t* state, void* arg) {
    // count depth
    if (gc.store.tree.level_bound == global.explored) {
        if (RTverbosity > 1) Warning(info, "level %zu, has %zu states, explored %zu states %zu transitions", global.max_depth, global.visited - global.explored, global.explored, global.ntransitions);

        global.depth=++global.max_depth;
        gc.store.tree.level_bound = global.visited;
    }
    return; (void)state; (void)arg;
}
static int bfs_tree_has_open(gsea_state_t* state, void* arg) { return global.visited - global.explored; (void)state; (void)arg; }
static int bfs_tree_open_insert_condition(gsea_state_t* state, void* arg) { return 1; (void)state; (void)arg; }


/* VSET configuration */

typedef struct bfs_vset_arg_store { foreach_open_cb open_cb; void* ctx; } bfs_vset_arg_store_t;

static void
bfs_vset_foreach_open_enum_cb (bfs_vset_arg_store_t *args, int *src)
{
    gsea_state_t s_open;
    s_open.state = src;
    args->open_cb(&s_open, args->ctx);
}

static void
bfs_vset_foreach_open(foreach_open_cb open_cb, void* arg)
{
    bfs_vset_arg_store_t args = { open_cb, arg };
    while(!vset_is_empty(gc.store.vset.next_set)) {
        vset_copy(gc.store.vset.current_set, gc.store.vset.next_set);
        vset_clear(gc.store.vset.next_set);
        if (RTverbosity > 1) Warning(info, "level %zu, has %zu states, explored %zu states %zu transitions", global.max_depth, global.visited - global.explored, global.explored, global.ntransitions);
        global.depth++;
        vset_enum(gc.store.vset.current_set, (void(*)(void*,int*)) bfs_vset_foreach_open_enum_cb, &args);
        global.max_depth++;
    }
}

static void
bfs_vset_open_insert(gsea_state_t* state, void* arg)
{
    vset_add(gc.store.vset.next_set, state->state);
    global.visited++;
    return;
    (void)arg;
}

static void
bfs_vset_closed_insert(gsea_state_t* state, void* arg)
{
    vset_add(gc.store.vset.closed_set, state->state);
    return;
    (void)arg;
}

static int
bfs_vset_open(gsea_state_t* state, void* arg)
{
    return vset_member(gc.store.vset.next_set, state->state);
    (void)arg;
}

static int
bfs_vset_closed(gsea_state_t* state, void* arg)
{
    return vset_member(gc.store.vset.closed_set, state->state);
    (void)arg;
}







/********************
 *   __   ___  __   *
 *  |  \ |__  /__`  *
 *  |__/ |    .__/  *
 ********************/

/* dfs framework configuration */
static int
dfs_goal_trace_cb(int* stack_element, void* ctx)
{
    struct write_trace_step_s* trace_ctx = (struct write_trace_step_s*)ctx;
    // printf("state %p, %d\n", stack_element, *stack_element);
    gsea_state_t state;
    // todo: this needs some assign functon instead of peek?
    // assign(state, arg, *queue); -> do what's needed to assign
    // replaces peek
    state.tree.tree_idx = *stack_element;
    state.state=stack_element;
    gc.queue.filo.stack_to_state(&state, NULL);
    // this is the state..
    // print_state(&state);

    // last state
    memcpy(trace_ctx->src_state, trace_ctx->dst_state, global.N * sizeof(int));
    memcpy(trace_ctx->dst_state, state.state, global.N * sizeof(int));

    // write transition, except on initial state
    if (trace_ctx->depth != 0)
        write_trace_step(opt.model, trace_ctx->src_state, trace_ctx->dst_state, trace_ctx->depth);

    write_trace_state(opt.model, trace_ctx->depth, trace_ctx->dst_state);
    trace_ctx->depth++;
    return 1;
}

static void
dfs_goal_trace(gsea_state_t* state, void* arg)
{
    int src[global.N];
    int dst[global.N];
    struct write_trace_step_s ctx;
    ctx.src_state = src;
    ctx.dst_state = dst;
    ctx.depth = 0;
    ctx.found = 0;

    // init trace output
    opt.trace_output=lts_output_open(opt.trc_output,opt.model,1,0,1,"vsi",NULL);
    {
        int init_state[global.N];
        GBgetInitialState(opt.model, init_state);
        lts_output_set_root_vec(opt.trace_output,(uint32_t*)init_state);
        lts_output_set_root_idx(opt.trace_output,0,0);
    }
    opt.trace_handle=lts_output_begin(opt.trace_output,0,1,0);

    // init context, pass context to stackwalker
    dfs_stack_walk_down(gc.queue.filo.stack, dfs_goal_trace_cb, &ctx);

    lts_output_end(opt.trace_output,opt.trace_handle);
    lts_output_close(&opt.trace_output);

    (void)state;
    (void)arg;
}

static void*
dfs_init(gsea_state_t* state)
{
    gc.queue.filo.state_to_stack(state, NULL);
    gc.open_insert(state, NULL);
    return NULL;
}

static void
dfs_open_insert(gsea_state_t* state, void* arg)
{
    gc.queue.filo.state_to_stack(state, arg);
    gc.queue.filo.push(state, arg);
    return;
}

static void
dfs_open_extract(gsea_state_t* state, void* arg)
{
    gc.queue.filo.stack_to_state(state, arg);

    // update max depth
    if (dfs_stack_nframes(gc.queue.filo.stack) > global.max_depth) {
        global.max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, explored %zu states, %zu transitions", global.max_depth, global.explored, global.ntransitions);
    }
    // printf("state %d:", state->tree.tree_idx); print_state(state);
    return;
    (void)arg;
}

static int
dfs_has_open(gsea_state_t* state, void* arg) {
    int has_backtrack = gc.state_backtrack != NULL;
    int is_backtrack;
    do {
        if (dfs_stack_size(gc.queue.filo.stack) == 0) return 0;
        is_backtrack = 0;
        // detect backtrack
        while (dfs_stack_frame_size(gc.queue.filo.stack) == 0) {
            // gc.backtrack(state, arg);
            dfs_stack_leave(gc.queue.filo.stack);
            // pop, because the backtrack state must be closed (except if reopened, which is unsupported)
            // printf("backtrack %d:\n", * dfs_stack_top(gc.queue.filo.stack));
            // if (gc.state_backtrack) gc.state_backtrack(state, arg);
            // less depth
            global.depth--;
            is_backtrack = 1;
        }
        gc.queue.filo.peek(state, arg);
    } while (gc.queue.filo.closed(state, is_backtrack, arg) &&
             (!is_backtrack || (!has_backtrack || gc.state_backtrack(state, arg)) ) &&
             dfs_stack_pop(gc.queue.filo.stack)
            );
    return 1;
    (void)arg;
}

static int dfs_open_insert_condition(gsea_state_t* state, void* arg) { return !gc.closed(state,arg); }

static void
dfs_state_next_all(gsea_state_t* state, void* arg)
{
    // new search depth
    global.depth++;
    // wrap with enter stack frame
    dfs_stack_enter(gc.queue.filo.stack);
    // original call
    state->count = GBgetTransitionsAll (opt.model, state->state, gsea_process, state);
    return;
    (void)arg;
}




/* dfs tree configuration */
static int
dfs_tree_stack_closed(gsea_state_t* state, int is_backtrack, void* arg)
{ return is_backtrack || bitset_test(gc.queue.filo.closed_set, state->tree.tree_idx); (void)arg; }

static void
dfs_tree_stack_peek(gsea_state_t* state, void* arg)
{ state->tree.tree_idx = *dfs_stack_top(gc.queue.filo.stack); return; (void)arg; }

static void
dfs_tree_state_to_stack(gsea_state_t* state, void* arg)
{
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    if ((size_t)state->tree.tree_idx >= global.visited) global.visited++;
    return;
    (void)arg;
}

static void
dfs_tree_stack_to_state(gsea_state_t* state, void* arg)
{ state->state = gc.context; TreeUnfold(gc.store.tree.dbs, state->tree.tree_idx, gc.context); return; (void)arg; }

static void
dfs_tree_open_insert(gsea_state_t* state, void* arg)
{
    dfs_stack_push(gc.queue.filo.stack, &(state->tree.tree_idx));
    return;
    (void)arg;
}

static void dfs_tree_closed_insert(gsea_state_t* state, void* arg)
{ bitset_set(gc.queue.filo.closed_set, state->tree.tree_idx); return; (void)arg;}


static int dfs_tree_closed(gsea_state_t* state, void* arg)
{
    // state is not yet serialized at this point, hence, this must be done here -> error in framework?
    dfs_tree_state_to_stack(state, arg);
    return bitset_test(gc.queue.filo.closed_set, state->tree.tree_idx); (void)state; (void)arg;
}
static int dfs_tree_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_tree_closed(state,arg); (void)state; (void)arg; }


/* dfs vset configuration */
static int dfs_vset_closed(gsea_state_t* state, void* arg) { return vset_member(gc.store.vset.closed_set, state->state); (void)arg; }

static int
dfs_vset_stack_closed(gsea_state_t* state, int is_backtrack, void* arg)
{ return is_backtrack ||dfs_vset_closed(state, arg); }

static void
dfs_vset_stack_peek(gsea_state_t* state, void* arg)
{ state->state = dfs_stack_top(gc.queue.filo.stack); return; (void)arg; }

static void dfs_vset_state_to_stack(gsea_state_t* state, void* arg) { (void)state; (void)arg; }
static void dfs_vset_stack_to_state(gsea_state_t* state, void* arg) { (void)state; (void)arg; }


static void
dfs_vset_open_insert(gsea_state_t* state, void* arg)
{
    // this is not necessary, but do this for now
    // hmm, open insert conditoin should forbid this situation
    if (!dfs_vset_closed(state, arg) && !vset_member(gc.store.vset.next_set, state->state) && !vset_member(gc.store.vset.current_set, state->state))
        global.visited++;

    dfs_stack_push(gc.queue.filo.stack, state->state);
    vset_add(gc.store.vset.current_set, state->state);
    return;
    (void)arg;
}

static void
dfs_vset_closed_insert(gsea_state_t* state, void* arg) {
    vset_add(gc.store.vset.closed_set, state->state);
    return;
    (void)arg;
}

static int dfs_vset_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_vset_closed(state,arg); (void)state; (void)arg; }



/* dfs table configuration */
static int
dfs_table_stack_closed(gsea_state_t* state, int is_backtrack, void* arg)
{ return is_backtrack || bitset_test(gc.queue.filo.closed_set, state->table.hash_idx); (void)arg; }

static void
dfs_table_stack_peek(gsea_state_t* state, void* arg)
{ state->table.hash_idx = *dfs_stack_top(gc.queue.filo.stack); return; (void)arg; }

static void
dfs_table_state_to_stack(gsea_state_t* state, void* arg)
{
    if (!DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx))) {
        global.visited++;
    }
    return;
    (void)arg;
}

static void
dfs_table_stack_to_state(gsea_state_t* state, void* arg)
{
    int hash;
    state->state = DBSLLget(gc.store.table.dbs, state->table.hash_idx, &hash);
    (void)arg;
}

static void dfs_table_open_insert(gsea_state_t* state, void* arg)
{
    dfs_stack_push(gc.queue.filo.stack, &(state->table.hash_idx));
    return;
    (void)arg;
}


static void dfs_table_closed_insert(gsea_state_t* state, void* arg) { bitset_set(gc.queue.filo.closed_set, state->table.hash_idx); (void)arg;}

static int dfs_table_closed(gsea_state_t* state, void* arg) {
    // state is not yet serialized at this point, hence, this must be done here -> error in framework
    dfs_table_state_to_stack(state, arg);
    return bitset_test(gc.queue.filo.closed_set, state->table.hash_idx); (void)state; (void)arg;
}

static int dfs_table_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_table_closed(state,arg); (void)state; (void)arg; }





/********************
 *   __   __   __   *
 *  /__` /  ` /  `  *
 *  .__/ \__, \__,  *
 ********************/

/* scc table configuration */
static void scc_table_open_insert(gsea_state_t* state, void* arg)
{
    if (!DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx))) {
        global.visited++;
    }

    dfs_stack_push(gc.queue.filo.stack, &(state->table.hash_idx));
    return;
    (void)arg;
}

static void scc_table_open_extract(gsea_state_t* state, void* arg)
{
    int* idx = NULL;
    do {
        idx = dfs_stack_top(gc.queue.filo.stack);
        if (bitset_test(gc.queue.filo.closed_set, *idx)) {
            dfs_stack_pop(gc.queue.filo.stack);
            idx = NULL;
        }
    } while (idx == NULL);
    state->table.hash_idx = *idx;
    // index is known
    int hash;

    state->state = DBSLLget(gc.store.table.dbs, *idx, &hash);
    return;
    (void)arg;
}

static void scc_table_closed_insert(gsea_state_t* state, void* arg) { bitset_set(gc.queue.filo.closed_set, state->table.hash_idx); (void)arg;}

static int scc_table_open(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }
static int scc_table_closed(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }


















/****************************************************
 *   __   __   ___          __   ___ ___       __   *
 *  / _` /__` |__   /\     /__` |__   |  |  | |__)  *
 *  \__> .__/ |___ /~~\    .__/ |___  |  \__/ |     *
 ****************************************************/


/* GSEA setup code */

static void
error_state_arg(gsea_state_t* state, void* arg)
{
    Fatal(1, error, "unimplemented functionality in gsea configuration");
    return;
    (void)state;
    (void)arg;
}

static int
error_state_int_arg(gsea_state_t* state, int i, void* arg)
{
    error_state_arg(state, arg);
    (void)i;
    return 0;
}

static void
error_arg(void* arg)
{
    Fatal(1, error, "unimplemented functionality in gsea configuration");
    return;
    (void)arg;
}

static void*
gsea_init_default(gsea_state_t* state)
{
    gc.open_insert(state, NULL);
    return NULL;
}

static int
gsea_open_insert_condition_default(gsea_state_t* state, void* arg) {
    return (!gc.closed(state, arg) && !gc.open(state, arg));
}

static void
gsea_state_next_all_default(gsea_state_t* state, void* arg)
{
    state->count = GBgetTransitionsAll (opt.model, state->state, gsea_process, state);
    return;
    (void)arg;
}

static void
gsea_state_next_grey_default(gsea_state_t* state, void* arg)
{
    state->count = 0;
    for (size_t i = 0; i < global.K; i++) {
        state->count += GBgetTransitionsLong (opt.model, i, state->state, gsea_process, state);
    }
    return;
    (void)arg;
}

static void
gsea_dlk_default(gsea_state_t* state, void* arg)
{
    // chain deadlock test after original code
    if (gc.dlk_placeholder) gc.dlk_placeholder(state, arg);

    // no outgoing transitions
    if (state->count == 0) { // gc.is_goal(..) = return state->count == 0;
        opt.threshold = global.visited-1;
        gc.report_progress(arg);
        Warning(info, "deadlock detected");
        if (opt.trc_output && gc.goal_trace) gc.goal_trace(state, arg);
        Fatal(1, info, "exiting now");
    }
}

static int
gsea_max_wrapper(gsea_state_t* state, void* arg)
{
    // (depth < max depth) with chain original condition
    return (global.depth < opt.max) && gc.max_placeholder(state,arg);
}



static void
gsea_goal_trace_default(gsea_state_t* state, void* arg)
{
    Fatal(1, error, "goal state reached");
    (void)state;
    (void)arg;
}

static void
gsea_setup_default()
{
        // setup standard bfs/tree configuration
        gc.init = gsea_init_default;
        gc.foreach_open = gsea_foreach_open;
        gc.has_open = (gsea_int) error_state_arg;
        gc.open_insert_condition = gsea_open_insert_condition_default;
        gc.open_insert = error_state_arg;
        gc.open_delete = error_state_arg;
        gc.open = (gsea_int) error_state_arg;
        gc.open_extract = error_state_arg;
        gc.closed_insert = error_state_arg;
        gc.closed_delete = error_state_arg;
        gc.closed = (gsea_int) error_state_arg;
        gc.pre_state_next = NULL;
        if (opt.call_mode == UseGreyBox) {
            gc.state_next = gsea_state_next_grey_default;
        } else {
            gc.state_next = gsea_state_next_all_default;
        }
        gc.state_backtrack = NULL;
        gc.post_state_next = NULL;
        gc.goal_reached = (gsea_int) error_state_arg;
        gc.goal_trace = gsea_goal_trace_default;
        gc.report_progress = gsea_progress;
        gc.report_finished = gsea_finished;

        // setup dfs framework
        if (opt.strategy == Strat_DFS) {
            gc.init = dfs_init;
            gc.open_insert_condition = dfs_open_insert_condition;
            gc.open_insert = dfs_open_insert;
            gc.has_open = dfs_has_open;
            gc.open_extract = dfs_open_extract;
            gc.state_next = dfs_state_next_all;
            gc.goal_trace = dfs_goal_trace;

            // init filo  queue
            gc.queue.filo.push = error_state_arg;
            gc.queue.filo.pop = error_state_arg;
            gc.queue.filo.peek = error_state_arg;
            gc.queue.filo.stack_to_state = error_state_arg;
            gc.queue.filo.state_to_stack = error_state_arg;
            gc.queue.filo.closed = error_state_int_arg;
            if (opt.call_mode == UseGreyBox)
                Fatal(1, error, "--grey not implemented for dfs");
        }
}

static void
gsea_setup()
{
    switch(opt.strategy) {

    case Strat_BFS:
        switch (opt.state_db) {
            case DB_TreeDBS:
                // setup standard bfs/tree configuration
                gc.open_insert_condition = bfs_tree_open_insert_condition;
                gc.open_insert = bfs_tree_open_insert;
                gc.open_extract = bfs_tree_open_extract;
                gc.has_open = bfs_tree_has_open;
                gc.closed_insert = bfs_tree_closed_insert;

                gc.store.tree.dbs = TreeDBScreate(global.N);
                gc.store.tree.level_bound = 0;
                gc.context = RTmalloc(sizeof(int) * global.N);
                break;
            case DB_Vset:
                // setup standard bfs/vset configuration
                gc.foreach_open = bfs_vset_foreach_open;
                gc.open_insert = bfs_vset_open_insert;
                gc.open = bfs_vset_open;
                gc.closed_insert = bfs_vset_closed_insert;
                gc.closed = bfs_vset_closed;

                gc.context = RTmalloc(sizeof(int) * global.N);
                gc.store.vset.domain = vdom_create_default (global.N);
                gc.store.vset.closed_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.next_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.current_set = vset_create(gc.store.vset.domain, 0, NULL);
                break;
            default:
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", opt.arg_strategy, opt.arg_state_db );
        }
        break;

    case Strat_DFS:
        switch (opt.state_db) {
            case DB_TreeDBS:
                // setup dfs/tree configuration
                gc.open_insert = dfs_tree_open_insert;
                gc.closed_insert = dfs_tree_closed_insert;
                gc.closed = dfs_tree_closed;
                gc.open_insert_condition = dfs_tree_open_insert_condition;

                gc.queue.filo.closed = dfs_tree_stack_closed;
                gc.queue.filo.peek = dfs_tree_stack_peek;
                gc.queue.filo.stack_to_state = dfs_tree_stack_to_state;
                gc.queue.filo.state_to_stack = dfs_tree_state_to_stack;

                gc.store.tree.dbs = TreeDBScreate(global.N);
                gc.queue.filo.stack = dfs_stack_create(1);
                gc.queue.filo.closed_set = bitset_create(128,128);
                gc.context = RTmalloc(sizeof(int) * global.N);
                break;
            case DB_Vset:
                // dfs/vset configuration
                gc.open_insert = dfs_vset_open_insert;
                gc.closed_insert = dfs_vset_closed_insert;
                gc.closed = dfs_vset_closed;
                gc.open_insert_condition = dfs_vset_open_insert_condition;

                gc.queue.filo.closed = dfs_vset_stack_closed;
                gc.queue.filo.peek = dfs_vset_stack_peek;
                gc.queue.filo.stack_to_state = dfs_vset_stack_to_state;
                gc.queue.filo.state_to_stack = dfs_vset_state_to_stack;

                gc.context = RTmalloc(sizeof(int) * global.N);
                gc.store.vset.domain = vdom_create_default (global.N);
                gc.store.vset.closed_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.next_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.current_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.queue.filo.stack = dfs_stack_create(global.N);
                break;
            case DB_DBSLL:
                gc.open_insert = dfs_table_open_insert;
                gc.closed_insert = dfs_table_closed_insert;
                gc.closed = dfs_table_closed;
                gc.open_insert_condition = dfs_table_open_insert_condition;

                gc.queue.filo.closed = dfs_table_stack_closed;
                gc.queue.filo.peek = dfs_table_stack_peek;
                gc.queue.filo.stack_to_state = dfs_table_stack_to_state;
                gc.queue.filo.state_to_stack = dfs_table_state_to_stack;

                gc.context = RTmalloc(sizeof(int) * global.N);
                gc.store.table.dbs = DBSLLcreate(global.N);
                gc.queue.filo.closed_set = bitset_create(128,128);
                gc.queue.filo.stack = dfs_stack_create(1);
                break;

            default:
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", opt.arg_strategy, opt.arg_state_db );
        }
        break;

    case Strat_SCC:
        switch (opt.state_db) {
            case DB_DBSLL:
                gc.open_insert = scc_table_open_insert;
                gc.open_extract = scc_table_open_extract;
                gc.open = scc_table_open;
                gc.closed_insert = scc_table_closed_insert;
                gc.closed = scc_table_closed;

                gc.context = RTmalloc(sizeof(int) * global.N);
                gc.store.table.dbs = DBSLLcreate(global.N);
                gc.queue.filo.closed_set = bitset_create(128,128);
                gc.queue.filo.stack = dfs_stack_create(1);

                // scc init
                gc.store.table.scc.current = bitset_create (128,128);
                gc.store.table.scc.active = dfs_stack_create (1);
                gc.store.table.scc.roots = dfs_stack_create (1);
                gc.store.table.scc.dfsnum_man = create_manager(65536);
                ADD_ARRAY(gc.store.table.scc.dfsnum_man, gc.store.table.scc.dfsnum, int);
                gc.store.table.scc.count = 0;
                break;

            default:
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", opt.arg_strategy, opt.arg_state_db );
        }
        break;

    default:
        Fatal(1, error, "unimplemented strategy");
    }

    // check deadlocks?
    if (opt.dlk_detect) {
        gc.dlk_placeholder = gc.post_state_next;
        gc.post_state_next = gsea_dlk_default;
    }

    // maximum search depth?
    if (opt.max != UINT_MAX) {
        gc.max_placeholder = gc.open_insert_condition;
        gc.open_insert_condition = gsea_max_wrapper;
    }
}












/**********************************************************************
 *   __   __   ___                    __   __   __    ___             *
 *  / _` /__` |__   /\      /\  |    / _` /  \ |__) |  |  |__|  |\/|  *
 *  \__> .__/ |___ /~~\    /~~\ |___ \__> \__/ |  \ |  |  |  |  |  |  *
 **********************************************************************/

static void gsea_foreach_open_cb(gsea_state_t* s_open, void* arg);

static void
gsea_search(int* src)
{
    gsea_state_t s0;
    s0.state = src;
    void* ctx = gc.init(&s0);

    // while open states, process
    gc.foreach_open(gsea_foreach_open_cb, ctx);

    // give the result
    gc.report_finished(ctx);
}

static void
gsea_foreach_open(foreach_open_cb open_cb, void* arg)
{
    gsea_state_t s_open;
    while(gc.has_open(&s_open, arg)) {
        gc.open_extract(&s_open, arg);
        open_cb(&s_open, arg);
    }
}

static void
gsea_foreach_open_cb(gsea_state_t* s_open, void* arg)
{
    // insert in closed set
    gc.closed_insert(s_open, arg);

    // count new explored state
    global.explored++;

    // find goal state, reopen state.., a*
    if (gc.pre_state_next) gc.pre_state_next(s_open, arg);

    // expand, calls gsea_process
    gc.state_next(s_open, arg);

    // find goal state, .. etc
    if (gc.post_state_next) gc.post_state_next(s_open, arg);

    // progress
    gc.report_progress(arg);
}

static void
gsea_process(void* arg, transition_info_t *ti, int *dst)
{
    gsea_state_t s_next;
    s_next.state = dst;
    // this should be in here.
    if (gc.open_insert_condition(&s_next, arg))
        gc.open_insert(&s_next, arg);
    global.ntransitions++;
    return;
    (void)ti;
}

static void
gsea_progress(void* arg) {
    if (RTverbosity < 1 || global.visited < opt.threshold)
        return;
    if (!cas (&opt.threshold, opt.threshold, opt.threshold << 1))
        return;
    Warning (info, "explored %zu levels ~%zu states ~%zu transitions",
             global.max_depth, global.explored, global.ntransitions);
    return;
    (void)arg;
}

static void
gsea_finished(void* arg) {
    Warning (info, "state space %zu levels, %zu states %zu transitions",
             global.max_depth, global.explored, global.ntransitions);
    return;
    (void)arg;
}











/************************
 *   |\/|  /\  | |\ |   *
 *   |  | /~~\ | | \|   *
 ************************/

int main(int argc, char *argv[]){
    char           *files[2];
    RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<model> [<lts>]",
        "Perform an enumerative reachability analysis of <model>\n\n"
        "Options");

    Warning(info,"loading model from %s",files[0]);
    opt.model=GBcreateBase();
    GBsetChunkMethods(opt.model,new_string_index,NULL,
        (int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

    GBloadFile(opt.model,files[0],&opt.model);

    lts_type_t ltstype=GBgetLTStype(opt.model);
    if (RTverbosity >=2) {
        lts_type_print(info,ltstype);
    }
    global.N=lts_type_get_state_length(ltstype);
    global.K=dm_nrows(GBgetDMInfo(opt.model));
    Warning(info,"length is %d, there are %d groups",global.N,global.K);
    global.state_labels=lts_type_get_state_label_count(ltstype);
    global.edge_labels=lts_type_get_edge_label_count(ltstype);
    Warning(info,"There are %d state labels and %d edge labels",global.state_labels,global.edge_labels);

    int src[global.N];
    GBgetInitialState(opt.model,src);
    Warning(info,"got initial state");

    gsea_setup_default();
    gsea_setup();
    gsea_search(src);

	return 0;
}
