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
typedef enum { Strat_BFS    = 1,
               Strat_DFS    = 2,
               Strat_NDFS   = 4,
               Strat_NNDFS  = 8,
               Strat_GNDFS  = 16,
               Strat_GNNDFS = 32,
               Strat_YNDFS  = 64,
               Strat_YNNDFS  = 128,
               Strat_LTLG   = Strat_GNDFS | Strat_GNNDFS | Strat_YNNDFS,
               Strat_LTLY   = Strat_YNDFS,
               Strat_LTLS   = Strat_LTLG | Strat_LTLY,
               Strat_LTL    = Strat_NNDFS | Strat_NDFS | Strat_LTLS,
               Strat_Reach  = Strat_BFS | Strat_DFS
} strategy_t;

/* TODO: merge into trace.c
 * where to define idx_t/state_data_t/...
 */
typedef idx_t (*trc_get_idx_f)(void *state, void *ctx);

/* permute_get_transitions is a replacement for GBgetTransitionsLong
 * TODO: move this to permute.c
 */
#define                 TODO_MAX 20

typedef enum {
    Perm_None,      /* normal group order */
    Perm_Shift,     /* shifted group order (lazy impl., thus cheap) */
    Perm_Shift_All, /* eq. to Perm_Shift, but non-lazy */
    Perm_Sort,      /* order on the state index in the DB */
    Perm_Random,    /* generate a random fixed permutation */
    Perm_SR,        /* sort according to a random fixed permutation */
    Perm_Otf,       /* on-the-fly calculation of a random perm for num_succ */
    Perm_Unknown    /* not set yet */
} permutation_perm_t;

typedef struct permute_todo_s {
    idx_t               idx;
    transition_info_t   ti;
} permute_todo_t;

typedef struct permute_s {
    void               *ctx;
    int               **rand;
    int                *otf;
    TransitionCB        real_cb;
    int                 start_group;
    int                 start_group_index;
    double              shift;
    uint32_t            shiftorder;
    permute_todo_t     *todos;
    size_t              nstored;
    size_t              trans;
    permutation_perm_t  permutation;
    trc_get_idx_f       get_idx;
    trc_get_state_f     get_state;
    model_t             model;

} permute_t;

/**
 * Create a permuter.
 * arguments:
 * permutation: see permutation_perm_t
 * shift: distance between shifts
 */
extern permute_t *permute_create (permutation_perm_t permutation, model_t model,
                                  trc_get_idx_f get_idx, trc_get_state_f get_state,
                                  size_t workers, size_t trans, int worker_index);
extern void permute_free (permute_t *perm);
extern int permute_trans (permute_t *perm, state_data_t state,
                                    TransitionCB cb, void *ctx);

static char            *program;
static cct_map_t       *tables = NULL;
static char            *files[2];
static int              dbs_size = 24;
static int              refs = 0;
static int              no_red_perm = 0;
static box_t            call_mode = UseBlackBox;
static size_t           max = UINT_MAX;
static size_t           W = 2;
static lb_t            *lb;
static void            *dbs;
static dbs_stats_f      statistics;
static dbs_get_f        get;
static dbs_get_sat_f    get_sat_bit;
static dbs_try_set_sat_f try_set_sat_bit;
static char            *state_repr = "table";
static db_type_t        db_type = UseDBSLL;
static char            *arg_strategy = "bfs";
static strategy_t       strategy = Strat_BFS;
static char            *arg_lb = "srp";
static lb_method_t      lb_method = LB_SRP;
static char            *arg_perm = "unknown";
static permutation_perm_t permutation = Perm_Unknown;
static permutation_perm_t permutation_red = Perm_Unknown;
static char*            trc_output=NULL;
static int              dlk_detect = 0;
static size_t           G = 100;
static size_t           H = MAX_HANDOFF_DEFAULT;
static int              ZOBRIST = 0;
static idx_t           *parent_idx=NULL;
static state_data_t     initial_state;

static si_map_entry strategies[] = {
    {"bfs",     Strat_BFS},
    {"dfs",     Strat_DFS},
    {"ndfs",    Strat_NDFS},
    {"nndfs",   Strat_NNDFS},
    {"gndfs",   Strat_GNDFS},
    {"yndfs",   Strat_YNDFS},
    {"gnndfs",  Strat_GNNDFS},
    {"ynndfs",  Strat_YNNDFS},
    {NULL, 0}
};

static si_map_entry permutations[] = {
    {"shift",   Perm_Shift},
    {"shiftall",Perm_Shift_All},
    {"sort",    Perm_Sort},
    {"otf",     Perm_Otf},
    {"random",  Perm_Random},
    {"sr",      Perm_SR},
    {"none",    Perm_None},
    {"unknown", Perm_Unknown},
    {NULL, 0}
};

static si_map_entry db_types[] = {
    {"table",   UseDBSLL},
    {"tree",    UseTreeDBSLL},
    {NULL, 0}
};

static si_map_entry lb_methods[] = {
    {"srp",     LB_SRP},
    {"static",  LB_Static},
    {"combined",LB_Combined},
    {"none",    LB_None},
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
        res = linear_search (permutations, arg_perm);
        if (res < 0)
            Fatal (1, error, "unknown permutation method %s", arg_perm);
        permutation = res;
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
     &arg_lb, 0, "select the load balancing method", "<srp|static|combined|none>"},
    {"perm", 'p', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_perm, 0, "select the transition permutation method",
     "<shift|shiftall|sort|random|none>"},
    {"no-red-perm", 0, POPT_ARG_VAL, &no_red_perm, 1, "turn off transition permutation for the red search", NULL},
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

/* TODO: move to color.c
 * NNDFS state colors are encoded using one bitset, where two consecutive bits
 * describe four colors: (thus state 0 uses bit 0 and 1, state 1, 2 and 3, ..)
 * Colors:
 * While: 0 0) first generated by next state call
 * Blue:  0 1) A state that has finished its blue search and has not yet been reached
 *             in a red search
 * Red:   1 0) A state that has been considered in both the blue and the red search
 * Cyan:  1 1) A state whose blue search has not been terminated
 */
typedef struct {
  int nn;
} nndfs_color_t;

enum { WHITE=0, BLUE=1, RED=2, CYAN=3 };
#define NNCOLOR(c) (nndfs_color_t){ .nn = (c) }
#define NNWHITE    NNCOLOR(WHITE) // value: 00
#define NNBLUE     NNCOLOR(BLUE)  // value: 01
#define NNRED      NNCOLOR(RED)   // value: 10
#define NNCYAN     NNCOLOR(CYAN)  // value: 11

