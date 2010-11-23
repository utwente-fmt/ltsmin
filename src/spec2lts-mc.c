#include <config.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <archive.h>
#include <cctables.h>
#include <dbs-ll.h>
#include <dbs.h>
#include <dm/bitvector.h>
#include <dfs-stack.h>
#include <fast_hash.h>
#include <is-balloc.h>
#include <lb.h>
#include <runtime.h>
#include <scctimer.h>
#include <spec-greybox.h>
#include <stream.h>
#include <stringindex.h>
#include <trace.h>
#include <treedbs-ll.h>
#include <treedbs.h>
#include <vector_set.h>
#include <zobrist.h>

static const int    THREAD_STACK_SIZE = 400 * 4096; //pthread_attr_setstacksize

typedef uint32_t    idx_t;
typedef int        *state_data_t;
static const state_data_t   state_data_dummy;
static const size_t         SLOT_SIZE = sizeof(*state_data_dummy);
typedef int        *raw_data_t;
typedef struct state_info_s {
    state_data_t        data;
    tree_t              tree;
    idx_t               idx;
    uint32_t            hash32;
    int                 group;
} state_info_t;

typedef enum { UseGreyBox, UseBlackBox } box_t;
typedef enum { UseDBSLL, UseTreeDBSLL } db_type_t;
typedef enum { Strat_BFS, Strat_DFS, Strat_NDFS, Strat_NNDFS } strategy_t;
static char        *program;
static cct_map_t   *tables = NULL;
static char        *files[2];
static int          dbs_size = 24;
static int          refs = 0;
static box_t        call_mode = UseBlackBox;
static size_t       max = UINT_MAX;
static size_t       W = 2;
static lb_t        *lb;
static void        *dbs;
static dbs_stats_f  statistics;
static dbs_get_f    get;
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
static idx_t       *parent_idx=NULL;
static idx_t        start_idx=0;

