#include <config.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <archive.h>
#include <bitset.h>
#include <dbs-ll.h>
#include <dfs-stack.h>
#include <dynamic-array.h>
#include <is-balloc.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <runtime.h>
#include <scctimer.h>
#include <spec-greybox.h>
#include <stringindex.h>
#include <treedbs.h>
#include <vector_set.h>

typedef enum { UseGreyBox , UseBlackBox } mode_t;

typedef struct grey_stack
{
    // use only int typed values in here
    int count;
    int group;
} grey_stack_t;

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

    char *arg_proviso;
    enum { LTLP_ClosedSet, LTLP_Stack } proviso;

    char* dot_output;
    FILE* dot_file;
} opt = {
    .model         = NULL,
    .trc_output    = NULL,
    .trace_handle  = NULL,
    .trace_output  = NULL,
    .dlk_detect    = 0,
    .ltl_semantics = "spin",
    .ltl_type      = PINS_LTL_SPIN,
    .ltl_file      = NULL,
    .threshold     = 100000,
    .matrix        = 0,
    .write_state   = 0,
    .max           = UINT_MAX,
    .call_mode     = UseBlackBox,
    .arg_strategy  = "bfs",
    .strategy      = Strat_BFS,
    .arg_state_db  = "tree",
    .state_db      = DB_TreeDBS,
    .arg_proviso   = "closedset",
    .proviso       = LTLP_ClosedSet,
};

static si_map_entry strategies[] = {
    {"bfs", Strat_BFS},
    {"dfs", Strat_DFS},
    {"scc", Strat_SCC},
    {NULL, 0}
};

static si_map_entry db_types[]={
    {"table", DB_DBSLL},
    {"tree",  DB_TreeDBS},
    {"vset",  DB_Vset},
    {NULL, 0}
};

static si_map_entry provisos[]={
    {"closedset", LTLP_ClosedSet},
    {"stack",     LTLP_Stack},
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

            int p = linear_search (provisos, opt.arg_proviso);
            if (p < 0) {
                Warning(error, "unknown proviso %s", opt.arg_proviso);
                RTexitUsage (EXIT_FAILURE);
            }
            opt.proviso = p;
        }
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to state_db_popt");
}

static struct poptOption development_options[] = {
    { "grey", 0 , POPT_ARG_VAL , &opt.call_mode , UseGreyBox , "make use of GetTransitionsLong calls" , NULL },
    { "matrix", 0 , POPT_ARG_VAL, &opt.matrix,1,"Print the dependency matrix and quit",NULL},
    { "write-state" , 0 , POPT_ARG_VAL , &opt.write_state, 1 , "write the full state vector" , NULL },
    POPT_TABLEEND
};

static struct poptOption options[] = {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)state_db_popt , 0 , NULL , NULL },
    { "deadlock" , 'd' , POPT_ARG_VAL , &opt.dlk_detect , 1 , "detect deadlocks" , NULL },
    { "dot" , 0 , POPT_ARG_STRING , &opt.dot_output , 0 , "file to dot graph to" , "<dot output>" },
    { "trace" , 0 , POPT_ARG_STRING , &opt.trc_output , 0 , "file to write trace to" , "<lts output>" },
    { "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &opt.arg_state_db , 0 ,
      "select the data structure for storing states", "<table|tree|vset>"},
    { "strategy" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &opt.arg_strategy , 0 ,
      "select the search strategy", "<bfs|dfs|scc>"},
    { "proviso", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &opt.arg_proviso , 0 ,
      "select proviso for ltl/por", "<closedset>"},
    { "max" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &opt.max , 0 ,"maximum search depth", "<int>"},
    SPEC_POPT_OPTIONS,
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
    .visited      = 0,
    .explored     = 0,
    .depth        = 0,
    .max_depth    = 0,
    .ntransitions = 0,
};


static void *
new_string_index (void *context)
{
    (void)context;
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

typedef struct gsea_store {
    union {
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

        } table;
    };
    // couvreur wrapper
    struct {
        bitset_t         current;
        dfs_stack_t      active;
        dfs_stack_t      roots;
        array_manager_t  dfsnum_man;
        int             *dfsnum;
        int              count;
    } scc;
} gsea_store_t;