static inline nndfs_color_t
nn_get_color (bitvector_t *set, int idx)
{ return (nndfs_color_t){ .nn = bitvector_get2 (set, idx<<1) };  }

static inline int
nn_set_color (bitvector_t *set, int idx, nndfs_color_t color)
{ return bitvector_isset_or_set2 (set, idx<<1, color.nn); }

static inline int
nn_color_eq (const nndfs_color_t a, const nndfs_color_t b)
{ return a.nn == b.nn; };

/* NDFS uses two colors which are independent of each other.
 * Blue: bit 0) A state that has finished its blue search and has not yet been reached
 *             in a red search
 * Red:  bit 1) A state that has been considered in both the blue and the red search
 */
typedef struct {
  int n;
} ndfs_color_t;

enum { IBLUE=0, IRED=1 };
#define NCOLOR(c)  (ndfs_color_t){ .n = (c) }
#define NBLUE      NCOLOR(IBLUE) // bit 0
#define NRED       NCOLOR(IRED)  // bit 1

static inline int
ndfs_has_color (bitvector_t *set, int idx, ndfs_color_t color)
{ return bitvector_is_set (set, (idx<<1)|color.n); }

static inline int
ndfs_try_color (bitvector_t *set, int idx, ndfs_color_t color)
{ return bitvector_isset_or_set (set, (idx<<1)|color.n); }

static inline int
n_color_eq (const ndfs_color_t a, const ndfs_color_t b)
{ return a.n == b.n; };


/* Global state colors are encoded in the state database as independent bits.
 * All threads are sensitive too them.
 *
 * Colors:
 * Green: bit 0) A state that globally is does not have to be considered in
 *               the blue search anymore.
 * Yellow:bit 1) A state that globally is does not have to be considered in
 *               the Red search anymore.
 */
typedef struct {
  int g;
} global_color_t;

enum { GREEN=0, YELLOW=1 };
#define GCOLOR(c)  (global_color_t){ .g = (c) }
#define GGREEN     GCOLOR(GREEN)  // bit 0
#define GYELLOW    GCOLOR(YELLOW) // bit 1


static inline int
global_has_color (int idx, global_color_t color)
{ return get_sat_bit (dbs, idx, color.g); }

static inline int //RED and BLUE are independent
global_try_color (int idx, global_color_t color)
{ return try_set_sat_bit (dbs, idx, color.g); }

static inline int
g_color_eq (const global_color_t a, const global_color_t b)
{ return a.g == b.g; };

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
    size_t              threshold;      // report threshold
} counter_t;

typedef struct thread_ctx_s {
    pthread_t           me;             // currently executing thread
    size_t              id;             // thread id (0..NUM_THREADS)
    stream_t            out;            // raw file output stream
    model_t             model;          // Greybox model
    state_data_t        store;          // temporary state storage1
    state_data_t        store2;         // temporary state storage2
    state_info_t        state;          // currently explored state
    dfs_stack_t         stack;          // Successor stack (for BFS and DFS)
    dfs_stack_t         in_stack;       // Input stack (for BFS)
    dfs_stack_t         out_stack;      // Output stack (for BFS)
    bitvector_t         color_map;      // Local NDFS coloring of states (idx-based)
    isb_allocator_t     group_stack;    // last explored group per frame (grey)
    counter_t           counters;       // reachability/NDFS_blue counters
    counter_t           red;            // NDFS_red counters
    size_t              load;           // queue load (for balancing)
    ndfs_color_t        search;         // current NDFS color
    idx_t               seed;           // current NDFS seed
    permute_t          *permute;        // transition permutor
    bitvector_t         not_all_red;    // all_red gaiser/Schwoon
} wctx_t;

