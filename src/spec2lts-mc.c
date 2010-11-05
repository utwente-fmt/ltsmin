#include "config.h"
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <pthread.h>
#include <sys/types.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>

#include <runtime.h>
#include <archive.h>
#include <treedbs.h>
#include <treedbs-ll.h>
#include <dbs-ll.h>
#include <dbs.h>
#include <vector_set.h>
#include <dfs-stack.h>
#include <is-balloc.h>
#include <scctimer.h>
#include <stream.h>
#include <cctables.h>
#include <zobrist.h>
#include <fast_hash.h>
#include <stringindex.h>
#include <lb.h>
#include <trace.h>

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

static const int    THREAD_STACK_SIZE = 400 * 4096; //pthread_attr_setstacksize

typedef enum { UseGreyBox, UseBlackBox } box_t;
typedef enum { UseDBSLL, UseTreeDBSLL } db_type_t;
typedef enum { Strat_BFS, Strat_DFS } strategy_t;
static char        *program;
static cct_map_t   *tables = NULL;
static char        *files[2];
static int          dbs_size = 24;
static int          refs = 0;
static int          matrix = 0;
static box_t        call_mode = UseBlackBox;
static size_t       max = UINT_MAX;
static size_t       W = 2;
static lb_t        *lb;
static void        *dbs;
static dbs_stats_f  statistics;
static dbs_get_f    get;
static TransitionCB succ_cb;
static char        *state_repr = "table";
static db_type_t    db_type = UseDBSLL;
static char        *arg_strategy = "bfs";
static strategy_t   strategy = Strat_BFS;
static char        *arg_lb = "srp";
static lb_method_t  lb_method = LB_SRP;
static char*        trc_output=NULL;
static int          dlk_detect = 0;
static size_t       G = 100;
static size_t       H = MAX_HANDOFF_DEFAULT;
static int          ZOBRIST = 0;
static int         *parent_idx=NULL;
static int          start_idx=0;

static si_map_entry strategies[] = {
    {"bfs", Strat_BFS},
    {"dfs", Strat_DFS},
    {NULL, 0}
};

static si_map_entry db_types[] = {
    {"table", UseDBSLL},
    {"tree", UseTreeDBSLL},
    {NULL, 0}
};

static si_map_entry lb_methods[] = {
    {"srp", LB_SRP},
    {"static", LB_Static},
    {"combined", LB_Combined},
    {NULL, 0}
};

static void
state_db_popt (poptContext con, enum poptCallbackReason reason,
               const struct poptOption *opt, const char *arg, void *data)
{
    int                 res;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        res = linear_search (db_types, state_repr);
        if (res < 0)
            Fatal (1, error, "unknown vector storage mode type %s", state_repr);
        db_type = res;
        res = linear_search (strategies, arg_strategy);
        if (res < 0)
            Fatal (1, error, "unknown search strategy %s", arg_strategy);
        strategy = res;
        res = linear_search (lb_methods, arg_lb);
        if (res < 0)
            Fatal (1, error, "unknown load balance method %s", arg_lb);
        lb_method = res;
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal (1, error, "unexpected call to state_db_popt");
    (void)con; (void)opt; (void)arg; (void)data;
}