typedef union gsea_queue {
    struct {
        dfs_stack_t stack;
        dfs_stack_t grey;
        bitset_t closed_set;
        // proviso related
        struct {
            struct {
                bitset_t off_stack_set;
            } stack;
        } proviso;

        // queue/store specific callbacks
        void (*push)(gsea_state_t*, void*);
        int* (*pop)(gsea_state_t*, void*);
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

static void gsea_process(void *arg, transition_info_t *ti, int *dst);
static void gsea_foreach_open(foreach_open_cb open_cb, void *arg);
static void gsea_finished(void *arg);
static void gsea_progress(void *arg);

typedef struct gsea_context {
    // init
    void *(*init)(gsea_state_t*);

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
    void (*state_matched)(gsea_state_t*, void*);
    void (*state_process)(gsea_state_t* src, transition_info_t *ti, gsea_state_t *dst);
    int  (*state_proviso)(transition_info_t *ti, gsea_state_t *dst);

    // search for state
    int  (*goal_reached)(gsea_state_t*, void*);
    void (*goal_trace)(gsea_state_t*, void*);

    // reporting
    void (*report_progress)(void*);
    void (*report_finished)(void*);

    gsea_store_t store;
    gsea_queue_t queue;
    void *context;

    // placeholders
    void (*dlk_placeholder)(gsea_state_t*, void*);
    int  (*max_placeholder)(gsea_state_t*, void*);

    // placeholders for dfs --grey stack functions
    void (*dfs_grey_push)(gsea_state_t*, void*);
    int* (*dfs_grey_pop)(gsea_state_t*, void*);
    int  (*dfs_grey_closed)(gsea_state_t*, int is_backtrack, void*);

    // placeholders for scc couvreur wrappers
    void (*scc_open_extract)(gsea_state_t*, void*);
    int  (*scc_state_backtrack)(gsea_state_t*, void*);
    void (*scc_state_matched)(gsea_state_t*, void*);

    // placeholders for dot
    void (*dot_state_process)(gsea_state_t* src, transition_info_t *ti, gsea_state_t *dst);
    void (*dot_report_finished)(void*);

} gsea_context_t;

static gsea_context_t gc;


#ifdef DEBUG
static void
print_state(gsea_state_t *state)
{
    for(size_t i=0; i < global.N; i++) {
        printf("%d ", state->state[i]);
    }
    printf("\n");
}
#endif


/*
 * Graphviz Output
 */
static void
gsea_report_finished_dot(void *arg) {
    fputs("}\n", opt.dot_file);
    fclose(opt.dot_file);
    if (gc.dot_report_finished) gc.dot_report_finished(arg);
}

static void
gsea_process_dot(gsea_state_t *src, transition_info_t *ti, gsea_state_t *dst)
{
    char buf[global.N * 18 + 1024];
    sprintf(buf, "\"");
    for(size_t i=0; i < global.N; i++) sprintf(&buf[i*9]+1, "%.8X ", src->state[i]);
    sprintf(&buf[global.N*9], "\" -> \"");
    for(size_t i=0; i < global.N; i++) sprintf(&buf[global.N * 9 + 6 + i*9], "%.8X ", dst->state[i]);
    sprintf(&buf[global.N * 18 + 5], "\"");
    // just the index (tree/hash)
    //sprintf(buf, "%d -> %d", src->tree.tree_idx, dst->tree.tree_idx);
    //sprintf(buf, "%d -> %d", src->table.hash_idx, dst->table.hash_idx);

    fputs(buf, opt.dot_file);
    fputs("\n", opt.dot_file);
    if (gc.dot_state_process) gc.dot_state_process(src, ti, dst);
}

static void
gsea_init_dot(const char *filename)
{
    assert (filename != NULL);
    opt.dot_file = fopen(filename, "w+");
    if (!opt.dot_file) {
        Warning(info, "Failed to open DOT output file %s", filename);
    } else {
        Warning(info, "Writing DOT output to %s", filename);
        fputs("digraph {\n", opt.dot_file);
        gc.dot_state_process = gc.state_process;
        gc.dot_report_finished = gc.report_finished;
        gc.state_process = gsea_process_dot;
        gc.report_finished = gsea_report_finished_dot;
    }
}



/*
 * Trace
 */
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

static void
write_trace_next (void *arg, transition_info_t *ti, int *dst)
{
    struct write_trace_step_s*ctx=(struct write_trace_step_s*)arg;
    if(ctx->found) return;
    for(size_t i=0;i<global.N;i++) {
        if (ctx->dst_state[i]!=dst[i]) return;
    }
    ctx->found=1;
    enum_seg_seg(opt.trace_handle,0,ctx->depth-1,0,ctx->depth,ti->labels);
}

static void
write_trace_step (model_t model, int *src, int *dst, int depth)
{
    //Warning(debug,"finding edge for state %d",src_no);
    struct write_trace_step_s ctx;
    ctx.src_state=src;
    ctx.dst_state=dst;
    ctx.found=0;
    ctx.depth=depth;
    GBgetTransitionsAll(model,src,write_trace_next,&ctx);
    if (ctx.found==0) Abort ("no matching transition found");
}


/* GSEA run configurations */

/*
 * BFS (GSEA run configurations)
 */

/* TREE configuration */
static void
bfs_tree_open_insert(gsea_state_t *state, void *arg)
{
    if ((size_t)state->tree.tree_idx >= global.visited)
        global.visited++;
    else if (gc.state_matched) gc.state_matched(state, arg);
    return;
    (void)arg;
}

static void
bfs_tree_open_extract(gsea_state_t *state, void *arg)
{
    state->tree.tree_idx = global.explored;
    state->state = gc.context;
    TreeUnfold(gc.store.tree.dbs, global.explored, gc.context);
    (void)arg;
}

static void
bfs_tree_closed_insert(gsea_state_t *state, void *arg) {
    // count depth
    if (gc.store.tree.level_bound == global.explored) {
        if (RTverbosity > 1) Warning(info, "level %zu, has %zu states, explored %zu states %zu transitions", global.max_depth, global.visited - global.explored, global.explored, global.ntransitions);

        global.depth=++global.max_depth;
        gc.store.tree.level_bound = global.visited;
    }
    (void)state; (void)arg;
}

static int
bfs_tree_has_open (gsea_state_t *state, void *arg)
{
    return global.visited - global.explored;
    (void)state; (void)arg;
}

static int
bfs_tree_open_insert_condition (gsea_state_t *state, void *arg)
{
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    return ((size_t)state->tree.tree_idx >= global.explored);
    (void)arg;
}

/* VSET configuration */

typedef struct bfs_vset_arg_store {
    foreach_open_cb  open_cb;
    void            *ctx;
} bfs_vset_arg_store_t;

static void
bfs_vset_foreach_open_enum_cb (bfs_vset_arg_store_t *args, int *src)
{
    gsea_state_t s_open;
    s_open.state = src;
    args->open_cb(&s_open, args->ctx);
}

static void
bfs_vset_foreach_open(foreach_open_cb open_cb, void *arg)
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
bfs_vset_open_insert(gsea_state_t *state, void *arg)
{
    vset_add(gc.store.vset.next_set, state->state);
    global.visited++;
    (void)arg;
}

static void
bfs_vset_closed_insert(gsea_state_t *state, void *arg)
{
    vset_add(gc.store.vset.closed_set, state->state);
    (void)arg;
}

static int
bfs_vset_open(gsea_state_t *state, void *arg)
{
    return vset_member(gc.store.vset.next_set, state->state);
    (void)arg;
}

static int
bfs_vset_closed(gsea_state_t *state, void *arg)
{
    return vset_member(gc.store.vset.closed_set, state->state);
    (void)arg;
}


/*
 * DFS (GSEA run configurations)
 */

/* dfs framework configuration */
static int
dfs_goal_trace_cb(int* stack_element, void *ctx)
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
dfs_goal_trace(gsea_state_t *state, void *arg)
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

static int*
dfs_pop(gsea_state_t *state, void *arg)
{
    return dfs_stack_pop(gc.queue.filo.stack);
    (void)state;
    (void)arg;
}

static void
dfs_open_extract(gsea_state_t *state, void *arg)
{
    gc.queue.filo.stack_to_state(state, arg);
    // printf("state %d:", state->tree.tree_idx); print_state(state);
    (void)arg;
}

static int
dfs_has_open(gsea_state_t *state, void *arg) {
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
             gc.queue.filo.pop(state, arg)
            );
    return 1;
    (void)arg;
}

static int
dfs_open_insert_condition(gsea_state_t *state, void *arg)
{
    return !gc.closed(state,arg);
}

static void
dfs_state_next_all(gsea_state_t *state, void *arg)
{
    // new search depth
    global.depth++;
    // update max depth
    if (global.depth > global.max_depth) {
        global.max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, explored %zu states, %zu transitions", global.max_depth, global.explored, global.ntransitions);
    }
    // wrap with enter stack frame
    dfs_stack_enter(gc.queue.filo.stack);
    // original call
    state->count = GBgetTransitionsAll (opt.model, state->state, gsea_process, state);
    (void)arg;
}

/* dfs --grey code */
static void
dfs_grey_push(gsea_state_t *state, void *arg)
{
    grey_stack_t gs = {0,0};
    dfs_stack_push(gc.queue.filo.grey, (const int*) &gs);
    gc.dfs_grey_push(state,arg);
}

static int*
dfs_grey_pop(gsea_state_t *state, void *arg)
{
    dfs_stack_pop(gc.queue.filo.grey);
    return gc.dfs_grey_pop(state, arg);
    (void)state;
    (void)arg;
}

static int
dfs_grey_closed(gsea_state_t *state, int is_backtrack, void *arg)
{
    grey_stack_t* gs = (grey_stack_t*) dfs_stack_top(gc.queue.filo.grey);
    if (gs->group != (int)global.K) return 0;
    return gc.dfs_grey_closed(state, is_backtrack, arg);
}

static void
dfs_state_next_grey(gsea_state_t *state, void *arg)
{
    // new search depth
    global.depth++;
    // update max depth
    if (global.depth > global.max_depth) {
        global.max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, explored %zu states, %zu transitions", global.max_depth, global.explored, global.ntransitions);
    }
    // wrap with enter stack frame
    dfs_stack_enter(gc.queue.filo.stack);
    // restore this states state count
    grey_stack_t* grey = (grey_stack_t*) dfs_stack_top(gc.queue.filo.grey);
    // original call
    state->count = grey->count;
    // hack: global.explored should be gc.closed_size()
    // since the state is reexplored, it is counted more then once, so uncount again
    if (state->count != 0) global.explored--;
    for(; grey->group < (int)global.K && state->count == grey->count; grey->group++) {
        state->count += GBgetTransitionsLong (opt.model, grey->group, state->state, gsea_process, state);
    }
    // sync state count
    grey->count = state->count;
    (void)arg;
}


/* stack proviso for dfs tree */
static int
dfs_tree_state_backtrack(gsea_state_t* state, void* arg)
{ return bitset_set(gc.queue.filo.proviso.stack.off_stack_set, state->tree.tree_idx); (void)arg; }

static int
dfs_tree_state_proviso(transition_info_t* ti, gsea_state_t* state)
{ return bitset_test(gc.queue.filo.proviso.stack.off_stack_set, state->tree.tree_idx); (void)ti; }

/* dfs tree configuration */
static int
dfs_tree_stack_closed(gsea_state_t *state, int is_backtrack, void *arg)
{
    return is_backtrack || bitset_test(gc.queue.filo.closed_set, state->tree.tree_idx);
    (void)arg;
}

static void
dfs_tree_stack_peek(gsea_state_t *state, void *arg)
{
    state->tree.tree_idx = *dfs_stack_top(gc.queue.filo.stack);
    (void)arg;
}

static void
dfs_tree_state_to_stack(gsea_state_t *state, void *arg)
{
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    (void)arg;
}

static void
dfs_tree_stack_to_state(gsea_state_t *state, void *arg)
{
    state->state = gc.context;
    TreeUnfold(gc.store.tree.dbs, state->tree.tree_idx, gc.context);
    (void)arg;
}

static void
dfs_tree_stack_push(gsea_state_t *state, void *arg)
{
    dfs_stack_push(gc.queue.filo.stack, &(state->tree.tree_idx));
    (void)arg;
}

static void
dfs_tree_closed_insert (gsea_state_t *state, void *arg)
{
    bitset_set (gc.queue.filo.closed_set, state->tree.tree_idx);
    (void)arg;
}


static int
dfs_tree_closed (gsea_state_t *state, void *arg)
{
    // state is not yet serialized at this point, hence, this must be done here -> error in framework?
    dfs_tree_state_to_stack(state, arg);
    return bitset_test(gc.queue.filo.closed_set, state->tree.tree_idx); (void)state; (void)arg;
}

static int
dfs_tree_open_insert_condition (gsea_state_t *state, void *arg)
{
    return !dfs_tree_closed(state,arg);
    (void)state; (void)arg;
}

/* dfs vset configuration */
static int
dfs_vset_closed (gsea_state_t *state, void *arg)
{
    return vset_member (gc.store.vset.closed_set, state->state);
    (void)arg;
}

static int
dfs_vset_stack_closed(gsea_state_t *state, int is_backtrack, void *arg)
{
    return is_backtrack ||dfs_vset_closed(state, arg);
}

static void
dfs_vset_stack_peek(gsea_state_t *state, void *arg)
{
    state->state = dfs_stack_top(gc.queue.filo.stack);
    (void)arg;
}

static void
dfs_vset_state_to_stack (gsea_state_t *state, void *arg)
{ (void)state; (void)arg; }

static void
dfs_vset_stack_to_state (gsea_state_t *state, void *arg)
{ (void)state; (void)arg; }


static void
dfs_vset_stack_push(gsea_state_t *state, void *arg)
{
    dfs_stack_push(gc.queue.filo.stack, state->state);
    (void)arg;
}

static void
dfs_vset_closed_insert(gsea_state_t *state, void *arg) {
    vset_add(gc.store.vset.closed_set, state->state);
    (void)arg;
}

static int
dfs_vset_open_insert_condition (gsea_state_t *state, void *arg)
{
    return !dfs_vset_closed (state, arg);
    (void)state; (void)arg;
}



/* stack proviso for dfs table */
static int
dfs_table_state_backtrack(gsea_state_t* state, void* arg)
{ return bitset_set(gc.queue.filo.proviso.stack.off_stack_set, state->table.hash_idx); (void)arg; }

static int
dfs_table_state_proviso(transition_info_t* ti, gsea_state_t* state)
{ return bitset_test(gc.queue.filo.proviso.stack.off_stack_set, state->table.hash_idx); (void)ti; }

/* dfs table configuration */
static int
dfs_table_stack_closed (gsea_state_t *state, int is_backtrack, void *arg)
{
    return is_backtrack
        || bitset_test (gc.queue.filo.closed_set, state->table.hash_idx);
    (void)arg;
}

static void
dfs_table_stack_peek (gsea_state_t *state, void *arg)
{
    state->table.hash_idx = *dfs_stack_top (gc.queue.filo.stack);
    (void)arg;
}

static void
dfs_table_state_to_stack(gsea_state_t *state, void *arg)
{
    DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx));
    (void)arg;
}