/* predecessor --(transition_group)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     state_info_t *predecessor,
                                     state_data_t store);

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
extern void         dfs_grey (wctx_t *ctx, size_t work);
extern void         ndfs_blue (wctx_t *ctx, size_t work);
extern void         ndfs_green (wctx_t *ctx, size_t work);
extern void         nndfs_blue (wctx_t *ctx, size_t work);
extern void         gnn_blue (wctx_t *ctx, size_t work);
extern void         dfs (wctx_t *ctx, size_t work);
extern void         bfs (wctx_t *ctx, size_t work);
extern size_t       split_bfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs (size_t src_id, size_t dst_id, size_t handoff);
extern size_t       split_dfs_grey (size_t src_id, size_t dst_id, size_t handoff);

static find_or_put_f find_or_put;
static int          N;
static int          K;
static int          MAX_SUCC;           // max succ. count to expand at once
static size_t       threshold;
static pthread_attr_t  *attr = NULL;
static wctx_t     **contexts;
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
    res->touched_red += cnt->touched_red;
    if (NULL != res->stats && NULL != cnt->stats)
    add_stats(res->stats, cnt->stats);
}

static inline void
increase_level(counter_t *cnt)
{
    cnt->level_cur++;
    if(cnt->level_cur > cnt->level_max)
        cnt->level_max = cnt->level_cur;
}

void        *
get_state (int idx, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    raw_data_t state = get (dbs, idx, ctx->store2);
    return UseTreeDBSLL==db_type ? TreeDBSLLdata(dbs, state) : state;
}

idx_t
get_idx (void *state, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, state, NULL, DUMMY_IDX, GB_UNKNOWN_GROUP);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);
    return successor.idx;
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

wctx_t *
wctx_create (size_t id)
{
    assert (NULL == 0);
    wctx_t             *ctx = RTalignZero (CACHE_LINE_SIZE, sizeof (wctx_t));
    memset (ctx, 0, sizeof (wctx_t));
    ctx->id = id;
    ctx->model = NULL;
    state_info_create_empty (&ctx->state);
    ctx->store = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->store2 = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->stack = dfs_stack_create (state_info_int_size());
    ctx->out_stack = ctx->in_stack = ctx->stack;
    if (strategy == Strat_BFS)
        ctx->in_stack = dfs_stack_create (state_info_int_size());
    //allocate two bits for NDFS colorings
    if (strategy & Strat_LTL) {
        bitvector_create_large (&ctx->color_map, 2<<dbs_size);
        bitvector_create_large (&ctx->not_all_red, 20000000); //magic number for the largest stack i've encountered.
    } else if ( UseGreyBox == call_mode && Strat_DFS == strategy)
        ctx->group_stack = isba_create (1);
    if (files[1]) {
        char               name[PATH_MAX];
        int ret = snprintf (name, sizeof name, "%s-%zu", files[1], id);
        assert (ret < (int)sizeof name);
        ctx->out = file_output (name);
        stream_write (ctx->out, &K, 4);
    }
    ctx->search = NBLUE;
    ctx->counters.threshold = ctx->red.threshold = threshold;
    ctx->permute = permute_create (permutation, NULL, get_idx, get_state, W, K, id);
    return ctx;
}

void
wctx_free (wctx_t *ctx)
{
    RTfree (ctx->store);
    RTfree (ctx->store2);
    dfs_stack_destroy (ctx->out_stack);
    //if (NULL != ctx->model)
    //    GBfree (ctx->model);
    if (strategy & Strat_LTL) {
        bitvector_free (&ctx->color_map);
        bitvector_free (&ctx->not_all_red);
    }
    if (NULL != ctx->group_stack)
        isba_destroy (ctx->group_stack);
    if (strategy == Strat_BFS)
        dfs_stack_destroy (ctx->in_stack);
    if (files[1]) {
        stream_flush (ctx->out);
        stream_close (&ctx->out);
    }
    if (NULL != ctx->permute)
        permute_free (ctx->permute);
    RTfree (ctx);
}

static uint32_t
z_rehash (const void *v, int b, uint32_t seed)
{
    return zobrist_rehash (zobrist, seed);
    (void)b; (void)v;
}

static int
find_or_put_zobrist (state_info_t *state, state_info_t *pred, state_data_t store)
{
    state->hash32 = zobrist_hash_dm (zobrist, state->data, pred->data,
                                            pred->hash32, state->group);
    return DBSLLlookup_hash (dbs, state->data, &state->idx, &state->hash32);
    (void) store;
}

static int
find_or_put_dbs (state_info_t *state, state_info_t *predecessor, state_data_t store)
{
    return DBSLLlookup_hash (dbs, state->data, &state->idx, NULL);
    (void) predecessor; (void) store;
}

static int
find_or_put_tree (state_info_t *s, state_info_t *pred, state_data_t store)
{
    int                 ret;
    ret = TreeDBSLLlookup_dm (dbs, s->data, pred->tree, store, s->group);
    s->tree = store;
    s->idx = TreeDBSLLindex(s->tree);
    return ret;
}

void
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
    if (Perm_Unknown == permutation) //default permutation depends on strategy
        permutation = strategy & Strat_Reach ? Perm_None : Perm_Shift;
    if (strategy & Strat_LTL) {
        if ( !(GBhasProperty(model) == PROPERTY_LTL_SPIN ||
               GBhasProperty(model) == PROPERTY_LTL_TEXTBOOK))
            Warning(info, "No properties found.\n"
                    "NDFS search only works in combination with a never claim\n"
                    "(use --ltl or supply a Buchi product as input model).");
        if (call_mode == UseGreyBox)
            Warning(info, "Greybox not supported with strategy NDFS, ignored.");
        lb_method = LB_None;
        threshold = 100000;
        permutation_red = no_red_perm ? Perm_None : permutation;
        refs = 1; //The permuter works with references only!
    } else {
        if (permutation != Perm_None)
            Fatal(1, error, "Transition permutation is not supported for reachability algorithms");
        if (trc_output) {
            parent_idx = RTmalloc(sizeof(int[1<<dbs_size]));
            dlk_detect = 1;
        }
        threshold = 100000 / W;
    }
#ifndef __APPLE__
    pthread_barrier_init (&start_barrier, NULL, W);
#endif
    Warning (info, "Using %d cores.", W);
    Warning (info, "loading model from %s", files[0]);
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

    /* for --grey: */
    MAX_SUCC = ( Strat_DFS == strategy ? 1 : INT_MAX );

    int                 global_bits = Strat_LTLS & strategy ? 2 : 0;
    switch (db_type) {
    case UseDBSLL:
        if (ZOBRIST) {
            //TODO: fix zobrist with hash on stack, now memoized hash is broken
            zobrist = zobrist_create (N, ZOBRIST, m);
            find_or_put = find_or_put_zobrist;
            dbs = DBSLLcreate_sized (N, dbs_size, (hash32_f)z_rehash, global_bits);
        } else {
            find_or_put = find_or_put_dbs;
            dbs = DBSLLcreate_sized (N, dbs_size, (hash32_f)SuperFastHash, global_bits);
        }
        statistics = (dbs_stats_f) DBSLLstats;
        get = (dbs_get_f) DBSLLget;
        get_sat_bit = (dbs_get_sat_f) DBSLLget_sat_bit;
        try_set_sat_bit = (dbs_try_set_sat_f) DBSLLtry_set_sat_bit;
        break;
    case UseTreeDBSLL:
        if (ZOBRIST)
            Fatal (1, error, "Zobrist and treedbs is not implemented");
        statistics = (dbs_stats_f) TreeDBSLLstats;
        get = (dbs_get_f) TreeDBSLLget;
        find_or_put = find_or_put_tree;
        dbs = TreeDBSLLcreate_dm (N, dbs_size, m, global_bits);
        get_sat_bit = (dbs_get_sat_f) TreeDBSLLget_sat_bit;
        try_set_sat_bit = (dbs_try_set_sat_f) TreeDBSLLtry_set_sat_bit;
        break;
    }
    contexts = RTmalloc (sizeof (wctx_t *[W]));
    for (size_t i = 0; i < W; i++)
        contexts[i] = wctx_create (i);
    contexts[0]->model = model;

    initial_state = RTmalloc (SLOT_SIZE * N);
    GBgetInitialState (model, initial_state);

    /* Load balancer assigned last, see exit_ltsmin */
    switch (strategy) {
    case Strat_GNNDFS:
        lb = lb_create_max (W, (algo_f)gnn_blue,   NULL,G, lb_method, H); break;
    case Strat_YNNDFS:
    case Strat_NNDFS:
        lb = lb_create_max (W, (algo_f)nndfs_blue, NULL,G, lb_method, H); break;
    case Strat_GNDFS:
        lb = lb_create_max (W, (algo_f)ndfs_green, NULL,G, lb_method, H); break;
    case Strat_YNDFS:
    case Strat_NDFS:
        lb = lb_create_max (W, (algo_f)ndfs_blue,  NULL,G, lb_method, H); break;
    case Strat_BFS:
        lb = lb_create_max (W, (algo_f)bfs, split_bfs,  G, lb_method, H); break;
    case Strat_DFS: {
        algo_f algo = call_mode == UseGreyBox ? (algo_f)dfs_grey : (algo_f)dfs;
        lb = lb_create_max (W, algo,        split_dfs,  G, lb_method, H); break;}
    default:
        Fatal(1, error, "Unknown strategy.");
    }
    (void) signal(SIGINT, exit_ltsmin);

    if (RTverbosity >= 3) {
        fprintf (stderr, "Dependency Matrix:\n");
        GBprintDependencyMatrixCombined (stderr, model);
    }
}

