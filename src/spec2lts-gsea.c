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


static lts_enum_cb_t output_handle=NULL;

static model_t model=NULL;
static char* trc_output=NULL;
static int dlk_detect=0;
static int dlk_all_detect=0;
static FILE* dlk_all_file=NULL;
static int dlk_count=0;
static lts_enum_cb_t trace_handle=NULL;
static lts_output_t trace_output=NULL;

static array_manager_t state_man=NULL;
static uint32_t *parent_ofs=NULL;

static size_t       threshold = 100000;

static int write_lts;
static int matrix=0;
static int write_state=0;
static size_t max = UINT_MAX;

typedef enum { UseGreyBox , UseBlackBox } mode_t;
static mode_t call_mode=UseBlackBox;

static char *arg_strategy = "bfs";
static enum { Strat_BFS, Strat_DFS, Strat_SCC } strategy = Strat_BFS;
static char *arg_state_db = "tree";
static enum { DB_DBSLL, DB_TreeDBS, DB_Vset } state_db = DB_TreeDBS;

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
               const struct poptOption *opt, const char *arg, void *data)
{
    (void)con; (void)opt; (void)arg; (void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST: {
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
	{ "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_state_db , 0 ,
		"select the data structure for storing states", "<table|tree|vset>"},
	{ "strategy" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &arg_strategy , 0 ,
		"select the search strategy", "<bfs|dfs>"},
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
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, development_options , 0 , "Development options" , NULL },
	POPT_TABLEEND
};


static size_t N;
static size_t K;
static size_t state_labels;
static size_t edge_labels;
static size_t max_depth=0;
static size_t depth=0;
static size_t visited=0;
static size_t explored=0;
static size_t ntransitions = 0;

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
        vset_t visited_set;
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
        union {
            bitset_t open;
            bitset_t closed;
        };
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
    void* (*gsea_init)(gsea_state_t*);

    // foreach open function
    void (*foreach_open)( foreach_open_cb, void*);

    // open insert
    int (*has_open)(void*);
    int (*open_insert_condition)(gsea_state_t*, void*);

    // open set
    void (*open_insert)(gsea_state_t*, void*);
    void (*open_delete)(gsea_state_t*, void*);
    int  (*open)(gsea_state_t*, void*);
    void (*open_extract)(gsea_state_t*, void*);
    int  (*open_size)(void*);

    // closed set
    void (*closed_insert)(gsea_state_t*, void*);
    void (*closed_delete)(gsea_state_t*, void*);
    int  (*closed)(gsea_state_t*, void*);
    void (*closed_extract)(gsea_state_t*, void*);
    int  (*closed_size)(void*);

    // state info
    void (*pre_state_next)(gsea_state_t*, void*);
    void (*post_state_next)(gsea_state_t*, void*);
    void (*state_next)(gsea_state_t*, void*);

    // search for state
    int  (*goal_reached)(gsea_state_t*, void*);
    int  (*goal_trace)(gsea_state_t*, void*);

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
    for(size_t i=0; i < N; i++) {
        printf("%d ", state->state[i]);
    } printf("\n");
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
    if ((size_t)state->tree.tree_idx >= visited)
        visited++;
    return;
    (void)arg;
}

static void
bfs_tree_open_extract(gsea_state_t* state, void* arg)
{
    state->tree.tree_idx = explored;
    state->state = gc.context;
    TreeUnfold(gc.store.tree.dbs, explored, gc.context);
    return;
    (void)arg;
}

static void
bfs_tree_closed_insert(gsea_state_t* state, void* arg) {
    // count depth
    if (gc.store.tree.level_bound == explored) {
        if (RTverbosity > 1) Warning(info, "level %zu, has %zu states, explored %zu states %zu transitions", max_depth, visited - explored, explored, ntransitions);

        depth=++max_depth;
        gc.store.tree.level_bound = visited;
    }
    explored++;
    return; (void)state; (void)arg;
}
static int bfs_tree_open_size(void* arg) { return visited - explored; (void)arg; }
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
        if (RTverbosity > 1) Warning(info, "level %zu, has %zu states, explored %zu states %zu transitions", max_depth, visited - explored, explored, ntransitions);
        vset_enum(gc.store.vset.current_set, (void(*)(void*,int*)) bfs_vset_foreach_open_enum_cb, &args);
        depth=++max_depth;
    }
}

static void
bfs_vset_open_insert(gsea_state_t* state, void* arg)
{
    vset_add(gc.store.vset.next_set, state->state);
    visited++;
    return;
    (void)arg;
}