static void
dfs_table_stack_to_state(gsea_state_t *state, void *arg)
{
    int hash;
    state->state = DBSLLget(gc.store.table.dbs, state->table.hash_idx, &hash);
    (void)arg;
}

static void dfs_table_stack_push(gsea_state_t *state, void *arg)
{
    dfs_stack_push(gc.queue.filo.stack, &(state->table.hash_idx));
    (void)arg;
}


static void
dfs_table_closed_insert (gsea_state_t *state, void *arg)
{
    bitset_set (gc.queue.filo.closed_set, state->table.hash_idx);
    (void)arg;
}

static int
dfs_table_closed (gsea_state_t *state, void *arg)
{
    // state is not yet serialized at this point, hence, this must be done 
    // here -> error in framework
    dfs_table_state_to_stack (state, arg);
    return bitset_test (gc.queue.filo.closed_set, state->table.hash_idx);
    (void)state; (void)arg;
}

static int
dfs_table_open_insert_condition (gsea_state_t *state, void *arg)
{
    return !dfs_table_closed (state, arg);
    (void)state; (void)arg;
}



/*
 * SCC
 */

/* scc table configuration */
static void
scc_open_extract(gsea_state_t *state, void *arg)
{
    gc.scc_open_extract(state, arg);
    gc.store.scc.count++;
    //Warning(info, "state %zu has dfs_num %d", state->tree.tree_idx, gc.store.scc.count);
    ensure_access(gc.store.scc.dfsnum_man, state->tree.tree_idx);
    gc.store.scc.dfsnum[state->tree.tree_idx] = gc.store.scc.count;
    bitset_set(gc.store.scc.current, state->tree.tree_idx);

    //if (accepting state) // there is precisely one accept set (NO GBA support)
    // fiddle accepting/not accepting in as single a bit
    int r = state->tree.tree_idx << 1;
    if (GBbuchiIsAccepting(opt.model, state->state)) {
        r |= 1;
    }

    dfs_stack_push(gc.store.scc.roots, &r);

    dfs_stack_push(gc.store.scc.active, &state->tree.tree_idx);

    (void)arg;
}