static si_map_entry strategies[] = {
    {"bfs", Strat_BFS},
    {"dfs", Strat_DFS},
    {"ndfs", Strat_NDFS},
    {"nndfs", Strat_NNDFS},
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

static void
exit_ltsmin(int sig)
{
    if ( !lb_stop(lb) ) {
        Warning(info, "UNGRACEFUL EXIT");
        exit(EXIT_FAILURE);
    } else {
        Warning(info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
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
    SPEC_POPT_OPTIONS,
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "Greybox options", NULL},
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, lts_io_options, 0, NULL, NULL},
    POPT_TABLEEND
};

/* State colors are encoded using one bitset, where two consecutive bits
 * describe four colors: (thus state 0 uses bit 0 and 1, state 1, 2 and 3, ..)
 * Colors:
 * While: 0 0) first generated by next state call
 * Cyan:  0 1) A state whose blue search has not been terminated
 * Blue:  1 0) A state that has finished its blue search and has not yet been reached
 *        in a red search
 * Red:   1 1) A state that has been considered in both the blue and the red search
 */
typedef enum {NDFS_WHITE, NDFS_BLUE, NDFS_RED, NDFS_CYAN} ndfs_color_t;

static inline ndfs_color_t
ndfs_get_color(bitvector_t *set, int idx)
{
    return bitvector_get2 (set, idx<<1);
}

static inline int //RED and BLUE are independent
ndfs_isset_or_set_color(bitvector_t *set, int idx, ndfs_color_t color)
{
    return bitvector_isset_or_set (set, (idx<<1)+color-1);
}

static inline ndfs_color_t
nndfs_get_color(bitvector_t *set, int idx)
{
    return bitvector_get2 (set, idx<<1);
}

static inline int
nndfs_isset_or_set_color(bitvector_t *set, int idx, ndfs_color_t color)
{
    return bitvector_isset_or_set2 (set, idx<<1, color);
}

void swap_pointer(void **a, void **b) {
    void *old = *a; *a = *b; *b = old;
}

typedef struct counter_s {
    double              runtime;        // measured exploration time
    size_t              visited;        // counter: visited states
    size_t              explored;       // counter: explored states
    size_t              trans;          // counter: transitions
    size_t              level_max;      // counter: (BFS) level / (DFS) max level
    size_t              level_cur;      // counter: current (DFS) level
    size_t              stack_sizes;    // max combined stack sizes
    size_t              touched_red;    // visited-before accepting states
    stats_t            *stats;          // running state storage statistics
    size_t              max_load;      // last work-load, for work slicing
} counter_t;

typedef struct thread_ctx_s {
    pthread_t           me;             // currently executing thread
    size_t              id;             // thread id (0..NUM_THREADS)
    stream_t            out;            // raw file output stream
    model_t             model;          // Greybox model
    state_data_t        store;          // temporary state storage1
    state_data_t        store2;          // temporary state storage2
    state_info_t        state;          // currently explored state
    dfs_stack_t         stack;          // Successor stack (for BFS and DFS)
    dfs_stack_t         in_stack;       // Input stack (for BFS)
    dfs_stack_t         out_stack;      // Output stack (for BFS)
    bitvector_t         color;          // Local NDFS coloring of states (idx-based)
    isb_allocator_t     group_stack;    // last explored group per frame (grey)
    counter_t           counters;       // reachability/NDFS_blue counters
    counter_t           red;            // NDFS_red counters
    size_t              load;           // queue load (for balancing)
    ndfs_color_t        COLOR;          // current NDFS color
    idx_t               SEED;          // current NDFS seed
} thread_ctx_t;

/* predecessor --(transition_group)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     state_info_t *predecessor);

/*TODO: change idx_t to size_t and handle maximum for 32bit machines
  this also requires changes to the data structures: tree, table, bitvector
*/
static const idx_t DUMMY_IDX = UINT_MAX;


extern size_t state_info_size();
extern size_t state_info_int_size();
extern void state_info_create_empty(state_info_t *state);
extern void state_info_create(state_info_t *state, state_data_t data,
                              tree_t tree, idx_t idx, int group);
extern void state_info_serialize(state_info_t *state, raw_data_t data);
extern void state_info_deserialize(state_info_t *state, raw_data_t data,
                                   raw_data_t store);
extern void         dfs_grey (thread_ctx_t *ctx, size_t work);
extern void         ndfs_blue (thread_ctx_t *ctx, size_t work);
extern void         nndfs_blue (thread_ctx_t *ctx, size_t work);
extern void         dfs (thread_ctx_t *ctx, size_t work);
extern void         bfs (thread_ctx_t *ctx, size_t work);
extern size_t       split_bfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs_grey (size_t src_id, size_t dst_id, size_t handoff);

static find_or_put_f find_or_put;
static int          N;
static int          K;
static int          MAX_SUCC;           // max succ. count to expand at once
static size_t       threshold;
static size_t       red_threshold;
static pthread_attr_t  *attr = NULL;
static thread_ctx_t   **contexts;
static zobrist_t    zobrist = NULL;
#ifndef __APPLE__
static pthread_barrier_t start_barrier;       // synchronize starting point
#endif

void
add_results(counter_t *res, counter_t *cnt)
{
    res->runtime += cnt->runtime;
    res->visited += cnt->visited;
    res->explored += cnt->explored;
    res->trans += cnt->trans;
    res->level_max += cnt->level_max;
    res->stack_sizes += cnt->stack_sizes;
    add_stats(res->stats, cnt->stats);
}

static inline void
increase_level(counter_t *cnt)
{
    cnt->level_cur++;
    if(cnt->level_cur > cnt->level_max)
        cnt->level_max = cnt->level_cur;
}

static              model_t
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
    state_info_create_empty(&ctx->state);
    ctx->store = RTalign (CACHE_LINE_SIZE, sizeof (int[N * 2]));
    memset (ctx->store, 0, sizeof (int[N * 2]));
    ctx->store2 = RTalign (CACHE_LINE_SIZE, sizeof (int[N * 2]));
    memset (ctx->store2, 0, sizeof (int[N * 2]));
    ctx->stack = dfs_stack_create (state_info_int_size());
    ctx->out_stack = ctx->in_stack = ctx->stack;
    if (strategy == Strat_BFS)
        ctx->in_stack = dfs_stack_create (state_info_int_size());
    //allocate two bits for NDFS colorings
    if (strategy == Strat_NDFS || strategy == Strat_NNDFS)
        bitvector_create_large(&ctx->color, 2<<dbs_size);
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

static int
find_or_put_zobrist(state_info_t *state, state_info_t *pred)
{
    state->hash32 = zobrist_hash_dm(zobrist, state->data, pred->data,
                                            pred->hash32, state->group);
    return DBSLLlookup_hash (dbs, state->data, &state->idx, &state->hash32);
}

static int
find_or_put_dbs(state_info_t *state, state_info_t *predecessor)
{
    return DBSLLlookup_hash (dbs, state->data, &state->idx, NULL);
    (void) predecessor;
}

static int
find_or_put_tree(state_info_t *s, state_info_t *pred)
{
    int                 ret;
    ret = TreeDBSLLlookup_dm (dbs, s->data, pred->tree, s->tree, s->group);
    s->idx = TreeDBSLLindex(s->tree);
    return ret;
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
    model_t             model = get_model (1);
    if (strategy == Strat_NDFS || strategy == Strat_NNDFS) {
        if (ZOBRIST)
            Fatal (1, error, "Zobrist and (N)NDFS is not implemented.");
        //if (!(GBhasProperty(model) == PROPERTY_LTL_SPIN || GBhasProperty(model) == PROPERTY_LTL_TEXTBOOK))
        //    Fatal(1, error, "NDFS search only works in combination with a never claim (use --ltl or supply a Buchi product from the language frontend)");
        if (W > 1) {
            Warning (info, "(N)NDFS is used, switching to one thread.");
            W = 1;
        }
        lb_method = LB_None;
    } else {
        if (trc_output) {
            parent_idx = RTmalloc(sizeof(int[1<<dbs_size]));
            dlk_detect = 1;
        }
    }
#ifndef __APPLE__
    pthread_barrier_init(&start_barrier, NULL, W);
#endif
    Warning (info, "Using %d cores.", W);
    Warning (info, "loading model from %s", files[0]);
    threshold = red_threshold = 100000 / W;
    program = get_label ();
    lts_type_t          ltstype = GBgetLTStype (model);
    int                 state_labels = lts_type_get_state_label_count (ltstype);
    int                 edge_labels = lts_type_get_edge_label_count (ltstype);
    Warning (info, "There are %d state labels and %d edge labels",
             state_labels, edge_labels);
    matrix_t           *m = GBgetDMInfo (model);
    N = lts_type_get_state_length (ltstype);
    K = dm_nrows (m);
    Warning (info, "length is %d, there are %d groups", N, K);

    MAX_SUCC = ( Strat_DFS == strategy ? 1 : INT_MAX );
    switch (db_type) {
    case UseDBSLL:
        if (ZOBRIST) {
            zobrist = zobrist_create (N, ZOBRIST, m);
            find_or_put = find_or_put_zobrist;
            dbs = DBSLLcreate_sized (N, dbs_size, (hash32_f)z_rehash);
        } else {
            find_or_put = find_or_put_dbs;
            dbs = DBSLLcreate_sized (N, dbs_size, (hash32_f)SuperFastHash);
        }
        statistics = (dbs_stats_f) DBSLLstats;
        get = (dbs_get_f) DBSLLget;
        break;
    case UseTreeDBSLL:
        if (ZOBRIST)
            Fatal (1, error, "Zobrist and treedbs is not implemented");
        statistics = (dbs_stats_f) TreeDBSLLstats;
        get = (dbs_get_f) TreeDBSLLget;
        find_or_put = find_or_put_tree;
        dbs = TreeDBSLLcreate_dm (N, dbs_size, m);
        break;
    }
    contexts = RTmalloc (sizeof (thread_ctx_t *[W]));
    for (size_t i = 0; i < W; i++)
        contexts[i] = create_context (i);

    /* Load balancer assigned last, see exit_ltsmin */
    switch (strategy) {
    case Strat_NNDFS:
        lb = lb_create_max(W, (algo_f)nndfs_blue,NULL,G, lb_method, H); break;
    case Strat_NDFS:
        lb = lb_create_max(W, (algo_f)ndfs_blue, NULL,G, lb_method, H); break;
    case Strat_BFS:
        lb = lb_create_max(W, (algo_f)bfs, split_bfs, G, lb_method, H); break;
    case Strat_DFS: {
        algo_f algo = call_mode == UseGreyBox ? (algo_f)dfs_grey : (algo_f)dfs;
        lb = lb_create_max(W, algo,        split_dfs, G, lb_method, H); break;
    }}
    (void) signal(SIGINT, exit_ltsmin);

    if (RTverbosity >= 3) {
        fprintf (stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined (stderr, model);
    }
    return model;
}

static inline void
print_state_space_total (char *name, counter_t *cnt)
{
    Warning (info, "%s%zu levels %zu states %zu transitions",
             name, cnt->level_max, cnt->explored, cnt->trans);
}

static inline void
maybe_report (counter_t *cnt, char *msg, size_t *threshold)
{
    if (RTverbosity < 1 || cnt->explored < *threshold)
        return;
    if (!cas (threshold, *threshold, *threshold << 1))
        return;
    if (W == 1)
        print_state_space_total (msg, cnt);
    else
        Warning (info, "%s%zu levels ±%zu states ±%zu transitions", msg,
                 cnt->level_max, W * cnt->explored,  W * cnt->trans);
}

void
print_statistics(counter_t *reach, counter_t *red, mytimer_t timer)
{
    float               tot = SCCrealTime(timer);
    size_t              states = reach->explored + red->explored;
    size_t              db_elts = reach->stats->elts;
    char               *name;
    double              mem1, mem2, compr, ratio;
    size_t              el_size = db_type == UseTreeDBSLL ? 3 : N;
    size_t              s = state_info_size();
    mem1 = (double)s * reach->stack_sizes / (1 << 10);

    Warning(info, "")
    print_state_space_total ("State space has ", reach);
    SCCreportTimer (timer, "Total exploration time");

    if (Strat_NDFS == strategy || Strat_NNDFS == strategy) {
        Warning(info, "")
        Warning (info, "NDFS stats:")
        Warning (info, "red states: %zu, accepting states: %zu, red transitions: %zu "
                 "touched: %zu", red->explored, red->visited, red->trans, red->touched_red);
    }

    Warning(info, "")
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.0fKB",
             s, reach->stack_sizes, mem1);
    mem2 = (double)(1UL << (dbs_size-20)) * sizeof (int[el_size]);
    compr = (double)(db_elts * el_size) / (N * states) * 100;
    ratio = (double)((db_elts * 100) / (1UL << dbs_size));
    name = db_type == UseTreeDBSLL ? "Tree" : "Table";
    Warning (info, "DB: %s, memory: %.1fMB, compression: %.1f%%, "
             "fill-ratio: %.1f%%", name, mem2, compr, ratio);
    if (RTverbosity >= 2) {        // detailed output for scripts
        Warning (info, "time:{{{%.2f}}}, elts:{{{%zu}}}, trans:{{{%zu}}}, misses"
                 ":{{{%zu}}}, tests:{{{%zu}}}, rehashes:{{{%zu}}} memq:{{{%.0f}}} "
                 "tt:{{{%.2f}}} explored:{{{%zu}}}, incr:{{{%zu}}} memdb:{{{%.0f}}}",
                 reach->runtime, db_elts, reach->trans, reach->stats->misses,
                 reach->stats->tests, reach->stats->rehashes, mem1, tot,
                 reach->explored, reach->stats->cache_misses, mem2);
    }
}

void
conf_thread (thread_ctx_t * ctx)
{
    char               *lbl = RTmalloc (sizeof (char[20]));
    snprintf (lbl, sizeof (char[20]), W>1?"%s[%zu]":"%s", program, ctx->id);
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

void        *
get_state(int idx, thread_ctx_t *ctx)
{
    raw_data_t state = get(dbs, idx, ctx->store);
    return UseTreeDBSLL==db_type ? TreeDBSLLdata(dbs, state) : state;
}

void
state_info_create_empty(state_info_t *state)
{
    state->tree = NULL;
    state->data = NULL;
    state->idx = DUMMY_IDX;
    state->group = GB_UNKNOWN_GROUP;
}

void
state_info_create(state_info_t *state, state_data_t data, tree_t tree,
                  idx_t idx, int group)
{
    state->data = data;
    state->tree = tree;
    state->idx = idx;
    state->group = group;
}

size_t
state_info_int_size ()
{
    return (state_info_size () + 3) / 4;
}

size_t
state_info_size ()
{
    size_t              idx_size = sizeof (idx_t);
    size_t              data_size = SLOT_SIZE * (UseDBSLL==db_type ? N : 2*N);
    size_t              state_info_size = refs ? idx_size : data_size;
    if (!refs && UseDBSLL==db_type &&  (Strat_NDFS==strategy ||
            Strat_NNDFS==strategy || trc_output || ZOBRIST))
        state_info_size += idx_size;
    return state_info_size;
}

void
state_info_serialize(state_info_t *state, raw_data_t data)
{
    if (refs) {
        ((idx_t*)data)[0] = state->idx;
    } else {
        if ( UseDBSLL==db_type ) {
            memcpy (data, state->data, sizeof (int[N]));
            if (trc_output)
                ((idx_t*)data+N)[0] = state->idx;
            else if(ZOBRIST)
                ((idx_t*)data+N)[0] = state->hash32; //TODO
        } else { // UseTreeDBSLL
            memcpy (data, state->tree, sizeof (int[2*N]));
        }
    }
}

void
state_info_deserialize(state_info_t *state, raw_data_t data, raw_data_t store)
{
    idx_t               idx = DUMMY_IDX;
    if (refs) {
        idx = ((idx_t*)data)[0];
        data = get(dbs, idx, store);
    } else if (trc_output || ZOBRIST) {
        idx = UseDBSLL==db_type ? ((idx_t*)data+N)[0] : TreeDBSLLindex(data);
    }
    if ( UseDBSLL==db_type ) {
        state_info_create(state, data, NULL, idx, GB_UNKNOWN_GROUP);
    } else {
        state_data_t        state_data = TreeDBSLLdata(dbs, data);
        state_info_create(state, state_data, data, idx, GB_UNKNOWN_GROUP);
    }
    state->hash32 = idx;
    //ctx->state.hash32 = DBSLLmemoized_hash(dbs, ctx->state.idx);
}

void
ndfs_state_info_serialize (state_info_t *state, raw_data_t data)
{
    if (refs) {
        ((idx_t*)data)[0] = state->idx;
    } else if ( UseDBSLL==db_type ) {
        ((idx_t*)data+N)[0] = state->idx;
        memcpy (data, state->data, sizeof (int[N]));
    } else { // UseTreeDBSLL
        memcpy (data, state->tree, sizeof (int[2*N]));
    }
}

void
ndfs_state_info_deserialize (state_info_t *state, raw_data_t data, state_data_t store)
{
    idx_t               idx = DUMMY_IDX;
    state_data_t        tree_data = NULL;
    if (data) {
        if (refs) {
            idx = ((idx_t*)data)[0];
            data = get(dbs, idx, store);
        } else if ( UseDBSLL==db_type ) {
            idx = ((idx_t*)data+N)[0];
        }
        if (UseTreeDBSLL==db_type) {
            tree_data = data;
            idx = TreeDBSLLindex(data);
            data = TreeDBSLLdata(dbs, data);
        }
    }
    state_info_create(state, data, tree_data, idx, GB_UNKNOWN_GROUP);
}

static void
find_dfs_stack_trace (thread_ctx_t *ctx, dfs_stack_t stack, int *trace)
{
    int                 i = dfs_stack_nframes(ctx->stack);
    state_info_t        state;

    // gather trace
    raw_data_t data = dfs_stack_pop(stack);
    ndfs_state_info_deserialize(&state, data, ctx->store);
    trace[i] = state.idx;
    while (i > 0) {
        dfs_stack_leave(stack);
        data = dfs_stack_pop(stack);
        ndfs_state_info_deserialize(&state, data, ctx->store);
        i--;
        trace[i] = state.idx;
    }
}

static void
ndfs_report_cycle(thread_ctx_t *ctx, state_info_t *cycle_closing_state)
{
    Warning(info,"accepting cycle found!");
    /* Write last state to stack to close cycle */
    raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
    ndfs_state_info_serialize (cycle_closing_state, stack_loc);
    if (trc_output) {
        size_t      level = dfs_stack_nframes(ctx->stack) + 1;
        int        *trace = (int*)RTmalloc(sizeof(int) * level);
        find_dfs_stack_trace(ctx, ctx->stack, trace);
        trc_env_t  *trace_env = trc_create(ctx->model,
                               (trc_get_state_f)get_state, trace[0], ctx);
        trc_write_trace(trace_env, trc_output, trace, level);
        RTfree (trace);
    }
    Warning(info,"exiting now!");
    pthread_exit( statistics (dbs) );
}

static          int dl_found = 0;
void
handle_deadlock(thread_ctx_t *ctx)
{
    if ( cas (&dl_found, 0, 1) ) {
        Warning(info,"Deadlock found in state");
        lb_stop(lb);
        if (trc_output) {
            model_t     m = ctx->model;
            idx_t       idx = ctx->state.idx;
            int         level = ctx->counters.level_max;
            trc_env_t  *trace_env = trc_create(m, (trc_get_state_f)get_state,
                                               start_idx, ctx);
            trc_find_and_write(trace_env, trc_output, (int)idx, level, (int*)parent_idx); //TODO: warning
        }
        Warning(info, "Exiting now");
    }
    pthread_exit( statistics(dbs) );
}

/*
 * NDFS algorithm by Courcoubetis et al.
 */

void
ndfs_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, ctx->store2, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    if ( UseDBSLL==db_type )
        find_or_put_dbs(&successor, NULL);
    else // UseTreeDBSLL
        find_or_put_tree(&successor, &ctx->state);

    if (ctx->COLOR == NDFS_RED && successor.idx == ctx->SEED)
        /* Found cycle back to the seed */
        ndfs_report_cycle(ctx, &successor);

    if ( ctx->COLOR != ndfs_get_color(&ctx->color, successor.idx) ) {
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        ndfs_state_info_serialize (&successor, stack_loc);
    }
}

static inline void
ndfs_explore_state (thread_ctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    int count = GBgetTransitionsAll(ctx->model, ctx->state.data, ndfs_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    if (ctx->COLOR == NDFS_RED)
        maybe_report (cnt, "R ", &red_threshold);
    else
        maybe_report (cnt, "B ", &threshold);
    if (cnt->explored > cnt->max_load) {
        if (lb_is_stopped(lb))
            pthread_exit( statistics(dbs) );
        cnt->max_load += 1000;
    }
}

/* RED DFS */
void
ndfs_red (thread_ctx_t *ctx, idx_t seed)
{
    size_t              seed_level = dfs_stack_nframes (ctx->stack);
    ctx->COLOR = NDFS_RED;
    ctx->SEED = seed;
    ctx->red.visited++; //count accepting states

    while (1) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_isset_or_set_color(&ctx->color, ctx->state.idx, NDFS_RED) ) {
                dfs_stack_pop (ctx->stack);
            } else {
                ndfs_explore_state(ctx, &ctx->red);
            }
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->COLOR = NDFS_BLUE;
}

/* BLUE DFS */
void
ndfs_blue (thread_ctx_t *ctx, size_t work)
{
    while (1) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_isset_or_set_color(&ctx->color, ctx->state.idx, NDFS_BLUE) ) {
                dfs_stack_pop (ctx->stack);
            } else {
                ndfs_explore_state(ctx, &ctx->counters);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;

            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            if( GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                ndfs_red(ctx, ctx->state.idx);

            dfs_stack_pop (ctx->stack);
        }
    }
/*a*/(void) work;
}