static void
bfs_vset_closed_insert(gsea_state_t* state, void* arg)
{
    vset_add(gc.store.vset.visited_set, state->state);
    explored++;
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
    return vset_member(gc.store.vset.visited_set, state->state);
    (void)arg;
}







/********************
 *   __   ___  __   *
 *  |  \ |__  /__`  *
 *  |__/ |    .__/  *
 ********************/


/* dfs tree configuration */
static void
dfs_tree_open_insert(gsea_state_t* state, void* arg)
{
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    dfs_stack_push(gc.queue.filo.stack, &(state->tree.tree_idx));
    if ((size_t)state->tree.tree_idx >= visited)
        visited++;

    return;
    (void)arg;
}

static void
dfs_tree_open_extract(gsea_state_t* state, void* arg)
{
    // queue.get(state, arg)
    int* idx = NULL;
    do {
        // detect backtrack
        if (dfs_stack_frame_size(gc.queue.filo.stack) == 0) {
            // gc.backtrack(state, arg);
            dfs_stack_leave(gc.queue.filo.stack);
            // pop, because the backtrack state must be closed (except if reopened, which is unsupported)
            idx = dfs_stack_pop(gc.queue.filo.stack);
            // less depth
            depth--;
            //printf("backtrack %d:\n", *idx);
            idx = NULL;
        } else {
            idx = dfs_stack_top(gc.queue.filo.stack);
            if (bitset_test(gc.queue.filo.closed, *idx)) {
                //printf("pop %d\n",*idx);
                dfs_stack_pop(gc.queue.filo.stack);
                idx = NULL;
            }
        }
    } while (idx == NULL);
    state->tree.tree_idx = *idx;
    state->state = gc.context;
    // stote.get(state, arg)
    TreeUnfold(gc.store.tree.dbs, *idx, gc.context);

    // update max depth
    if (dfs_stack_nframes(gc.queue.filo.stack) > max_depth) {
        max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, visited %zu states, %zu transitions", max_depth, visited, ntransitions);
    }
    //printf("state %d:", state->tree.tree_idx); print_state(state);
    return;
    (void)arg;
}

static void dfs_tree_closed_insert(gsea_state_t* state, void* arg) { explored++; bitset_set(gc.queue.filo.closed, state->tree.tree_idx); return; (void)arg;}
// the visited - explored condition prevents backtracking from being called
// problem, stack can be filled without open states..
// solution, has_open should be adapted for this, to backtrack to the latest state
static int dfs_tree_open_size(void* arg) { return visited - explored; (void)arg; }


static int dfs_tree_open(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }
static int dfs_tree_closed(gsea_state_t* state, void* arg)
{
    // state is not yet serialized at this point, hence, this must be done here -> error in framework
    state->tree.tree_idx = TreeFold(gc.store.tree.dbs, state->state);
    return bitset_test(gc.queue.filo.closed, state->tree.tree_idx); (void)state; (void)arg;
}
//static int dfs_tree_closed(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }
static int dfs_tree_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_tree_closed(state,arg); (void)state; (void)arg; }

static void
dfs_tree_state_next(gsea_state_t* state, void* arg)
{
    // wrap with enter stack frame
    depth++;
    dfs_stack_enter(gc.queue.filo.stack);
    // original call (call old.state_next for wrapping with grey)
    state->count = GBgetTransitionsAll (model, state->state, gsea_process, state);
    return;
    (void)arg;
}

/* dfs vset configuration */
static int dfs_vset_closed(gsea_state_t* state, void* arg) { return vset_member(gc.store.vset.visited_set, state->state); (void)arg; }

static void
dfs_vset_open_insert(gsea_state_t* state, void* arg)
{
    // this is not necessary, but do this for now
    // hmm, open insert conditoin should forbid this situation
    if (!dfs_vset_closed(state, arg) && !vset_member(gc.store.vset.next_set, state->state) && !vset_member(gc.store.vset.current_set, state->state))
        visited++;

    dfs_stack_push(gc.queue.filo.stack, state->state);
    vset_add(gc.store.vset.current_set, state->state);
    return;
    (void)arg;
}