static struct poptOption options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)state_db_popt, 0, NULL, NULL},
    {"threads", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &W, 0, "number of threads", "<int>"},
    {"state", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &state_repr,
      0, "select the data structure for storing states", "<tree|table>"},
    {"size", 's', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &dbs_size, 0,
     "size of the state store (log2(size))", NULL},
    {"strategy", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_strategy, 0, "select the search strategy", "<bfs|dfs>"},
    {"lb", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
      &arg_lb, 0, "select the load balancing method", "<srp|static|combined>"},
    {"gran", 'g', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &G,
     0, "subproblem granularity ( T( work(P,g) )=min( T(P), g ) )", NULL},
    {"handoff", 'h', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &H,
     0, "maximum balancing handoff (handoff=min(max, stack_size/2))", NULL},
    {"zobrist", 'z', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &ZOBRIST,
     0,"log2 size of zobrist random table (6 or 8 is good enough; 0 is no zobrist)", NULL},
    {"grey", 0, POPT_ARG_VAL, &call_mode, UseGreyBox, "make use of GetTransitionsLong calls", NULL},
    {"ref", 0, POPT_ARG_VAL, &refs, 1, "store references on the stack/queue instead of full states", NULL},
    {"max", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &max, 0, "maximum search depth", "<int>"},
    {"deadlock", 'd', POPT_ARG_VAL, &dlk_detect, 1, "detect deadlocks", NULL },
    {"trace", 0, POPT_ARG_STRING, &trc_output, 0, "file to write trace to", "<lts output>" },
    {"matrix", 'm', POPT_ARG_VAL, &matrix, 1, "Print the dependency matrix for the model and exit", NULL},
#if defined(MCRL)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl_options, 0, "mCRL options", NULL},
#endif
#if defined(MCRL2)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, mcrl2_options, 0, "mCRL2 options", NULL},
#endif
#if defined(NIPS)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, nips_options, 0, "NIPS options", NULL},
#endif
#if defined(ETF)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, etf_options, 0, "ETF options", NULL},
#endif
#if defined(DIVINE)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, dve_options, 0, "DiVinE options", NULL},
#endif
#if defined(DIVINE2)
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, dve2_options, 0, "DiVinE 2.2 options", NULL},
#endif
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "Greybox options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, lts_io_options, 0, NULL, NULL},
    POPT_TABLEEND
};

typedef struct thread_ctx_s {
    pthread_t           me;             // currently executing thread
    stats_t            *stats;          // running statitics
    size_t              id;             // thread id (0..NUM_THREADS)
    stream_t            out;            // raw file output stream
    model_t             model;          // Greybox model
    int                *store;          // currently visited state
    int                *cur_state;      // currently explored state
    int                 cur_idx;        // previous idx for trace reconstructing
    int                 group;          // currently explored group (-1=unknown)
    dfs_stack_t         stack;          // Successor stack (for BFS and DFS)
    dfs_stack_t         in_stack;       // Input stack (for BFS)
    dfs_stack_t         out_stack;      // Output stack (for BFS)
    isb_allocator_t     group_stack;    // last explored group per frame (grey)
    size_t              visited;        // counter: visited states
    size_t              explored;       // counter: explored states
    size_t              trans;          // counter: transitions
    size_t              level;          // counter: (BFS) level / (DFS) max level
    size_t              stack_sizes;    // max combined stack sizes
    size_t              cur_level;      // counter: current (DFS) level
    float               runtime;        // measured exploration time
    size_t              load;           // queue load (for balancing)
} thread_ctx_t;

extern int          dfs_grey (thread_ctx_t * ctx, size_t work);
extern int          dfs (thread_ctx_t * ctx, size_t work);
extern int          bfs (thread_ctx_t * ctx, size_t work);
extern void         handle_successor_tree (void *arg, transition_info_t *ti, int *dst);
extern void         handle_successor_zobrist (void *arg, transition_info_t *ti, int *dst);
extern void         handle_successor (void *arg, transition_info_t *ti, int *dst);
extern size_t       split_bfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs_grey (size_t src_id, size_t dst_id, size_t handoff);

static int          N;
static int          K;
static int          Q;                  // queueing length of states
static int          IDX_LOC;            // location of database index on queue
static int          MAX_SUCC;           // max succ. count to expand at once
static size_t       threshold;
static pthread_attr_t *attr = NULL;
static thread_ctx_t **contexts;
static zobrist_t    zobrist = NULL;
#ifndef __APPLE__
static pthread_barrier_t start_barrier;       // synchronize starting point
#endif