static int
scc_state_backtrack(gsea_state_t *state, void *arg)
{
    //Warning(info, "backtracked state %d, dfsnum %d", state->tree.tree_idx, gc.store.scc.dfsnum[state->tree.tree_idx]);
    if (((*dfs_stack_top(gc.store.scc.roots))>>1) == state->tree.tree_idx) {
        dfs_stack_pop(gc.store.scc.roots);
        int u;
        do {
            u = *dfs_stack_pop(gc.store.scc.active);
            bitset_clear(gc.store.scc.current, u);
        } while (u != state->tree.tree_idx);
    }
    if (gc.scc_state_backtrack) return gc.scc_state_backtrack(state, arg);
    return 1;
}

static void
scc_state_matched(gsea_state_t *state, void *arg)
{
    if (bitset_test(gc.store.scc.current, state->tree.tree_idx)) {
        int b = 0, r;
        do {
            r = *dfs_stack_pop(gc.store.scc.roots);
            b |= (r&1);
            if (b) {
                opt.threshold = global.visited-1;
                gc.report_progress(arg);
                Warning(info, "accepting cycle found!");
                if (opt.trc_output && gc.goal_trace) {
                    gc.queue.filo.push(state, arg);
                    gc.goal_trace(state, arg);
                }
                Fatal(1, info, "exiting now");
            }
        } while (gc.store.scc.dfsnum[r>>1] > gc.store.scc.dfsnum[state->tree.tree_idx]);
        dfs_stack_push(gc.store.scc.roots, &r);
    }
    if (gc.scc_state_matched) return gc.scc_state_matched(state, arg);
}