void
deinit_globals ()
{
    if (db_type == UseDBSLL)
        DBSLLfree (dbs);
    else //TreeDBSLL
        TreeDBSLLfree (dbs);
    lb_destroy (lb);
    RTfree (initial_state);
    for (size_t i = 0; i < W; i++)
        wctx_free (contexts[i]);
    RTfree (contexts);
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
    if (W == 1 || strategy & Strat_LTL)
        print_state_space_total (msg, cnt);
    else
        Warning (info, "%s%zu levels ±%zu states ±%zu transitions", msg,
                 cnt->level_max, W * cnt->explored,  W * cnt->trans);
}

static inline void
ndfs_maybe_report (ndfs_color_t color, counter_t *cnt)
{
    maybe_report (cnt, n_color_eq(color, NRED)?"[R] ":"[B] ", &cnt->threshold);
}

void
print_statistics(counter_t *reach, counter_t *red, mytimer_t timer)
{
    char               *name;
    double              mem1, mem2, compr, ratio;
    float               tot = SCCrealTime (timer);
    size_t              db_elts = reach->stats->elts;
    size_t              db_nodes = reach->stats->nodes;
    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    size_t              el_size = db_type == UseTreeDBSLL ? 3 : N;
    size_t              s = state_info_size();
    mem1 = (double)s * reach->stack_sizes / (1 << 10);

    reach->level_max /= W; // not so meaningful for DFS
    if (Strat_LTL & strategy) {
        reach->explored /= W;
        reach->trans /= W;
        red->explored /= W;
        red->trans /= W;
        red->level_max /= W;
        if ( 0 == (Strat_LTLG & strategy) )
            red->visited /= W;
        SCCreportTimer (timer, "Total exploration time");

        Warning (info, "");
        Warning (info, "%s(%s/%s) stats:", key_search(strategies, strategy),
                 key_search(permutations, permutation), key_search(permutations, permutation_red));
        Warning (info, "State space has %zu states, %zu are accepting", db_elts,
                 red->visited);
        Warning (info, "avg blue states/worker: %zu (%.2f%%), transitions: %zu ",
                 reach->explored, ((double)reach->explored/db_elts)*100, reach->trans);
        Warning (info, "avg red states/worker: %zu (%.2f%%), transitions: %zu ",
                 red->explored, ((double)red->explored/db_elts)*100, red->trans);
        Warning (info, "");
        Warning (info, "red in red: %zu", red->touched_red);
    } else {
        Warning (info, "")
        print_state_space_total ("State space has ", reach);
        SCCreportTimer (timer, "Total exploration time");
    }

    Warning(info, "")
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.0fKB",
             s, reach->stack_sizes, mem1);
    mem2 = ((double)(1UL << (dbs_size)) / (1<<20)) * sizeof (int[el_size]);
    compr = (double)(db_nodes * el_size) / (N * db_elts) * 100;
    ratio = (double)((db_elts * 100) / (1UL << dbs_size));
    name = db_type == UseTreeDBSLL ? "Tree" : "Table";
    Warning (info, "DB: %s, memory: %.1fMB, compr. ratio: %.1f%%, "
             "fill ratio: %.1f%%", name, mem2, compr, ratio);
    if (RTverbosity >= 2) {        // detailed output for scripts
        Warning (info, "time:{{{%.2f}}}, elts:{{{%zu}}}, nodes:{{{%zu}}}, "
                 "trans:{{{%zu}}}, misses:{{{%zu}}}, tests:{{{%zu}}}, "
                 "rehashes:{{{%zu}}}, memq:{{{%.0f}}}, tt:{{{%.2f}}}, "
                 "explored:{{{%zu}}}, memdb:{{{%.0f}}}",
                 reach->runtime, db_elts, db_nodes, reach->trans,
                 reach->stats->misses, reach->stats->tests,
                 reach->stats->rehashes, mem1, tot, reach->explored, mem2);
    }
}