static void dfs_vset_open_extract(gsea_state_t* state, void* arg)
{
    // queue.get(state, arg)
    do {
        // detect backtrack
        if (dfs_stack_frame_size(gc.queue.filo.stack) == 0) {
            // gc.backtrack(state, arg);
            dfs_stack_leave(gc.queue.filo.stack);
            // less depth
            depth--;
            // pop, because the backtrack state must be closed (except if reopened, which is unsupported)
            state->state = dfs_stack_pop(gc.queue.filo.stack);
        }
        // as long as visited - explored > 0, we should still have an open state on the stack
    //    if (dfs_stack_size(gc.queue.filo.stack) > 0) // for now, just check
        state->state = dfs_stack_top(gc.queue.filo.stack);
    //    if (state->state == NULL) Fatal(1, error, "null");
    } while (state->state == NULL || (dfs_vset_closed(state, arg) && dfs_stack_pop(gc.queue.filo.stack)));

    // update max depth
    if (dfs_stack_nframes(gc.queue.filo.stack) > max_depth) {
        max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, visited %zu states, %zu transitions", max_depth, visited, ntransitions);
    }
    //printf("state %d:", state->tree.tree_idx); print_state(state);
    return;
    (void)arg;
}

static void
dfs_vset_closed_insert(gsea_state_t* state, void* arg) {
    vset_add(gc.store.vset.visited_set, state->state);
    explored++;
    return;
    (void)arg;
}
// the visited - explored condition prevents backtracking from being called
// problem, stack can be filled without open states..
// solution, has_open should be adapted for this, to backtrack to the latest state
static int dfs_vset_open_size(void* arg) { return visited - explored; (void)arg;}

static int dfs_vset_open(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }


static int dfs_vset_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_vset_closed(state,arg); (void)state; (void)arg; }

static void
dfs_vset_state_next(gsea_state_t* state, void* arg)
{
    // depth
    depth++;
    // wrap with enter stack frame
    dfs_stack_enter(gc.queue.filo.stack);
    // original call (call old.state_next for wrapping with grey)
    state->count = GBgetTransitionsAll (model, state->state, gsea_process, state);
    return;
    (void)arg;
}



/* dfs table configuration */
static void dfs_table_open_insert(gsea_state_t* state, void* arg)
{
    if (!DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx))) {
        visited++;
    }

    dfs_stack_push(gc.queue.filo.stack, &(state->table.hash_idx));
    return;
    (void)arg;
}

static void dfs_table_open_extract(gsea_state_t* state, void* arg)
{
    // queue.get(state, arg)
    int* idx = NULL;
    do {
        // detect backtrack
        if (dfs_stack_frame_size(gc.queue.filo.stack) == 0) {
            // gc.backtrack(state, arg);
            dfs_stack_leave(gc.queue.filo.stack);
            // less depth
            depth--;
            // pop, because the backtrack state must be closed (except if reopened, which is unsupported)
            idx = dfs_stack_pop(gc.queue.filo.stack);
            //printf("backtrack %d:\n", *idx);
            idx = NULL;
        } else {
            idx = dfs_stack_top(gc.queue.filo.stack);
            if (bitset_test(gc.queue.filo.closed, *idx)) {
                //printf("pop %d\n",*idx);
                dfs_stack_pop(gc.queue.filo.stack);
                idx = NULL;
            }
        }
    } while (idx == NULL);
    state->table.hash_idx = *idx;
    // index is known
    int hash;

    state->state = DBSLLget(gc.store.table.dbs, *idx, &hash);

    // update max depth
    if (dfs_stack_nframes(gc.queue.filo.stack) > max_depth) {
        max_depth++;
        if (RTverbosity > 1) Warning(info, "new level %zu, visited %zu states, %zu transitions", max_depth, visited, ntransitions);
    }
    //printf("state %d:", state->tree.tree_idx); print_state(state);
    return;
    (void)arg;
}

static void dfs_table_closed_insert(gsea_state_t* state, void* arg) { explored++; bitset_set(gc.queue.filo.closed, state->table.hash_idx); (void)arg;}
// the visited - explored condition prevents backtracking from being called
// problem, stack can be filled without open states..
// solution, has_open should be adapted for this, to backtrack to the latest state
static int dfs_table_open_size(void* arg) { return visited - explored; (void)arg;}

static int dfs_table_open(gsea_state_t* state, void* arg) { return 0; (void)state; (void)arg; }

static int dfs_table_closed(gsea_state_t* state, void* arg) {
    // state is not yet serialized at this point, hence, this must be done here -> error in framework
    if (!DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx))) {
        visited++;
    }
    return bitset_test(gc.queue.filo.closed, state->table.hash_idx); (void)state; (void)arg;
}

static int dfs_table_open_insert_condition(gsea_state_t* state, void* arg) { return !dfs_table_closed(state,arg); (void)state; (void)arg; }

static void
dfs_table_state_next(gsea_state_t* state, void* arg)
{
    // depth
    depth++;
    // wrap with enter stack frame
    dfs_stack_enter(gc.queue.filo.stack);
    // original call (call old.state_next for wrapping with grey)
    state->count = GBgetTransitionsAll (model, state->state, gsea_process, state);
    return;
    (void)arg;
}