/*
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

void
nndfs_red_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, ctx->store2, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    if ( UseDBSLL==db_type )
        find_or_put_dbs(&successor, NULL);
    else // UseTreeDBSLL
        find_or_put_tree(&successor, &ctx->state);

    ndfs_color_t color = nndfs_get_color(&ctx->color, successor.idx);
    if (color == NDFS_CYAN) {
        /* Found cycle back to the stack */
        ndfs_report_cycle(ctx, &successor);
    } else if (color == NDFS_BLUE) {
        nndfs_isset_or_set_color(&ctx->color, ctx->state.idx, NDFS_RED);
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        ndfs_state_info_serialize (&successor, stack_loc);
    }
}

void
nndfs_blue_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, ctx->store2, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    if ( UseDBSLL==db_type )
        find_or_put_dbs(&successor, NULL);
    else // UseTreeDBSLL
        find_or_put_tree(&successor, &ctx->state);

    ndfs_color_t color = nndfs_get_color(&ctx->color, successor.idx);
    if (color == NDFS_CYAN && (GBbuchiIsAccepting(ctx->model,ctx->state.data) ||
                    GBbuchiIsAccepting(ctx->model, successor.data))) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, &successor);
    } else if (color == NDFS_WHITE) {
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        ndfs_state_info_serialize (&successor, stack_loc);
    }
}