/*
 * GSEA setup
 */

static void
error_state_arg(gsea_state_t *state, void *arg)
{
    Abort ("unimplemented functionality in gsea configuration");
    (void)state; (void)arg;
}

static void *
gsea_init_default(gsea_state_t *state)
{
    if (gc.open_insert_condition(state, NULL))
        gc.open_insert(state, NULL);
    return NULL;
}

static int
gsea_open_insert_condition_default(gsea_state_t *state, void *arg)
{
    return (!gc.closed(state, arg) && !gc.open(state, arg));
}

static void
gsea_state_next_all_default(gsea_state_t *state, void *arg)
{
    state->count = GBgetTransitionsAll (opt.model, state->state, gsea_process, state);
    (void)arg;
}

static void
gsea_state_next_grey_default(gsea_state_t *state, void *arg)
{
    state->count = 0;
    for (size_t i = 0; i < global.K; i++) {
        state->count += GBgetTransitionsLong (opt.model, i, state->state, gsea_process, state);
    }
    (void)arg;
}

static void
gsea_dlk_default(gsea_state_t *state, void *arg)
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
gsea_max_wrapper(gsea_state_t *state, void *arg)
{
    // (depth < max depth) with chain original condition
    return (global.depth < opt.max) && gc.max_placeholder(state,arg);
}