static void
print_thread_statistics(wctx_t *ctx)
{
    char                name[128];
    char               *format = "[%zu%s] saw in %.3f sec ";
    if (Strat_Reach & strategy) {
        if (strategy == Strat_BFS)
            ctx->counters.stack_sizes += dfs_stack_size_max (ctx->out_stack);
        snprintf (name, sizeof name, format, ctx->id, "", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
    } else if (Strat_LTL & strategy) {
        snprintf (name, sizeof name, format, ctx->id, " B", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
        snprintf (name, sizeof name, format, ctx->id, " R", ctx->counters.runtime);
        print_state_space_total (name, &ctx->red);
    }
    if (ctx->load) Warning (info, "Wrong load counter %zu", ctx->load);
}

/** Fisher / Yates GenRandPerm*/
static void
randperm (int *perm, int n, uint32_t seed)
{
    srandom (seed);
    for (int i=0; i<n; i++)
        perm[i] = i;
    for (int i=0; i<n; i++) {
        int                 j = random()%(n-i)+i;
        int                 t = perm[j];
        perm[j] = perm[i];
        perm[i] = t;
    }
}

#if defined(__CYGWIN__)
    #include <search.h>
    #define qsortr(a,b,c,d,e) qsort_s (a,b,c,d,e)
#elif defined(linux)
    #define qsortr(a,b,c,d,e) qsort_r (a,b,c,d,e)
#else //BSD
    #define qsortr(a,b,c,d,e) qsort_r (a,b,c,e,d)
#endif

static int
#ifdef linux //See also: define for qsortr
sort_cmp (const void *a, const void *b, void *arg)
#else
sort_cmp (void *arg, const void *a, const void *b)
#endif
{
    return ((permute_todo_t*)a)->idx - (((permute_todo_t*)b)->idx + (*(uint32_t*)arg));
}

static int
#ifdef linux //See also: define for qsortr
rand_cmp (const void *a, const void *b, void *arg)
#else
rand_cmp (void *arg, const void *a, const void *b)
#endif
{
    int                *rand = (int*)arg;
    return rand[((permute_todo_t*)a)->ti.group] -
           rand[((permute_todo_t*)b)->ti.group];
}

static inline void
store_todo (permute_t *perm, state_data_t dst, transition_info_t *ti)
{
    assert (perm->nstored < perm->trans+TODO_MAX);
    perm->todos[ perm->nstored ].idx = perm->get_idx (dst, perm->ctx);
    perm->todos[ perm->nstored ].ti.group = ti->group; //TODO: copy labels?
    perm->nstored++;
}

static inline void
empty_todo (permute_t *perm)
{
    for (size_t i = 0; i < perm->nstored; i++) {
        void           *succ = perm->get_state (perm->todos[i].idx, perm->ctx);
        perm->real_cb (perm->ctx, &perm->todos[i].ti, succ);
    }
}

permute_t *
permute_create (permutation_perm_t permutation, model_t model,
                trc_get_idx_f get_idx, trc_get_state_f get_state,
                size_t workers, size_t trans, int worker_index)
{
    permute_t          *perm = RTalign (CACHE_LINE_SIZE, sizeof(permute_t));
    perm->todos = RTalign (CACHE_LINE_SIZE, sizeof(permute_todo_t[trans+TODO_MAX]));
    perm->shift = ((double)trans)/workers;
    perm->shiftorder = INT_MAX/workers * worker_index;
    perm->start_group = perm->shift * worker_index;
    perm->trans = trans;
    perm->get_idx = get_idx;
    perm->get_state = get_state;
    perm->model = model;
    perm->permutation = permutation;
    if (Perm_Otf == permutation)
        perm->otf = RTalign (CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
    if (Perm_Random == permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*[trans+TODO_MAX]));
        for (size_t i = 1; i < perm->trans+TODO_MAX; i++) {
            perm->rand[i] = RTalign(CACHE_LINE_SIZE, sizeof(int[ i ]));
            randperm (perm->rand[i], i, i+perm->shiftorder);
        }
    }
    if (Perm_SR == permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign(CACHE_LINE_SIZE, sizeof(int[trans]));
        randperm (perm->rand[0], trans, (time(NULL) + 9876*worker_index));
    }
    return perm;
}

void
permute_set_model (permute_t *perm, model_t model)
{
    perm->model = model;
}

void
permute_free (permute_t *perm)
{
    RTfree (perm->todos);
    if (Perm_Otf == permutation)
        RTfree (perm->otf);
    if (Perm_Random == perm->permutation) {
        for (size_t i = 0; i < perm->trans+TODO_MAX; i++)
            RTfree (perm->rand[i]);
        RTfree (perm->rand);
    }
    if (Perm_SR == perm->permutation) {
        RTfree (perm->rand[0]);
        RTfree (perm->rand);
    }
    RTfree (perm);
}

static void
permute_one (void *arg, transition_info_t *ti, state_data_t dst)
{
    permute_t         *perm = (permute_t*) arg;
    switch (perm->permutation) {
    case Perm_Shift:
        if (ti->group >= perm->start_group)
            perm->real_cb (perm->ctx, ti, dst);
        else
            store_todo (perm, dst, ti);
        break;
    case Perm_Shift_All:
        if (0 == perm->start_group_index && ti->group >= perm->start_group)
            perm->start_group_index = perm->nstored;
    case Perm_Random:
    case Perm_SR:
    case Perm_Otf:
    case Perm_Sort:
        store_todo (perm, dst, ti);
        break;
    default:
        Fatal(1, error, "Unknown permutation!");
    }
}

int
permute_trans (permute_t *perm, state_data_t state, TransitionCB cb, void *ctx)
{
    if (Perm_None == perm->permutation)
        return GBgetTransitionsAll(perm->model, state, cb, ctx);
    perm->ctx = ctx;
    perm->real_cb = cb;
    perm->nstored = 0;
    perm->start_group_index = 0;
    int count = GBgetTransitionsAll(perm->model, state, permute_one, perm);
    size_t                  n = perm->nstored,
                            j;
    void                   *succ;
    switch (perm->permutation) {
    case Perm_Otf:
        randperm (perm->otf, n, ((wctx_t*)ctx)->state.idx + perm->shiftorder);
        for (size_t i = 0; i < perm->nstored; i++) {
            size_t          j = perm->otf[i];
            void           *succ = perm->get_state (perm->todos[j].idx, ctx);
            cb (ctx, &perm->todos[j].ti, succ);
        }
        break;
    case Perm_Random:
        for (size_t i = 0; i < perm->nstored; i++) {
            j = perm->rand[n][i];
            succ = perm->get_state (perm->todos[j].idx, ctx);
            cb (ctx, &perm->todos[j].ti, succ);
        }
        break;
    case Perm_SR:
        qsortr (perm->todos, n, sizeof(permute_todo_t), rand_cmp, *perm->rand);
        empty_todo (perm);
        break;
    case Perm_Sort:
        qsortr (perm->todos, n, sizeof(permute_todo_t), sort_cmp, &perm->shiftorder);
        empty_todo (perm);
        break;
    case Perm_Shift:
        empty_todo (perm);
        break;
    case Perm_Shift_All:
        for (size_t i = 0; i < perm->nstored; i++) {
            j = (perm->start_group_index + i);
            j = j < perm->nstored ? j : 0;
            succ = perm->get_state (perm->todos[j].idx, ctx);
            cb (ctx, &perm->todos[j].ti, succ);
        }
        break;
    default:
        Fatal(1, error, "Unknown permutation!");
    }
    return count;
}

/**
 * Algo's state representation and serialization / deserialization
 * TODO: move this to state_info.c
 */

void
state_info_create_empty(state_info_t *state)
{
    state->tree = NULL;
    state->data = NULL;
    state->idx = DUMMY_IDX;
    state->group = GB_UNKNOWN_GROUP;
}

void
state_info_create (state_info_t *state, state_data_t data, tree_t tree,
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
    if (!refs && UseDBSLL==db_type)
        state_info_size += idx_size;
    return state_info_size;
}

void
state_info_serialize (state_info_t *state, raw_data_t data)
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
state_info_deserialize (state_info_t *state, raw_data_t data, state_data_t store)
{
    idx_t               idx = DUMMY_IDX;
    state_data_t        tree_data = NULL;
    if (data) {
        if (refs) {
            idx = ((idx_t*)data)[0];
            data = get (dbs, idx, store);
        } else if ( UseDBSLL==db_type ) {
            idx = ((idx_t*)data+N)[0];
        }
        if (ZOBRIST) state->hash32 = DBSLLmemoized_hash (dbs, idx);
        if (UseTreeDBSLL==db_type) {
            tree_data = data;
            idx = TreeDBSLLindex (data);
            data = TreeDBSLLdata (dbs, data);
        }
    }
    state_info_create (state, data, tree_data, idx, GB_UNKNOWN_GROUP);
}

static void
find_dfs_stack_trace (wctx_t *ctx, dfs_stack_t stack, int *trace)
{
    // gather trace
    state_info_t        state;
    for (int i = dfs_stack_nframes (ctx->stack)-1; i > 0; i--) {
        dfs_stack_leave (stack);
        raw_data_t          data = dfs_stack_pop (stack);
        state_info_deserialize (&state, data, ctx->store);
        trace[i] = state.idx;
    }
}

static void
ndfs_report_cycle (wctx_t *ctx, state_info_t *cycle_closing_state)
{
    /* Stop other workers, exit if some other worker was first here */
    if ( !lb_stop(lb) )
        return;
    size_t              level = dfs_stack_nframes (ctx->stack) + 1;
    Warning (info, "Accepting cycle FOUND at depth %zu!", level);
    if (trc_output) {
        int                *trace = (int*)RTmalloc(sizeof(int) * level*10);
        /* Write last state to stack to close cycle */
        trace[level-1] = cycle_closing_state->idx;
        find_dfs_stack_trace (ctx, ctx->stack, trace);
        trc_env_t          *trace_env = trc_create (ctx->model,
                               (trc_get_state_f)get_state, trace[0], ctx);
        trc_write_trace (trace_env, trc_output, trace, level);
        RTfree (trace);
    }
    Warning (info,"Exiting now!");
}

static void
handle_deadlock (wctx_t *ctx)
{
    /* Stop other workers, exit if some other worker was first here */
    if ( !lb_stop(lb) )
        return;
    size_t              level = ctx->counters.level_cur;
    Warning (info,"Deadlock found in state at depth %zu!", level);
    if (trc_output) {
        idx_t       start_idx = get_idx (initial_state, ctx); //TODO: int <> idx
        trc_env_t  *trace_env = trc_create (ctx->model, (trc_get_state_f)get_state, start_idx, ctx);
        trc_find_and_write (trace_env, trc_output, (int)ctx->state.idx, level, (int*)parent_idx);
    }
    Warning (info, "Exiting now!");
}

/*
 * NDFS algorithm by Courcoubetis et al.
 */

/* ndfs_handle and ndfs_explore_state can be used by blue and red search */
static void
ndfs_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    if ( n_color_eq(ctx->search, NRED) && successor.idx == ctx->seed )
        /* Found cycle back to the seed */
        ndfs_report_cycle (ctx, &successor);

    if ( !ndfs_has_color(&ctx->color_map, successor.idx, ctx->search) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static inline void
ndfs_explore_state (wctx_t *ctx, counter_t *cnt)
{
    int                 count;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    count = permute_trans (ctx->permute, ctx->state.data, ndfs_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    ndfs_maybe_report(ctx->search, cnt);
}

static void
ndfs_handle_green (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    /* put on temporary stack to be sorted later */
    if ( !global_has_color(successor.idx, GGREEN) &&
         !ndfs_has_color (&ctx->color_map, successor.idx, NBLUE) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static inline void
ndfs_explore_state_green (wctx_t *ctx)
{
    dfs_stack_enter (ctx->stack);
    increase_level (&ctx->counters);
    int count = permute_trans (ctx->permute, ctx->state.data, ndfs_handle_green, ctx);
    ctx->counters.trans += count;
    ctx->counters.explored++;
    ndfs_maybe_report (ctx->search, &ctx->counters);
}

/* YELLOW DFS */
static void
ndfs_yellow (wctx_t *ctx, idx_t seed)
{
    size_t              seed_level = dfs_stack_nframes (ctx->stack);
    ctx->search = NRED; ctx->permute->permutation = permutation_red;
    ctx->seed = seed;
    ctx->red.visited++; //count accepting states

    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( global_has_color(ctx->state.idx, GYELLOW) ||
                 ndfs_try_color(&ctx->color_map, ctx->state.idx, NRED)) {
                if (seed_level == dfs_stack_nframes (ctx->stack))
                    break;
                dfs_stack_pop (ctx->stack);
            } else
                ndfs_explore_state (ctx, &ctx->red);
        } else { //backtrack
            if (seed_level != dfs_stack_nframes (ctx->stack)) {
                dfs_stack_leave (ctx->stack);
                ctx->red.level_cur--;
            }

            /* mark state as YELLOW */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            global_try_color (ctx->state.idx, GYELLOW);

            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes (ctx->stack))
                break;

            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->search = NBLUE; ctx->permute->permutation = permutation;
}

/* RED DFS */
static void
ndfs_red (wctx_t *ctx, idx_t seed)
{
    size_t              seed_level = dfs_stack_nframes (ctx->stack);
    ctx->search = NRED; ctx->permute->permutation = permutation_red;
    ctx->seed = seed;
    ctx->red.visited++; //count accepting states

    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_try_color(&ctx->color_map, ctx->state.idx, NRED) ) {
                if (seed_level == dfs_stack_nframes (ctx->stack))
                    break;
                dfs_stack_pop (ctx->stack);
            } else
                ndfs_explore_state (ctx, &ctx->red);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;

            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }
    ctx->search = NBLUE; ctx->permute->permutation = permutation;
}

/* BLUE DFS */
void
ndfs_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_try_color(&ctx->color_map, ctx->state.idx, NBLUE) )
                dfs_stack_pop (ctx->stack);
            else
                ndfs_explore_state (ctx, &ctx->counters);
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;

            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                if (Strat_YNDFS == strategy)
                    ndfs_yellow (ctx, ctx->state.idx);
                else
                    ndfs_red (ctx, ctx->state.idx);
            }
            dfs_stack_pop(ctx->stack);
        }
    }