static inline void
nndfs_explore_state (thread_ctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    TransitionCB cb = ctx->COLOR == NDFS_BLUE ? nndfs_blue_handle : nndfs_red_handle;
    int count = GBgetTransitionsAll(ctx->model, ctx->state.data, cb, ctx);
    cnt->trans += count;
    cnt->explored++;
    if (ctx->COLOR == NDFS_RED)
        maybe_report (cnt, "R ", &red_threshold);
    else
        maybe_report (cnt, "B ", &threshold);
    if (cnt->explored > cnt->max_load) {
        if (lb_is_stopped(lb))
            pthread_exit( statistics(dbs) );
        cnt->max_load += 1000;
    }
}

/* RED DFS */
void
nndfs_red (thread_ctx_t *ctx)
{
    size_t              start_level = dfs_stack_nframes (ctx->stack);
    ctx->COLOR = NDFS_RED;
    ctx->red.visited++; //count accepting states

    while (1) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            ndfs_color_t color = nndfs_get_color(&ctx->color, ctx->state.idx);
            if (color != NDFS_RED)  //TODO: why is this needed?
                nndfs_explore_state(ctx, &ctx->red);
            else
                dfs_stack_pop (ctx->stack);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (start_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->COLOR = NDFS_BLUE;
}

/* BLUE DFS */
void
nndfs_blue (thread_ctx_t *ctx, size_t work)
{
    while (1) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            ndfs_color_t color = nndfs_get_color(&ctx->color, ctx->state.idx);
            if (color == NDFS_WHITE) { //TODO: why is this needed?
                nndfs_isset_or_set_color (&ctx->color, ctx->state.idx, NDFS_CYAN);
                nndfs_explore_state (ctx, &ctx->counters);
            } else
                dfs_stack_pop (ctx->stack);
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;

            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            ndfs_state_info_deserialize (&ctx->state, state_data, ctx->store);
            if( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                nndfs_red (ctx);
                nndfs_isset_or_set_color(&ctx->color, ctx->state.idx, NDFS_RED);
            } else
                nndfs_isset_or_set_color(&ctx->color, ctx->state.idx, NDFS_BLUE);

            dfs_stack_pop (ctx->stack);
        }
    }