void
add_results(thread_ctx_t *res, thread_ctx_t *ctx)
{
    res->runtime += ctx->runtime;
    res->visited += ctx->visited;
    res->explored += ctx->explored;
    res->trans += ctx->trans;
    res->level += ctx->level;
    res->stack_sizes += ctx->stack_sizes;
    add_stats(res->stats, ctx->stats);
}

static model_t
get_model (int first)
{
    int                 start_index = 0;
    if (tables == NULL)
        tables = cct_create_map ();
    cct_cont_t         *map = cct_create_cont (tables, start_index);
    model_t             model = GBcreateBase ();
    GBsetChunkMethods (model, cct_new_map, map, cct_map_get, cct_map_put,
                       cct_map_count);
    if (first)
        GBloadFileShared (model, files[0]);
    GBloadFile (model, files[0], &model);
    return model;
}

thread_ctx_t *
create_context (size_t id)
{
    thread_ctx_t       *ctx = RTalign (CACHE_LINE_SIZE, sizeof (thread_ctx_t));
    memset (ctx, 0, sizeof (thread_ctx_t));
    ctx->id = id;
    ctx->model = NULL;
    ctx->cur_state = NULL;
    ctx->group = -1;
    ctx->store = RTalign (CACHE_LINE_SIZE, sizeof (int[N * 2]));
    memset (ctx->store, 0, sizeof (int[N * 2]));
    ctx->stack = dfs_stack_create (Q);
    ctx->out_stack = ctx->in_stack = ctx->stack;
    if (strategy == Strat_BFS)
        ctx->in_stack = dfs_stack_create (Q);
    else if ( UseGreyBox == call_mode )
        ctx->group_stack = isba_create (1);
    if (files[1]) {
        char               name[PATH_MAX];
        int ret = snprintf (name, sizeof name, "%s-%zu", files[1], id);
        assert (ret < (int)sizeof name);
        ctx->out = file_output (name);
        stream_write (ctx->out, &K, 4);
    }
    return ctx;
}

static uint32_t
z_rehash (const void *v, int b, uint32_t seed)
{
    return zobrist_rehash (zobrist, seed);
    (void)b; (void)v;
}