/*a*/(void) work;
}

/* GREEN NDFS is BLUE DFS with GREEN (global) in the backtrack */
void
ndfs_green (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( global_has_color(ctx->state.idx, GGREEN)  ||
                 ndfs_try_color(&ctx->color_map, ctx->state.idx, NBLUE) )
                dfs_stack_pop (ctx->stack);
            else
                ndfs_explore_state_green (ctx);
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;

            /* call red DFS for accepting states
             * AND mark globally GREEN */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( global_try_color(ctx->state.idx, GGREEN) &&
                 GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                ndfs_red (ctx, ctx->state.idx);

            dfs_stack_pop(ctx->stack);
        }
    }
/*a*/(void) work;
}

/*
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

static void
nndfs_red_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    nndfs_color_t color = nn_get_color(&ctx->color_map, successor.idx);
    if ( nn_color_eq(color, NNCYAN) ) {
        /* Found cycle back to the stack */
        ndfs_report_cycle(ctx, &successor);
    } else if ( nn_color_eq(color, NNBLUE) ) {
        nn_set_color(&ctx->color_map, ctx->state.idx, NNRED);
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static void
nndfs_blue_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    nndfs_color_t color = nn_get_color (&ctx->color_map, successor.idx);
    if ( nn_color_eq(color, NNCYAN) &&
            (GBbuchiIsAccepting(ctx->model,ctx->state.data) ||
             GBbuchiIsAccepting(ctx->model, successor.data)) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, &successor);
    } else if ( !(nn_color_eq(color, NNRED) || (strategy == Strat_YNNDFS &&
                  global_has_color(ctx->state.idx, GYELLOW))) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static inline void
nndfs_explore_state_red (wctx_t *ctx, counter_t *cnt)
{
    int                 count;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    count = permute_trans (ctx->permute, ctx->state.data, nndfs_red_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    ndfs_maybe_report(ctx->search, cnt);
}

static inline void
nndfs_explore_state_blue (wctx_t *ctx, counter_t *cnt)
{
    int                 count;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    count = permute_trans (ctx->permute, ctx->state.data, nndfs_blue_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    ndfs_maybe_report(ctx->search, cnt);
}

/* YELLOW (NN)DFS */
static void
nndfs_yellow (wctx_t *ctx)
{
    size_t              start_level = dfs_stack_nframes (ctx->stack);
    ctx->search = NRED; ctx->permute->permutation = permutation_red;
    ctx->red.visited++; //count accepting states

    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.idx);
            if ( nn_color_eq(color, NNRED) ||
                 global_has_color(ctx->state.idx, GYELLOW) ) {
                if (start_level == dfs_stack_nframes (ctx->stack)) {
                     break; ctx->red.touched_red++;
                }
                dfs_stack_pop (ctx->stack);
            } else
                nndfs_explore_state_red (ctx, &ctx->red);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;

            /* mark state as YELLOW */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            global_try_color (ctx->state.idx, GYELLOW);

            /* exit search if backtrack hits seed, leave stack the way it was */
            if (start_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->search = NBLUE; ctx->permute->permutation = permutation;
}


/* RED DFS */
static void
nndfs_red (wctx_t *ctx)
{
    size_t              start_level = dfs_stack_nframes (ctx->stack);
    ctx->search = NRED; ctx->permute->permutation = permutation_red;
    ctx->red.visited++; //count accepting states

    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.idx);
            if ( nn_color_eq(color, NNRED)  ) {
                if (start_level == dfs_stack_nframes (ctx->stack)) {
                     break; ctx->red.touched_red++;
                }
                dfs_stack_pop (ctx->stack);
            } else
                nndfs_explore_state_red (ctx, &ctx->red);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (start_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->search = NBLUE; ctx->permute->permutation = permutation;
}

/* BLUE DFS */
void
nndfs_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.idx);
            if ( nn_color_eq(color, NNWHITE) && (strategy != Strat_YNNDFS ||
                 !global_has_color(ctx->state.idx, GYELLOW)) ) {
                nn_set_color (&ctx->color_map, ctx->state.idx, NNCYAN);
                nndfs_explore_state_blue (ctx, &ctx->counters);
            } else {
                if ( !(nn_color_eq(color, NNRED) || (strategy == Strat_YNNDFS &&
                       global_has_color(ctx->state.idx, GYELLOW))) )
                    bitvector_set ( &ctx->not_all_red, ctx->counters.level_cur );
                dfs_stack_pop (ctx->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;

            dfs_stack_leave (ctx->stack);

            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( !bitvector_is_set(&ctx->not_all_red, ctx->counters.level_cur) ) {
                nn_set_color (&ctx->color_map, ctx->state.idx, NNRED);
                if (strategy == Strat_YNNDFS)
                    global_try_color (ctx->state.idx, GYELLOW);
                bitvector_unset ( &ctx->not_all_red, ctx->counters.level_cur );
            } else if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                if (strategy == Strat_YNNDFS)
                    nndfs_yellow (ctx);
                else
                    nndfs_red (ctx);
                nn_set_color(&ctx->color_map, ctx->state.idx, NNRED);
            } else {
                nn_set_color(&ctx->color_map, ctx->state.idx, NNBLUE);
            }
            ctx->counters.level_cur--;

            dfs_stack_pop (ctx->stack);
        }
    }
/*a*/(void) work;
}