static void
gsea_goal_trace_default(gsea_state_t *state, void *arg)
{
    Abort ("goal state reached");
    (void)state; (void)arg;
}

static void
gsea_setup_default()
{
    // general setup
    if (!gc.init)                       gc.init = gsea_init_default;
    if (!gc.foreach_open)               gc.foreach_open = gsea_foreach_open;
    if (!gc.open_insert_condition)      gc.open_insert_condition = gsea_open_insert_condition_default;
    if (!gc.report_progress)            gc.report_progress = gsea_progress;
    if (!gc.report_finished)            gc.report_finished = gsea_finished;

    switch (opt.strategy) {
    case Strat_BFS:
        // check required functions
        if (!gc.goal_trace)             gc.goal_trace = gsea_goal_trace_default;
        if (!gc.has_open)               gc.has_open = (gsea_int) error_state_arg;
        if (!gc.open_insert)            gc.open_insert = error_state_arg;
        if (!gc.open_delete)            gc.open_delete = error_state_arg;
        if (!gc.open)                   gc.open = (gsea_int) error_state_arg;
        if (!gc.open_extract)           gc.open_extract = error_state_arg;
        if (!gc.closed_insert)          gc.closed_insert = error_state_arg;
        if (!gc.closed_delete)          gc.closed_delete = error_state_arg;
        if (!gc.closed)                 gc.closed = (gsea_int) error_state_arg;
        // setup standard bfs/tree configuration

        if (opt.call_mode == UseGreyBox) {
            if (!gc.state_next)         gc.state_next = gsea_state_next_grey_default;
        } else {
            if (!gc.state_next)         gc.state_next = gsea_state_next_all_default;
        }
        break;
    case Strat_SCC:
        // exception for Strat_SCC, only works in combination with ltl formula
        if (GBgetAcceptingStateLabelIndex(opt.model) < 0) {
            Abort("NDFS search only works in combination with an accepting state label"
                  " (see LTL options)");
        }
    case Strat_DFS:
        // init filo  queue
        if (!gc.queue.filo.push)        Abort ("GSEA push() not implemented");
        if (!gc.queue.filo.pop)         gc.queue.filo.pop = dfs_pop;
        if (!gc.queue.filo.peek)        Abort ("GSEA peek() not implemented");
        if (!gc.queue.filo.stack_to_state)  Abort ("GSEA stack_to_state() not implemented");
        if (!gc.queue.filo.state_to_stack)  Abort ("GSEA state_to_stack() not implemented");
        if (!gc.queue.filo.closed)      Abort ("GSEA stack closed() not implemented");

        if (opt.call_mode == UseGreyBox) {
            // wrap stack functions
            gc.dfs_grey_push = gc.queue.filo.push;
            gc.dfs_grey_pop = gc.queue.filo.pop;
            gc.dfs_grey_closed = gc.queue.filo.closed;
            gc.queue.filo.push = dfs_grey_push;
            gc.queue.filo.pop  = dfs_grey_pop;
            gc.queue.filo.closed = dfs_grey_closed;
            // modify next state function
            if (gc.state_next)          Abort ("state next filled before use of GSEA dfs_state_next_grey()");
            gc.state_next = dfs_state_next_grey;
            // init extra stack
            gc.queue.filo.grey = dfs_stack_create(sizeof(grey_stack_t)/sizeof(int));
        } else {
            if (!gc.state_next)             gc.state_next = dfs_state_next_all;
        }

        // setup dfs framework
        if (!gc.open_insert_condition)  gc.open_insert_condition = dfs_open_insert_condition;
        if (!gc.open_insert)            gc.open_insert = gc.queue.filo.push;
        if (!gc.has_open)               gc.has_open = dfs_has_open;
        if (!gc.open_extract)           gc.open_extract = dfs_open_extract;
        if (!gc.goal_trace)             gc.goal_trace = dfs_goal_trace;

        // scc specifics
        if (opt.strategy == Strat_DFS) break;

        /* scc wrapper functions */
        gc.scc_open_extract = gc.open_extract;
        gc.scc_state_backtrack = gc.state_backtrack;
        gc.scc_state_matched = gc.state_matched;

        gc.open_extract = scc_open_extract;
        gc.state_backtrack = scc_state_backtrack;
        gc.state_matched = scc_state_matched;

        // scc init
        gc.store.scc.current = bitset_create (128,128);
        gc.store.scc.active = dfs_stack_create (1);
        gc.store.scc.roots = dfs_stack_create (1);
        gc.store.scc.dfsnum_man = create_manager(65536);
        ADD_ARRAY(gc.store.scc.dfsnum_man, gc.store.scc.dfsnum, int);
        gc.store.scc.count = 0;
        break;
    }

    // options setup

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

    // output dot
    if (opt.dot_output) gsea_init_dot(opt.dot_output);
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
            Abort ("unimplemented combination --strategy=%s, --state=%s", opt.arg_strategy, opt.arg_state_db );
        }

        // proviso: doens't work here
        if (opt.proviso != LTLP_ClosedSet)
            Abort("proviso does not work for bfs, use --proviso=closedset");

        break;

    case Strat_SCC:
    case Strat_DFS:
        switch (opt.state_db) {
        case DB_TreeDBS:
            // setup dfs/tree configuration
            gc.closed_insert = dfs_tree_closed_insert;
            gc.closed = dfs_tree_closed;
            gc.open_insert_condition = dfs_tree_open_insert_condition;

            gc.queue.filo.closed = dfs_tree_stack_closed;
            gc.queue.filo.push = dfs_tree_stack_push;
            gc.queue.filo.peek = dfs_tree_stack_peek;
            gc.queue.filo.stack_to_state = dfs_tree_stack_to_state;
            gc.queue.filo.state_to_stack = dfs_tree_state_to_stack;

            gc.store.tree.dbs = TreeDBScreate(global.N);
            gc.queue.filo.stack = dfs_stack_create(1);
            gc.queue.filo.closed_set = bitset_create(128,128);
            gc.context = RTmalloc(sizeof(int) * global.N);

            // proviso: dfs tree specific
            switch (opt.proviso) {
                case LTLP_Stack:
                    gc.queue.filo.proviso.stack.off_stack_set = bitset_create(128,128);
                    gc.state_backtrack = dfs_tree_state_backtrack;
                    gc.state_proviso = dfs_tree_state_proviso;
                    break;
                default:
                    break;
            }
            break;
        case DB_Vset:
            // dfs/vset configuration
            gc.closed_insert = dfs_vset_closed_insert;
            gc.closed = dfs_vset_closed;
            gc.open_insert_condition = dfs_vset_open_insert_condition;

            gc.queue.filo.closed = dfs_vset_stack_closed;
            gc.queue.filo.push = dfs_vset_stack_push;
            gc.queue.filo.peek = dfs_vset_stack_peek;
            gc.queue.filo.stack_to_state = dfs_vset_stack_to_state;
            gc.queue.filo.state_to_stack = dfs_vset_state_to_stack;

            gc.context = RTmalloc(sizeof(int) * global.N);
            gc.store.vset.domain = vdom_create_default (global.N);
            gc.store.vset.closed_set = vset_create(gc.store.vset.domain, 0, NULL);
            gc.store.vset.next_set = vset_create(gc.store.vset.domain, 0, NULL);
            gc.store.vset.current_set = vset_create(gc.store.vset.domain, 0, NULL);
            gc.queue.filo.stack = dfs_stack_create(global.N);

            // proviso: doens't work here
            if (opt.proviso != LTLP_ClosedSet)
                Abort("proviso not implemented for dfs/vset combination");

            break;
        case DB_DBSLL:
            gc.closed_insert = dfs_table_closed_insert;
            gc.closed = dfs_table_closed;
            gc.open_insert_condition = dfs_table_open_insert_condition;

            gc.queue.filo.closed = dfs_table_stack_closed;
            gc.queue.filo.push = dfs_table_stack_push;
            gc.queue.filo.peek = dfs_table_stack_peek;
            gc.queue.filo.stack_to_state = dfs_table_stack_to_state;
            gc.queue.filo.state_to_stack = dfs_table_state_to_stack;

            gc.context = RTmalloc(sizeof(int) * global.N);
            gc.store.table.dbs = DBSLLcreate(global.N);
            gc.queue.filo.closed_set = bitset_create(128,128);
            gc.queue.filo.stack = dfs_stack_create(1);

            // proviso: dfs table specific
            switch (opt.proviso) {
                case LTLP_Stack:
                    gc.queue.filo.proviso.stack.off_stack_set = bitset_create(128,128);
                    gc.state_backtrack = dfs_table_state_backtrack;
                    gc.state_proviso = dfs_table_state_proviso;
                    break;
                default:
                    break;
            }
            break;

        default:
            Abort ("unimplemented combination --strategy=%s, --state=%s", opt.arg_strategy, opt.arg_state_db );
        }
        break;

    default:
        Abort ("unimplemented strategy");
    }
}