model_t
init_globals (int argc, char *argv[])
{
#if defined(NIPS) // Stack usage of NIPS is higher than ptheads default max
    attr = RTmalloc (sizeof (pthread_attr_t));
    pthread_attr_init (attr);
    pthread_attr_setstacksize (attr, THREAD_STACK_SIZE);
#endif
    // parse command line parameters
    RTinitPopt (&argc, &argv, options, 1, 2, files, NULL, "<model> [<raw>]",
                "Perform a parallel reachability analysis of <model>\n\nOptions");
    if (trc_output) {
        parent_idx = RTmalloc(sizeof(int[1<<dbs_size]));
        dlk_detect = 1;
    }
#ifndef __APPLE__
    pthread_barrier_init(&start_barrier, NULL, W);
#endif
    Warning (info, "Using %d cores.", W);
    Warning (info, "loading model from %s", files[0]);
    threshold = 100000 / W;
    program = get_label ();
    if (ZOBRIST && db_type == UseTreeDBSLL)
        Fatal (1, error, "Zobrist and treedbs is not implemented");
    if (lb_method == LB_Combined)
        Fatal (1, error, "Combined load balancing (static+SRP) is not implemented");
    model_t             model = get_model (1);
    lts_type_t          ltstype = GBgetLTStype (model);
    int                 state_labels = lts_type_get_state_label_count (ltstype);
    int                 edge_labels = lts_type_get_edge_label_count (ltstype);
    Warning (info, "There are %d state labels and %d edge labels",
             state_labels, edge_labels);
    matrix_t           *m = GBgetDMInfo (model);
    N = lts_type_get_state_length (ltstype);
    K = dm_nrows (m);
    Warning (info, "length is %d, there are %d groups", N, K);
    Q = refs ? 1 : N+1; // either store only idx or vector+idx
    IDX_LOC = Q-1;
    MAX_SUCC = ( Strat_DFS == strategy ? 1 : INT_MAX );
    succ_cb = handle_successor;
    if (ZOBRIST) { // idx on queue can be used as previous hash for zobrist!
        zobrist = zobrist_create (N, ZOBRIST, m);
        succ_cb = handle_successor_zobrist;
    }
    switch (db_type) {
    case UseDBSLL:
        dbs = DBSLLcreate_sized (N, dbs_size, 
                                 (hash32_f)(ZOBRIST?z_rehash:SuperFastHash));
        statistics = (dbs_stats_f) DBSLLstats;
        get = (dbs_get_f) DBSLLget;
        break;
    case UseTreeDBSLL:
        dbs = TreeDBSLLcreate_dm (N, dbs_size, m);
        statistics = (dbs_stats_f) TreeDBSLLstats;
        get = (dbs_get_f) TreeDBSLLget;
        if (!refs) {
            Q = 2*N;                    // store q internal data + vector
            IDX_LOC = 1;                // idx is present in internal tree data
        }
        succ_cb = handle_successor_tree;
        break;
    }
    lb = ( strategy == Strat_BFS
         ?   lb_create_max(W, (algo_f)bfs,      split_bfs, G, lb_method, H)
         : ( call_mode == UseGreyBox //strategy == Strat_DFS
           ? lb_create_max(W, (algo_f)dfs_grey, split_dfs, G, lb_method, H)
           : lb_create_max(W, (algo_f)dfs,      split_dfs, G, lb_method, H) ) );
    contexts = RTmalloc (sizeof (thread_ctx_t *[W]));
    for (size_t i = 0; i < W; i++)
        contexts[i] = create_context (i);
    if (matrix) {
        GBprintDependencyMatrix (stdout, model);
        exit (EXIT_SUCCESS);
    }
    if (RTverbosity >= 3) {
        fprintf (stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrix (stderr, model);
    }
    return model;
}

static inline void
print_state_space_total (char *name, thread_ctx_t * ctx)
{
    Warning (info, "%s%d levels %d states %d transitions",
             name, ctx->level, ctx->visited, ctx->trans);
}

static inline void
maybe_report (thread_ctx_t * ctx, char *msg)
{
    if (RTverbosity < 1 || ctx->visited < threshold)
        return;
    if (!cas (&threshold, threshold, threshold << 1))
        return;
    if (W == 1)
        print_state_space_total (msg, ctx);
    else
        Warning (info, "%s%d levels ~%d states ~%d transitions", msg,
                 ctx->level, W * ctx->visited,  W * ctx->trans);
}

void
print_statistics(thread_ctx_t *total, float tot)
{
    char               *name;
    double              mem1, mem2, compr, ratio;
    size_t              el_size = db_type == UseTreeDBSLL ? 3 : N;
    size_t              s = sizeof (int[Q]);
    mem1 = (double)s * total->stack_sizes / (1 << 10);
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.0fKB",
             s, total->stack_sizes, mem1);
    mem2 = (double)(1 << dbs_size) * sizeof (int[el_size]) / (1 << 10);
    compr = 1 - (double)(el_size * total->stats->elts) / (N * total->explored);
    ratio = (double)((total->stats->elts * 100) / (1 << dbs_size));
    name = db_type == UseTreeDBSLL ? "Tree" : "Table";
    Warning (info, "DB: %s, memory: %.1fMB, compression: %.1f%%, "
             "fill-ratio: %.1f%%", name, mem2 / 1024, 100 * compr, ratio);
    if (RTverbosity >= 2) {        // detailed output for scripts
        Warning (info, "time:{{{%.2f}}}, elts:{{{%zu}}}, trans:{{{%zu}}}, misses"
                 ":{{{%zu}}}, tests:{{{%zu}}}, rehashes:{{{%zu}}} memq:{{{%.0f}}} "
                 "tt:{{{%.2f}}} explored:{{{%zu}}}, incr:{{{%zu}}} memdb:{{{%.0f}}}",
                 total->runtime, total->stats->elts, total->trans, total->stats->misses,
                 total->stats->tests, total->stats->rehashes, mem1, tot,
                 total->explored, total->stats->cache_misses, mem2);
    }
}