/*
 * Cyan NDFS algorithm by Jaco vd Pol
 */

static void
gnn_red_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create(&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    nndfs_color_t color = nn_get_color(&ctx->color_map, successor.idx);
    if ( nn_color_eq(color, NNCYAN) ) {
        /* Found cycle back to the stack */
        ndfs_report_cycle(ctx, &successor);
    } else if ( nn_color_eq(color, NNBLUE) ) {
        nn_set_color(&ctx->color_map, ctx->state.idx, NNRED);
        raw_data_t stack_loc = dfs_stack_push(ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static void
gnn_blue_handle (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, dst, NULL, DUMMY_IDX, ti->group);
    /* retrieve IDX from state database */
    find_or_put (&successor, &ctx->state, ctx->store2);

    nndfs_color_t color = nn_get_color (&ctx->color_map, successor.idx);
    if ( nn_color_eq(color, NNCYAN) &&
            (GBbuchiIsAccepting(ctx->model,ctx->state.data) ||
             GBbuchiIsAccepting(ctx->model, successor.data)) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, &successor);
    } else if ( nn_color_eq(color, NNWHITE) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
    }
}

static inline void
gnn_explore_state_red (wctx_t *ctx, counter_t *cnt)
{
    int                 count;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    count = permute_trans (ctx->permute, ctx->state.data, gnn_red_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    ndfs_maybe_report(ctx->search, cnt);
}

static inline void
gnn_explore_state_blue (wctx_t *ctx, counter_t *cnt)
{
    int                 count;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    count = permute_trans (ctx->permute, ctx->state.data, gnn_blue_handle, ctx);
    cnt->trans += count;
    cnt->explored++;
    ndfs_maybe_report(ctx->search, cnt);
}

/* RED DFS (of CYAN) */
static void
gnn_red (wctx_t *ctx)
{
    size_t              start_level = dfs_stack_nframes (ctx->stack);
    ctx->search = NRED; ctx->permute->permutation = permutation_red;
    ctx->red.visited++; //count accepting states

    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.idx);
            if ( nn_color_eq(color, NNRED)  ) {
                if (start_level == dfs_stack_nframes (ctx->stack)) {
                     break;  ctx->red.touched_red++; }
                dfs_stack_pop (ctx->stack);
            } else
                gnn_explore_state_red (ctx, &ctx->red);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (start_level == dfs_stack_nframes (ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }

    ctx->search = NBLUE; ctx->permute->permutation = permutation;
}

/* BLUE DFS (of CYAN) */
void
gnn_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.idx);
            if ( nn_color_eq(color, NNWHITE) &&
                 !global_has_color(ctx->state.idx, GGREEN) ) {
                nn_set_color (&ctx->color_map, ctx->state.idx, NNCYAN);
                gnn_explore_state_blue (ctx, &ctx->counters);
            } else
                dfs_stack_pop (ctx->stack);
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;

            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( global_try_color(ctx->state.idx, GGREEN) && //mark green
                 GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                gnn_red (ctx);
                nn_set_color(&ctx->color_map, ctx->state.idx, NNRED);
            } else
                nn_set_color(&ctx->color_map, ctx->state.idx, NNBLUE);

            dfs_stack_pop (ctx->stack);
        }
    }