/*a*/(void) work;
}

/*
 * Reachability algorithms
 */

void
handle_state (void *arg, transition_info_t *ti, state_data_t dst)
{
    thread_ctx_t       *ctx = (thread_ctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, ctx->store2, DUMMY_IDX, ti->group);
    if (!find_or_put (&successor, &ctx->state)) {
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        state_info_serialize(&successor, stack_loc);
        if (trc_output)
            parent_idx[successor.idx] = ctx->state.idx;
        ctx->load++;
        ctx->counters.visited++;
    }
    if (files[1])
        stream_write (ctx->out, dst, sizeof (int[N]));
    ctx->counters.trans++;
}

static inline int
explore_state (thread_ctx_t *ctx, raw_data_t state, int next_index)
{
    if (0 == next_index && ctx->counters.level_cur >= max)
        return K;
    int                 count = 0;
    int                 i = K;
    state_info_deserialize (&ctx->state, state, ctx->store);
    if ( UseBlackBox == call_mode )
        count = GBgetTransitionsAll(ctx->model, ctx->state.data, handle_state, ctx);
    else // UseGreyBox
        for (i = next_index; i<K && count<MAX_SUCC; i++)
            count += GBgetTransitionsLong(ctx->model, i, ctx->state.data, handle_state, ctx);
    if ( dlk_detect && (0==count && 0==next_index) )
        handle_deadlock(ctx);
    ctx->counters.explored++;
    maybe_report (&ctx->counters, "", &threshold);
    return i;
}