void
conf_thread (thread_ctx_t * ctx)
{
    char               *lbl = RTmalloc (sizeof (char[20]));
    snprintf (lbl, sizeof (char[20]), "%s[%zu]", program, ctx->id);
    set_label (lbl);    // register print label and load model
    if (ctx->model == NULL)
        ctx->model = get_model (0);
#ifndef __APPLE__
    // lock thread to one core
    cpu_set_t          *set = RTmalloc (sizeof (cpu_set_t));
    CPU_ZERO (set);
    CPU_SET (ctx->id, set);
    sched_setaffinity (0, sizeof (cpu_set_t), set);
    //synchronize exploration
    pthread_barrier_wait(&start_barrier);
#endif
}

static inline void
push_state_or_ref_or_data(thread_ctx_t *ctx, int idx, int *data)
{
    int *q_loc = dfs_stack_push(ctx->stack, data);       // bogus iff refs==1
    q_loc[IDX_LOC] = idx;                                // bogus iff refs<>1
    ctx->load++;
    ctx->visited++;
    if (trc_output)
        parent_idx[idx] = ctx->cur_idx;
}

void
handle_successor_tree (void *arg, transition_info_t *ti, int *dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    internal_t          inout = (internal_t)ctx->cur_state;
    if (!TreeDBSLLlookup_dm (dbs, dst, &inout, ti->group))
        push_state_or_ref_or_data(ctx, TreeDBSLLindex(inout), inout);
    if (files[1])
        stream_write (ctx->out, dst, sizeof (int[N]));
    ctx->trans++;
}

void
handle_successor_zobrist (void *arg, transition_info_t *ti, int *dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    uint32_t            zhash;
    int                 idx;
    uint32_t prev_hash = DBSLLmemoized_hash(dbs, ctx->cur_idx);
    zhash = zobrist_hash_dm (zobrist, dst, ctx->cur_state, prev_hash, ti->group);
    if (!DBSLLlookup_hash (dbs, dst, &idx, &zhash))
        push_state_or_ref_or_data(ctx, idx, dst);
    if (files[1])
        stream_write (ctx->out, dst, sizeof (int[N]));
    ctx->trans++;
}

void
handle_successor (void *arg, transition_info_t *ti, int *dst)
{
    (void)ti;
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    int                 idx;
    if (!DBSLLlookup_hash (dbs, dst, &idx, NULL))
        push_state_or_ref_or_data(ctx, idx, dst);
    if (files[1])
        stream_write (ctx->out, dst, sizeof (int[N]));
    ctx->trans++;
}

void *
get_state(int idx, int *store)
{
    int *state = get(dbs, idx, store);
    return UseTreeDBSLL==db_type ? TreeDBSLLdata(dbs, state) : state;
}

static          int dl_found = 0;
void
handle_deadlock(model_t model, int idx, int level)
{
    if ( cas (&dl_found, 0, 1) ) {
        Warning(info,"deadlock found in state");
        if (trc_output) {
            trc_env_t *trace_env = trc_create(model, get_state, start_idx);
            trc_find_and_write(trace_env, trc_output, idx, level, parent_idx);
        }
        lb_stop(lb);
        Warning(info, "exiting now");
    }
    pthread_exit( statistics (dbs) );
}