/*
 * GSEA Algorithm
 */

static void gsea_foreach_open_cb(gsea_state_t *s_open, void *arg);

static void
gsea_search(int* src)
{
    gsea_state_t s0;
    s0.state = src;
    void *ctx = gc.init(&s0);

    // while open states, process
    gc.foreach_open(gsea_foreach_open_cb, ctx);

    // give the result
    gc.report_finished(ctx);
}

static void
gsea_foreach_open(foreach_open_cb open_cb, void *arg)
{
    gsea_state_t s_open;
    while(gc.has_open(&s_open, arg)) {
        gc.open_extract(&s_open, arg);
        open_cb(&s_open, arg);
    }
}

static void
gsea_foreach_open_cb(gsea_state_t *s_open, void *arg)
{
    // insert in closed set
    gc.closed_insert(s_open, arg);

    // count new explored state
    // this should hold: global.explored == gc.closed_size()
    // maybe nicer to count this in closed_insert function
    // would work with --grey too
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
gsea_process(void *arg, transition_info_t *ti, int *dst)
{
    gsea_state_t s_next;
    s_next.state = dst;
    // this should be in here.
    if (gc.open_insert_condition(&s_next, arg)) {
        ti->por_proviso = 1;
        gc.open_insert(&s_next, arg);
    } else {
        ti->por_proviso = gc.state_proviso ? gc.state_proviso(ti, &s_next) : 0;
        if (gc.state_matched) gc.state_matched(&s_next, arg);
    }
    global.ntransitions++;
    if (gc.state_process) gc.state_process((gsea_state_t*)arg, ti, &s_next);
    (void)ti;
}

static void
gsea_progress(void *arg) {
    if (RTverbosity < 1 || global.explored < opt.threshold)
        return;
    if (!cas (&opt.threshold, opt.threshold, opt.threshold << 1))
        return;
    Warning (info, "explored %zu levels ~%zu states ~%zu transitions",
             global.max_depth, global.explored, global.ntransitions);
    (void)arg;
}

static void
gsea_finished(void *arg) {
    Warning (info, "state space %zu levels, %zu states %zu transitions",
             global.max_depth, global.explored, global.ntransitions);
    (void)arg;
}



int
main (int argc, char *argv[])
{
    char *files[2];
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

    gsea_setup();
    gsea_setup_default();
    gsea_search(src);

    exit(EXIT_SUCCESS);
}