/********************
 *   __   __   __   *
 *  /__` /  ` /  `  *
 *  .__/ \__, \__,  *
 ********************/

/* scc table configuration */
static void scc_table_open_insert(gsea_state_t* state, void* arg)
{
    if (!DBSLLlookup_ret(gc.store.table.dbs, state->state, &(state->table.hash_idx))) {
        visited++;
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
        if (bitset_test(gc.queue.filo.closed, *idx)) {
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

static void scc_table_closed_insert(gsea_state_t* state, void* arg) { explored++; bitset_set(gc.queue.filo.closed, state->table.hash_idx); (void)arg;}
static int scc_table_open_size(void* arg) { return visited - explored; (void)arg; }

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
gsea_has_open_default(void* arg) {
    return (gc.open_size(arg) != 0);
}

static int
gsea_open_insert_condition_default(gsea_state_t* state, void* arg) {
    return (!gc.closed(state, arg) && !gc.open(state, arg));
}

static void
gsea_state_next_default(gsea_state_t* state, void* arg)
{
    state->count = GBgetTransitionsAll (model, state->state, gsea_process, state);
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
        //gc.goal_trace(..);
        threshold = visited-1;
        gc.report_progress(arg);
        Warning(info, "deadlock detected");
        Fatal(1, info, "exiting now");
    }
}

static int
gsea_max_wrapper(gsea_state_t* state, void* arg)
{
    // (depth < max_depth) with chain original condition
    return (depth < max) && gc.max_placeholder(state,arg);
}



static int
gsea_goal_trace_default(gsea_state_t* state, void* arg)
{
    Fatal(1, error, "goal state reached");
    return 0;
    (void)state;
    (void)arg;
}

static void
gsea_setup_default()
{
        // setup standard bfs/tree configuration
        gc.gsea_init = gsea_init_default;
        gc.foreach_open = gsea_foreach_open;
        gc.has_open = gsea_has_open_default;
        gc.open_insert_condition = gsea_open_insert_condition_default;
        gc.open_insert = error_state_arg;
        gc.open_delete = error_state_arg;
        gc.open = (gsea_int) error_state_arg;
        gc.open_extract = error_state_arg;
        gc.open_size = (int(*)(void*)) error_arg;
        gc.closed_insert = error_state_arg;
        gc.closed_delete = error_state_arg;
        gc.closed = (gsea_int) error_state_arg;
        gc.closed_size = (int(*)(void*)) error_arg;
        gc.pre_state_next = NULL;
        gc.state_next = gsea_state_next_default;
        gc.post_state_next = NULL;
        gc.goal_reached = (gsea_int) error_state_arg;
        gc.goal_trace = gsea_goal_trace_default;
        gc.report_progress = gsea_progress;
        gc.report_finished = gsea_finished;
}

static void
gsea_setup()
{
    switch(strategy) {

    case Strat_BFS:
        switch (state_db) {
            case DB_TreeDBS:
                // setup standard bfs/tree configuration
                gc.open_insert_condition = bfs_tree_open_insert_condition;
                gc.open_insert = bfs_tree_open_insert;
                gc.open_extract = bfs_tree_open_extract;
                gc.has_open = bfs_tree_open_size;
                gc.open_size = bfs_tree_open_size;
                gc.closed_insert = bfs_tree_closed_insert;

                gc.store.tree.dbs = TreeDBScreate(N);
                gc.store.tree.level_bound = 0;
                gc.context = RTmalloc(sizeof(int) * N);
                break;
            case DB_Vset:
                // setup standard bfs/vset configuration
                gc.foreach_open = bfs_vset_foreach_open;
                gc.open_insert = bfs_vset_open_insert;
                gc.open = bfs_vset_open;
                gc.closed_insert = bfs_vset_closed_insert;
                gc.closed = bfs_vset_closed;

                gc.context = RTmalloc(sizeof(int) * N);
                gc.store.vset.domain = vdom_create_default (N);
                gc.store.vset.visited_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.next_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.current_set = vset_create(gc.store.vset.domain, 0, NULL);
                break;
            default:
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", arg_strategy, arg_state_db );
        }
        break;

    case Strat_DFS:
        switch (state_db) {
            case DB_TreeDBS:
                // setup dfs/tree configuration
                gc.open_insert = dfs_tree_open_insert;
                gc.open_extract = dfs_tree_open_extract;
                gc.open = dfs_tree_open;
                gc.open_size = dfs_tree_open_size;
                gc.closed_insert = dfs_tree_closed_insert;
                gc.closed = dfs_tree_closed;
                gc.open_insert_condition = dfs_tree_open_insert_condition;
                gc.state_next = dfs_tree_state_next;

                gc.store.tree.dbs = TreeDBScreate(N);
                //gc.queue.filo.open = bitset_create(128,128);
                gc.queue.filo.closed = bitset_create(128,128);
                gc.queue.filo.stack = dfs_stack_create(1);
                gc.context = RTmalloc(sizeof(int) * N);
                break;
            case DB_Vset:
                // dfs/vset configuration
                gc.open_insert = dfs_vset_open_insert;
                gc.open_extract = dfs_vset_open_extract;
                gc.open = dfs_vset_open;
                gc.open_size = dfs_vset_open_size;
                gc.closed_insert = dfs_vset_closed_insert;
                gc.closed = dfs_vset_closed;
                gc.open_insert_condition = dfs_vset_open_insert_condition;
                gc.state_next = dfs_vset_state_next;

                gc.context = RTmalloc(sizeof(int) * N);
                gc.store.vset.domain = vdom_create_default (N);
                // this should actually be closed_set
                gc.store.vset.visited_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.next_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.store.vset.current_set = vset_create(gc.store.vset.domain, 0, NULL);
                gc.queue.filo.stack = dfs_stack_create(N);
                break;
            case DB_DBSLL:
                gc.open_insert = dfs_table_open_insert;
                gc.open_extract = dfs_table_open_extract;
                gc.open = dfs_table_open;
                gc.open_size = dfs_table_open_size;
                gc.closed_insert = dfs_table_closed_insert;
                gc.closed = dfs_table_closed;
                gc.open_insert_condition = dfs_table_open_insert_condition;
                gc.state_next = dfs_table_state_next;

                gc.context = RTmalloc(sizeof(int) * N);
                gc.store.table.dbs = DBSLLcreate(N);
                gc.queue.filo.closed = bitset_create(128,128);
                gc.queue.filo.stack = dfs_stack_create(1);
                break;

            default:
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", arg_strategy, arg_state_db );
        }
        break;

    case Strat_SCC:
        switch (state_db) {
            case DB_DBSLL:
                gc.open_insert = scc_table_open_insert;
                gc.open_extract = scc_table_open_extract;
                gc.open = scc_table_open;
                gc.open_size = scc_table_open_size;
                gc.closed_insert = scc_table_closed_insert;
                gc.closed = scc_table_closed;

                gc.context = RTmalloc(sizeof(int) * N);
                gc.store.table.dbs = DBSLLcreate(N);
                gc.queue.filo.closed = bitset_create(128,128);
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
                Fatal(1, error, "unimplemented combination --strategy=%s, --state=%s", arg_strategy, arg_state_db );
        }
        break;

    default:
        Fatal(1, error, "unimplemented strategy");
    }

    // check deadlocks?
    if (dlk_detect) {
        gc.dlk_placeholder = gc.post_state_next;
        gc.post_state_next = gsea_dlk_default;
    }

    // maximum search depth?
    if (max != UINT_MAX) {
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
    void* ctx = gc.gsea_init(&s0);

    // while open states, process
    gc.foreach_open(gsea_foreach_open_cb, ctx);

    // give the result
    gc.report_finished(ctx);
}

static void
gsea_foreach_open(foreach_open_cb open_cb, void* arg)
{
    gsea_state_t s_open;
    while(gc.has_open(arg)) {
        gc.open_extract(&s_open, arg);
        open_cb(&s_open, arg);
    }
}

static void
gsea_foreach_open_cb(gsea_state_t* s_open, void* arg)
{
    // insert in closed set
    gc.closed_insert(s_open, arg);

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
    ntransitions++;
    return;
    (void)ti;
}

static void
gsea_progress(void* arg) {
    if (RTverbosity < 1 || visited < threshold)
        return;
    if (!cas (&threshold, threshold, threshold << 1))
        return;
    Warning (info, "explored %zu levels ~%zu states ~%zu transitions",
             max_depth, visited, ntransitions);
             //max_depth, explored, ntransitions); // more clear?
    return;
    (void)arg;
}

static void
gsea_finished(void* arg) {
    Warning (info, "state space %zu levels, %zu states %zu transitions",
             max_depth, visited, ntransitions);
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
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

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

	int src[N];
	GBgetInitialState(model,src);
	Warning(info,"got initial state");

    gsea_setup_default();
    gsea_setup();
    gsea_search(src);

	return 0;
}