static inline int
explore_state (thread_ctx_t * ctx, int *state, int next_index)
{
    int                 count = 0;
    int                 i = K;
    if (0 == next_index && ctx->level >= max)
        return K;
    ctx->cur_idx = state[IDX_LOC]; //record idx
    if ( refs )
        state = get (dbs, ctx->cur_idx, ctx->store);
    ctx->cur_state = state;
    if ( UseTreeDBSLL==db_type )
        state = TreeDBSLLdata (dbs, state);
    if ( UseBlackBox == call_mode ) {
        count = GBgetTransitionsAll(ctx->model, state, succ_cb, ctx);
    } else { // UseGreyBox
        for (ctx->group=next_index; ctx->group<K && count<MAX_SUCC; ctx->group++)
            count += GBgetTransitionsLong(ctx->model, ctx->group, state, succ_cb, ctx);
        count = count || next_index; //deadlock <--> post(count=0) ^ pre(i=0)
    }
    if ( dlk_detect && (0==count || dl_found) )
        handle_deadlock(ctx->model, ctx->cur_idx, ctx->level);
    ctx->explored++;
    return i;
}

int
dfs_grey (thread_ctx_t * ctx, size_t work)
{
    int                 next_index = 0;
    size_t              start_count = ctx->explored;
    while (1) {
        int            *state_or_ref = dfs_stack_top (ctx->stack);
        if (!state_or_ref) {
            if (0 == dfs_stack_nframes (ctx->stack))
                return 1;
            dfs_stack_leave (ctx->stack);
            next_index = isba_pop_int (ctx->group_stack)[0];
            ctx->cur_level--;
            continue;
        }
        if (next_index == K) {
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        } else {
            ctx->cur_level++;
            if (ctx->cur_level > ctx->level)
                ctx->level = ctx->cur_level;
            dfs_stack_enter (ctx->stack);
            next_index = explore_state (ctx, state_or_ref, next_index);
            maybe_report (ctx, "");
            isba_push_int (ctx->group_stack, &next_index);
            if (ctx->explored - start_count >= work) {
                return 0;
            }
        }
        next_index = 0;
    }
}

int
dfs (thread_ctx_t * ctx, size_t work)
{
    size_t              start_count = ctx->explored;
    while (1) {
        int            *state_or_ref = dfs_stack_top (ctx->stack);
        if (!state_or_ref) {
            if (0 == dfs_stack_nframes (ctx->stack))
                return 1;
            dfs_stack_leave (ctx->stack);
            ctx->cur_level--;
            dfs_stack_pop (ctx->stack);
            ctx->load--;
            continue;
        }
        if (dfs_stack_frame_size (ctx->stack)) {
            dfs_stack_enter (ctx->stack);
            ctx->cur_level++;
        }
        if (ctx->cur_level > ctx->level)
            ctx->level = ctx->cur_level;
        explore_state (ctx, state_or_ref, 0);
        maybe_report (ctx, "");
        if (ctx->explored - start_count >= work) {
            return 0;
        }
    }
}

int
bfs (thread_ctx_t * ctx, size_t work)
{
    size_t              start_count = ctx->explored;
    while (1) {
        int            *state_or_ref = dfs_stack_top (ctx->in_stack);
        if (NULL == state_or_ref) {
            size_t              size = dfs_stack_frame_size (ctx->out_stack);
            if (0 == size)
                return 1;
            dfs_stack_t         old = ctx->out_stack;
            ctx->stack = ctx->out_stack = ctx->in_stack;
            ctx->in_stack = old;
            ctx->level++;
            continue;
        }
        ctx->load--;
        dfs_stack_pop (ctx->in_stack);
        explore_state (ctx, state_or_ref, 0);
        if (ctx->explored - start_count >= work)
            return 0;
        maybe_report (ctx, "");
    }
}

size_t
split_bfs (size_t source_id, size_t target_id, size_t handoff)
{
    thread_ctx_t       *source = contexts[source_id];
    thread_ctx_t       *target = contexts[target_id];
    dfs_stack_t         source_stack = source->in_stack;
    size_t              in_size = dfs_stack_size (source_stack);
    if (in_size < 2) {
        in_size = dfs_stack_size (source->out_stack);
        source_stack = source->out_stack;
    }
    in_size >>= 1;
    handoff = in_size < handoff ? in_size : handoff;
    target->load += handoff;
    for (size_t i = 0; i < handoff; i++) {
        int                *one = dfs_stack_peek (source_stack, i);
        dfs_stack_push (target->stack, one);
    }
    dfs_stack_discard (source_stack, handoff);
    return handoff;
}