void
dfs_grey (thread_ctx_t *ctx, size_t work)
{
    int                 next_index = 0;
    ctx->counters.max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < ctx->counters.max_load) {
        raw_data_t      state_data = dfs_stack_top (ctx->stack);
        if (NULL == state_data) {
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            next_index = isba_pop_int (ctx->group_stack)[0];
            continue;
        }
        if (next_index == K) {
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        } else {
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            next_index = explore_state (ctx, state_data, next_index);
            isba_push_int (ctx->group_stack, &next_index);
        }
        next_index = 0;
    }
}

void
dfs (thread_ctx_t * ctx, size_t work)
{
    ctx->counters.max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < ctx->counters.max_load) {
        raw_data_t      state_data = dfs_stack_top (ctx->stack);
        if (NULL == state_data) {
            if (0 == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        } else {
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            explore_state (ctx, state_data, 0);
        }
    }
}

void
bfs (thread_ctx_t *ctx, size_t work)
{
    ctx->counters.max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < ctx->counters.max_load) {
        raw_data_t      state_data = dfs_stack_top (ctx->in_stack);
        if (NULL == state_data) {
            if (0 == dfs_stack_frame_size (ctx->out_stack))
                return;
            dfs_stack_t         old = ctx->out_stack;
            ctx->stack = ctx->out_stack = ctx->in_stack;
            ctx->in_stack = old;
            ctx->counters.level_max++;
        } else {
            dfs_stack_pop (ctx->in_stack);
            ctx->load--;
            explore_state (ctx, state_data, 0);
        }
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
            source->counters.level_cur--;
            one = dfs_stack_pop (source->stack);
            dfs_stack_push (target->stack, one);
            dfs_stack_enter (target->stack);
            target->counters.level_cur++;
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
    ctx->counters.runtime = SCCrealTime (timer);
    return statistics (dbs);  // call results are thread dependent
}