/*a*/(void) work;
}

/*
 * Reachability algorithms
 */

static void
handle_state (void *arg, transition_info_t *ti, state_data_t dst)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    state_info_create (&successor, dst, NULL, DUMMY_IDX, ti->group);
    if (!find_or_put (&successor, &ctx->state, ctx->store2)) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (&successor, stack_loc);
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
explore_state (wctx_t *ctx, raw_data_t state, int next_index)
{
    if (0 == next_index && ctx->counters.level_cur >= max)
        return K;
    int                 count = 0;
    int                 i = K;
    state_info_deserialize (&ctx->state, state, ctx->store);
    if ( UseBlackBox == call_mode )
        count = permute_trans(ctx->permute, ctx->state.data, handle_state, ctx);
    else // UseGreyBox
        for (i = next_index; i<K && count<MAX_SUCC; i++)
            count += GBgetTransitionsLong(ctx->model, i, ctx->state.data, handle_state, ctx);
    if ( dlk_detect && (0==count && 0==next_index) )
        handle_deadlock(ctx);
    maybe_report (&ctx->counters, "", &threshold);
    return i;
}

void
dfs_grey (wctx_t *ctx, size_t work)
{
    int                 next_index = 0;
    size_t              max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < max_load) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL == state_data) {
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            next_index = isba_pop_int (ctx->group_stack)[0];
            continue;
        }
        if (next_index == K) {
            ctx->counters.explored++;
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
dfs (wctx_t *ctx, size_t work)
{
    size_t              max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < max_load) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
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
            ctx->counters.explored++;
        }
    }
}

void
bfs (wctx_t *ctx, size_t work)
{
    size_t              max_load = ctx->counters.explored + work;
    while (ctx->counters.explored < max_load) {
        raw_data_t          state_data = dfs_stack_top (ctx->in_stack);
        if (NULL == state_data) {
            if (0 == dfs_stack_frame_size (ctx->out_stack))
                return;
            dfs_stack_t     old = ctx->out_stack;
            ctx->stack = ctx->out_stack = ctx->in_stack;
            ctx->in_stack = old;
            ctx->counters.level_cur++;
        } else {
            dfs_stack_pop (ctx->in_stack);
            ctx->load--;
            explore_state (ctx, state_data, 0);
            ctx->counters.explored++;
        }
    }
}

size_t
split_bfs (size_t source_id, size_t target_id, size_t handoff)
{
    wctx_t             *source = contexts[source_id];
    wctx_t             *target = contexts[target_id];
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
    wctx_t             *source = contexts[source_id];
    wctx_t             *target = contexts[target_id];
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

/* explore is started for each thread (worker) */
static void *
explore (void *args)
{
    wctx_t             *ctx = (wctx_t *) args;
    mytimer_t           timer = SCCcreateTimer ();
    char                lbl[20];
    snprintf (lbl, sizeof (char[20]), W>1?"%s[%zu]":"%s", program, ctx->id);
    set_label (lbl);    // register print label and load model

    if (NULL == ctx->model)
        ctx->model = get_model (0);
    permute_set_model (ctx->permute, ctx->model);

    transition_info_t start_trans_info = GB_NO_TRANSITION;
    if ( Strat_LTL & strategy )
        ndfs_handle (ctx, &start_trans_info, initial_state);
    else if (0 == ctx->id)
        handle_state (ctx, &start_trans_info, initial_state);
    ctx->counters.trans = 0; //reset trans count

#ifndef __APPLE__
    // lock thread to one core
    cpu_set_t          *set = RTmalloc (sizeof (cpu_set_t));
    CPU_ZERO (set);
    CPU_SET (ctx->id, set);
    sched_setaffinity (0, sizeof (cpu_set_t), set);
    //synchronize exploration
    pthread_barrier_wait(&start_barrier);
#endif

    /* The load balancer starts the right algorithm, see init_globals */
    lb_local_init(lb, ctx->id, ctx, &ctx->load);
    SCCstartTimer (timer);
    lb_balance( lb, ctx->id, ctx, &ctx->load );
    SCCstopTimer (timer);
    ctx->counters.runtime = SCCrealTime (timer);
    SCCdeleteTimer (timer);
    return statistics (dbs);
}

int
main (int argc, char *argv[])
{
    /* Init structures */
    init_globals (argc, argv);

    /* Start workers */
    mytimer_t           timer = SCCcreateTimer ();
    SCCstartTimer (timer);
    for (size_t i = 0; i < W; i++)
        pthread_create (&contexts[i]->me, attr, explore, contexts[i]);
    for (size_t i = 0; i < W; i++)
        pthread_join (contexts[i]->me, (void **)&contexts[i]->counters.stats);
    SCCstopTimer (timer);

    /* Gather results */
    counter_t          *reach = RTmallocZero (sizeof(counter_t));
    counter_t          *red = RTmallocZero (sizeof(counter_t));
    reach->stats = RTmallocZero (sizeof(stats_t));
    for (size_t i = 0; i < W; i++) {
        wctx_t             *ctx = contexts[i];
        ctx->counters.stack_sizes = dfs_stack_size_max (ctx->in_stack);
        add_results (reach, &ctx->counters);
        add_results (red, &ctx->red);
        print_thread_statistics (ctx);
        RTfree (ctx->counters.stats);
    }
    if (RTverbosity >= 1)
        print_statistics (reach, red, timer);
    SCCdeleteTimer (timer); RTfree (reach->stats); RTfree (reach); RTfree (red);
    deinit_globals ();
    return EXIT_SUCCESS;
}