size_t
split_dfs (size_t source_id, size_t target_id, size_t handoff)
{
    thread_ctx_t       *source = contexts[source_id];
    thread_ctx_t       *target = contexts[target_id];
    size_t              in_size = dfs_stack_size (source->stack);
    in_size >>= 1;
    handoff = in_size < handoff ? in_size : handoff;
    target->load += handoff;
    for (size_t i = 0; i < handoff; i++) {
        int                *one = dfs_stack_top (source->stack);
        if (!one) {
            if (UseGreyBox == call_mode) {
                int *next_index = isba_pop_int (source->group_stack);
                isba_push_int (target->group_stack, next_index);
            }
            dfs_stack_leave (source->stack);
            source->cur_level--;
            one = dfs_stack_pop (source->stack);
            dfs_stack_push (target->stack, one);
            dfs_stack_enter (target->stack);
            target->cur_level++;
        } else {
            dfs_stack_push (target->stack, one);
            dfs_stack_pop (source->stack);
        }
    }
    return handoff;
}

static void *
explore (void *args)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) args;
    mytimer_t           timer = SCCcreateTimer ();
    conf_thread ( ctx );
    lb_local_init(lb, ctx->id, ctx, &ctx->load);
    SCCstartTimer (timer);
    lb_balance( lb, ctx->id, ctx, &ctx->load );
    SCCstopTimer (timer);
    ctx->runtime = SCCrealTime (timer);
    return statistics (dbs);  // call results are thread dependent
}

void ex_program(int sig) {
    if ( !lb_stop(lb) ) {
        Warning(info, "PREMATURE EXIT (second caught signal: %d)", sig);
        exit(0);
    } else {
        Warning(info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
    (void) sig;
}

int
main (int argc, char *argv[])
{
    (void) signal(SIGINT, ex_program);
    char                name[128];
    model_t             model = init_globals (argc, argv);
    thread_ctx_t       *total = contexts[0];
    total->model = model;
    GBgetInitialState (model, total->store);
    transition_info_t start_trans_info = GB_NO_TRANSITION;
    succ_cb (total, &start_trans_info, total->store);
    start_idx = dfs_stack_top (total->stack)[IDX_LOC];
    int                 start_group = 0;
    if (UseGreyBox == call_mode && Strat_DFS == strategy)
        isba_push_int (total->group_stack, &start_group);
    total->trans = 0;
    mytimer_t           timer = SCCcreateTimer ();
    SCCstartTimer (timer);
    for (size_t i = 0; i < W; i++)
        pthread_create (&contexts[i]->me, attr, explore, contexts[i]);
    for (size_t i = 0; i < W; i++)
        pthread_join (contexts[i]->me, (void **)&contexts[i]->stats);
    SCCstopTimer (timer);
    for (size_t i = 0; i < W; i++) {
        thread_ctx_t   *ctx = contexts[i];
        ctx->stack_sizes = dfs_stack_size_max (ctx->in_stack);
        if (strategy == Strat_BFS)
            ctx->stack_sizes += dfs_stack_size_max (ctx->out_stack);
        if (total != ctx)
            add_results (total, ctx);
        snprintf (name, sizeof name, "[%zu] saw in %.2f sec ", i, ctx->runtime);
        print_state_space_total (name, ctx);
        if (ctx->load) Warning (info, "Wrong load counter %zu", ctx->load);
        if (files[1]) stream_close (&ctx->out);
    }
    total->level /= W; // not so meaningful for DFS
    print_state_space_total ("State space has ", total);
    SCCreportTimer (timer, "Total exploration time");
    if (RTverbosity >= 1)
        print_statistics(total, SCCrealTime (timer));
    return EXIT_SUCCESS;
}