int
main (int argc, char *argv[])
{
    char                name[128];
    model_t             model = init_globals (argc, argv);
    thread_ctx_t       *total = contexts[0];
    total->model = model;
    GBgetInitialState (model, total->store);
    transition_info_t start_trans_info = GB_NO_TRANSITION;
    if (Strat_NDFS == strategy || Strat_NNDFS == strategy) {
        total->COLOR = NDFS_BLUE;
        ndfs_handle (total, &start_trans_info, total->store);
    } else
        handle_state (total, &start_trans_info, total->store);
    raw_data_t          init_state = dfs_stack_top (total->stack);
    state_info_deserialize(&total->state, init_state, total->store);
    start_idx = total->state.idx;
    int                 start_group = 0;
    if (UseGreyBox == call_mode && Strat_DFS == strategy)
        isba_push_int (total->group_stack, &start_group);
    total->counters.trans = 0;
    mytimer_t           timer = SCCcreateTimer ();
    SCCstartTimer (timer);
    for (size_t i = 0; i < W; i++)
        pthread_create (&contexts[i]->me, attr, explore, contexts[i]);
    for (size_t i = 0; i < W; i++)
        pthread_join (contexts[i]->me, (void **)&contexts[i]->counters.stats);
    SCCstopTimer (timer);
    for (size_t i = 0; i < W; i++) {
        thread_ctx_t   *ctx = contexts[i];
        ctx->counters.stack_sizes = dfs_stack_size_max (ctx->in_stack);
        if (strategy == Strat_BFS)
            ctx->counters.stack_sizes += dfs_stack_size_max (ctx->out_stack);
        if (total != ctx)
            add_results (&total->counters, &ctx->counters);
        snprintf (name, sizeof name, "[%zu] saw in %.3f sec ", i, ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
        if (ctx->load) Warning (info, "Wrong load counter %zu", ctx->load);
        if (files[1]) stream_close (&ctx->out);
    }
    total->counters.level_max /= W; // not so meaningful for DFS
    if (RTverbosity >= 1)
        print_statistics(&total->counters, &total->red, timer);
    return EXIT_SUCCESS;
}
