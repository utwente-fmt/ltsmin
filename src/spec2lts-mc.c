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
#include <math.h>

#include <archive.h>
#include <cctables.h>
#include <dbs-ll.h>
#include <dbs.h>
#include <dfs-stack.h>
#include <dm/bitvector.h>
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
#include <unix.h>
#include <vector_set.h>
#include <zobrist.h>

static const int    THREAD_STACK_SIZE = 400 * 4096; //pthread_attr_setstacksize

static inline size_t min (size_t a, size_t b) {
    return a < b ? a : b;
}

typedef int                *state_data_t;
static const state_data_t   state_data_dummy;
static const size_t         SLOT_SIZE = sizeof(*state_data_dummy);
typedef int        *raw_data_t;
typedef struct state_info_s {
    state_data_t        data;
    tree_t              tree;
    ref_t               ref;
    uint32_t            hash32;
} state_info_t;

typedef enum { UseGreyBox, UseBlackBox } box_t;
typedef enum { UseDBSLL, UseTreeDBSLL } db_type_t;
typedef enum {
    Strat_BFS    = 1,
    Strat_DFS    = 2,
    Strat_NDFS   = 4,
    Strat_NNDFS  = 8,
    Strat_MCNDFS = 16,
    Strat_LTLG   = Strat_MCNDFS,
    Strat_LTL    = Strat_NDFS | Strat_NNDFS | Strat_MCNDFS,
    Strat_Reach  = Strat_BFS | Strat_DFS
} strategy_t;

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
    Perm_Dynamic,   /* generate a dynamic permutation based on color feedback */
    Perm_Unknown    /* not set yet */
} permutation_perm_t;

typedef struct permute_todo_s {
    state_info_t        si;
    transition_info_t   ti;
    int                 seen;
} permute_todo_t;

typedef void            (*perm_cb_f)(void *context, state_info_t *dst,
                                     transition_info_t *ti, int seen);

typedef struct permute_s {
    void               *ctx;    /* GB context */
    int               **rand;   /* random permutations */
    int                *pad;    /* scratch pad for otf and dynamic permutation */
    perm_cb_f           real_cb;            /* GB callback */
    state_info_t       *state;              /* the source state */
    double              shift;              /* distance in group-based shift */
    uint32_t            shiftorder;         /* shift projected to ref range*/
    int                 start_group;        /* fixed index of group-based shift*/
    int                 start_group_index;  /* recorded index higher than start*/
    permute_todo_t     *todos;  /* records states that require late permutation */
    int                *tosort; /* indices of todos */
    size_t              nstored;/* number of states stored in to-do */
    size_t              trans;  /* number of transition groups */
    permutation_perm_t  permutation;        /* kind of permuation */
    model_t             model;  /* GB model */
} permute_t;

/**
 * Create a permuter.
 * arguments:
 * permutation: see permutation_perm_t
 * shift: distance between shifts
 */
extern permute_t       *permute_create (permutation_perm_t permutation, model_t model,
                                        size_t workers, size_t trans, int worker_index);
extern void             permute_free (permute_t *perm);
extern int              permute_trans (permute_t *perm, state_info_t *state,
                                       perm_cb_f cb, void *ctx);

static char            *program;
static cct_map_t       *tables = NULL;
static char            *files[2];
static int              dbs_size = 0;
static int              refs = 0;
static int              no_red_perm = 0;
static int              all_red = 1;
static box_t            call_mode = UseBlackBox;
static size_t           max = UINT_MAX;
static size_t           W = 2;
static lb_t            *lb;
static void            *dbs;
static dbs_stats_f      statistics;
static dbs_get_f        get;
static dbs_get_sat_f    get_sat_bit;
static dbs_try_set_sat_f try_set_sat_bit;
static dbs_inc_sat_bits_f   inc_sat_bits;
static dbs_dec_sat_bits_f   dec_sat_bits;
static dbs_get_sat_bits_f   get_sat_bits;
static char            *state_repr = "tree";
static db_type_t        db_type = UseTreeDBSLL;
static char            *arg_strategy = "bfs";
static strategy_t       strategy = Strat_BFS;
static char            *arg_lb = "combined";
static lb_method_t      lb_method = LB_Combined;
static char            *arg_perm = "unknown";
static permutation_perm_t permutation = Perm_Unknown;
static permutation_perm_t permutation_red = Perm_Unknown;
static char*            trc_output = NULL;
static int              dlk_detect = 0;
static size_t           G = 100;
static size_t           H = MAX_HANDOFF_DEFAULT;
static int              ZOBRIST = 0;
static ref_t           *parent_ref = NULL;
static state_data_t     state_data;
static state_info_t     initial_state;
static int              sat_bits = 0;
static int              red_sat_bit;

static si_map_entry strategies[] = {
    {"bfs",     Strat_BFS},
    {"dfs",     Strat_DFS},
    {"ndfs",    Strat_NDFS},
    {"nndfs",   Strat_NNDFS},
    {"mcndfs",  Strat_MCNDFS},
    {"mcnndfs",  Strat_MCNDFS},
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
    {"dynamic", Perm_Dynamic},
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
     &arg_strategy, 0, "select the search strategy", "<bfs|dfs|ndfs|nndfs|mcndfs>"},
    {"lb", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_lb, 0, "select the load balancing method", "<srp|static|combined|none>"},
    {"perm", 'p', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_perm, 0, "select the transition permutation method",
     "<dynamic,sort,sr,shift|shiftall|otf|none>"},
    {"no-red-perm", 0, POPT_ARG_VAL, &no_red_perm, 1, "turn off transition permutation for the red search", NULL},
    {"nar", 1, POPT_ARG_VAL, &all_red, 0, "turn off red coloring in the blue search (NNDFS/MCNNDFS)", NULL},
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
 * Pink:  1 0) A state that has been considered in both the blue and the red search
 * Cyan:  1 1) A state whose blue search has not been terminated
 *
 * In MC-NDFS, Pink states are the ones on the stack of dfs_red
 *
 */
typedef struct {
  int nn;
} nndfs_color_t;

enum { WHITE=0, BLUE=1, PINK=2, CYAN=3 };
#define NNCOLOR(c) (nndfs_color_t){ .nn = (c) }
#define NNWHITE    NNCOLOR(WHITE) // value: 00
#define NNBLUE     NNCOLOR(BLUE)  // value: 01
#define NNPINK     NNCOLOR(PINK)  // value: 10
#define NNCYAN     NNCOLOR(CYAN)  // value: 11

static inline nndfs_color_t
nn_get_color (bitvector_t *set, ref_t ref)
{ return (nndfs_color_t){ .nn = bitvector_get2 (set, ref<<1) };  }

static inline int
nn_set_color (bitvector_t *set, ref_t ref, nndfs_color_t color)
{ return bitvector_isset_or_set2 (set, ref<<1, color.nn); }

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
ndfs_has_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{ return bitvector_is_set (set, (ref<<1)|color.n); }

static inline int
ndfs_try_color (bitvector_t *set, ref_t ref, ndfs_color_t color)
{ return bitvector_isset_or_set (set, (ref<<1)|color.n); }

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

enum { GREEN=0, RED=1 };
#define GCOLOR(c)  (global_color_t){ .g = (c) }
#define GGREEN     GCOLOR(GREEN)    // bit 0
#define GRED       GCOLOR(RED)      // bit 1

//TODO: REMOVE GREEN

static inline int
global_has_color (ref_t ref, global_color_t color)
{
    assert (color.g == RED);
    return get_sat_bit (dbs, ref, red_sat_bit);
}

static inline int //RED and BLUE are independent
global_try_color (ref_t ref, global_color_t color)
{
    assert (color.g == RED);
    return try_set_sat_bit (dbs, ref, red_sat_bit);
}

static inline uint32_t
inc_wip (ref_t ref)
{
    uint32_t res = inc_sat_bits (dbs, ref);
    return res;
}

static inline uint32_t
dec_wip (ref_t ref)
{
    return dec_sat_bits (dbs, ref);
}

static inline uint32_t
get_wip (ref_t ref)
{
    return get_sat_bits (dbs, ref) & ((1L << (red_sat_bit)) - 1);
}

static inline int
g_color_eq (const global_color_t a, const global_color_t b)
{ return a.g == b.g; };

typedef struct counter_s {
    double              runtime;        // measured exploration time
    size_t              visited;        // counter: visited states
    size_t              explored;       // counter: explored states
    size_t              allred;         // counter: allred states
    size_t              trans;          // counter: transitions
    size_t              level_max;      // counter: (BFS) level / (DFS) max level
    size_t              load_max;       // max stack/queue load
    size_t              level_cur;      // counter: current (DFS) level
    stats_t            *stats;          // running state storage statistics
    size_t              threshold;      // report threshold
    size_t              waits;          // number of waits for WIP
    size_t              bogus_red;      // number of bogus red colorings
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
    bitvector_t         color_map;      // Local NDFS coloring of states (ref-based)
    isb_allocator_t     group_stack;    // last explored group per frame (grey)
    counter_t           counters;       // reachability/NDFS_blue counters
    counter_t           red;            // NDFS_red counters
    size_t              load;           // queue load (for balancing)
    ref_t               seed;           // current NDFS seed
    permute_t          *permute;        // transition permutor
    bitvector_t         all_red;    // all_red gaiser/Schwoon
} wctx_t;

/* predecessor --(transition_info)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     transition_info_t *ti,
                                     state_info_t *predecessor,
                                     state_data_t store);

static const ref_t DUMMY_IDX = UINT_MAX;

extern size_t state_info_size ();
extern size_t state_info_int_size ();
extern void state_info_create_empty (state_info_t *state);
extern void state_info_create (state_info_t *state, state_data_t data,
                               tree_t tree, ref_t ref);
extern void state_info_serialize (state_info_t *state, raw_data_t data);
extern void state_info_deserialize (state_info_t *state, raw_data_t data,
                                    raw_data_t store);
extern int state_info_initialize (state_info_t *state, state_data_t data,
                                  transition_info_t *ti, state_info_t *src,
                                  wctx_t *ctx);
extern void         ndfs_blue (wctx_t *ctx, size_t work);
extern void         nndfs_blue (wctx_t *ctx, size_t work);
extern void         mcndfs_blue (wctx_t *ctx, size_t work);
extern void         dfs_grey (wctx_t *ctx, size_t work);
extern void         dfs (wctx_t *ctx, size_t work);
extern void         bfs (wctx_t *ctx, size_t work);
extern size_t       split_bfs (void *arg_src, void *arg_tgt, size_t handoff);
extern size_t       split_dfs (void *arg_src, void *arg_tgt, size_t handoff);

static find_or_put_f find_or_put;
static int          N;
static int          K;
static int          MAX_SUCC;           // max succ. count to expand at once
static size_t       threshold;
static pthread_attr_t  *attr = NULL;
static wctx_t     **contexts;
static zobrist_t    zobrist = NULL;

void
add_results (counter_t *res, counter_t *cnt)
{
    res->runtime += cnt->runtime;
    res->visited += cnt->visited;
    res->explored += cnt->explored;
    res->allred += cnt->allred;
    res->trans += cnt->trans;
    res->level_max += cnt->level_max;
    res->load_max += cnt->load_max;
    res->waits += cnt->waits;
    res->bogus_red += cnt->bogus_red;
    if (NULL != res->stats && NULL != cnt->stats)
        add_stats(res->stats, cnt->stats);
}

static inline void
wait_seed (wctx_t *ctx, ref_t seed)
{
    int didwait = 0;
    while (get_wip(seed) > 0 && !lb_is_stopped(lb)) { didwait = 1;} //wait
    if (didwait) {
        ctx->red.waits++;
    }
}

static inline void
increase_level (wctx_t *ctx, counter_t *cnt)
{
    cnt->level_cur++;
    if (cnt->level_cur > cnt->level_max)
        cnt->level_max = cnt->level_cur;
    if (ctx->load > cnt->load_max)
        cnt->load_max = ctx->load;
}

static inline void
set_all_red (wctx_t *ctx, state_info_t *state)
{
    if (global_try_color (state->ref, GRED)) {
        ctx->counters.allred++;
        if ( GBbuchiIsAccepting(ctx->model, state->data) )
            ctx->red.visited++; /* count accepting states */
    } else {
        ctx->red.allred++;
    }
}

static inline void
set_red (wctx_t *ctx, state_info_t *state)
{
    if (global_try_color (state->ref, GRED)) {
        ctx->red.explored++;
        if ( GBbuchiIsAccepting(ctx->model, get(dbs, state->ref, ctx->store2)) )
            ctx->red.visited++; /* count accepting states */
    } else {
        ctx->red.bogus_red++;
    }
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

/* Magic number for the largest stack i've encountered.
 * Allocated as bits in a bitvector, but addressed in a stack-wise fashion
 */
static const size_t MAX_STACK = 100000000;

wctx_t *
wctx_create (size_t id)
{
    assert (NULL == 0);
    wctx_t             *ctx = RTalignZero (CACHE_LINE_SIZE, sizeof (wctx_t));
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
        bitvector_create_large (&ctx->color_map, 2UL<<dbs_size);
        bitvector_clear (&ctx->color_map);
        bitvector_create_large (&ctx->all_red, MAX_STACK);
    } else if ( UseGreyBox == call_mode && Strat_DFS == strategy)
        ctx->group_stack = isba_create (1);
    ctx->counters.threshold = ctx->red.threshold = threshold;
    ctx->permute = permute_create (permutation, NULL, W, K, id);
    return ctx;
}

void
wctx_free (wctx_t *ctx)
{
    RTfree (ctx->store);
    RTfree (ctx->store2);
    dfs_stack_destroy (ctx->out_stack);
    if (strategy & Strat_LTL) {
        bitvector_free (&ctx->color_map);
        bitvector_free (&ctx->all_red);
    }
    if (NULL != ctx->group_stack)
        isba_destroy (ctx->group_stack);
    if (strategy == Strat_BFS)
        dfs_stack_destroy (ctx->in_stack);
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
find_or_put_zobrist (state_info_t *state, transition_info_t *ti,
                     state_info_t *pred, state_data_t store)
{
    state->hash32 = zobrist_hash_dm (zobrist, state->data, pred->data,
                                     pred->hash32, ti->group);
    return DBSLLlookup_hash (dbs, state->data, &state->ref, &state->hash32);
    (void) store;
}

static int
find_or_put_dbs (state_info_t *state, transition_info_t *ti,
                 state_info_t *predecessor, state_data_t store)
{
    return DBSLLlookup_hash (dbs, state->data, &state->ref, NULL);
    (void) predecessor; (void) store; (void) ti;
}

static int
find_or_put_tree (state_info_t *state, transition_info_t *ti,
                  state_info_t *pred, state_data_t store)
{
    int                 ret;
    ret = TreeDBSLLlookup_dm (dbs, state->data, pred->tree, store, ti->group);
    state->tree = store;
    state->ref = TreeDBSLLindex (state->tree);
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
    RTinitPopt (&argc, &argv, options, 1, 1, files, NULL, "<model>",
                "Perform a parallel reachability analysis of <model>\n\nOptions");
    model_t             model = get_model (1);
    if (Perm_Unknown == permutation) //default permutation depends on strategy
        permutation = strategy & Strat_Reach ? Perm_None : Perm_Dynamic;
    if (Perm_None != permutation) {
         if (call_mode == UseGreyBox)
            Fatal (1, error, "Greybox not supported with state permutation.");
        refs = 1; //The permuter works with references only!
    }
    if (strategy & Strat_LTL) {
        if (call_mode == UseGreyBox)
            Warning(info, "Greybox not supported with strategy NDFS, ignored.");
        lb_method = LB_None;
        threshold = 100000;
        permutation_red = no_red_perm ? Perm_None : permutation;
    } else {
        threshold = 100000 / W;
    }
    Warning (info, "Using %d cores (lb: %s)", W, key_search(lb_methods, lb_method));
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
    Warning (info, "State length is %d, there are %d groups", N, K);

    if (0 == dbs_size) {
        size_t              el_size = db_type == UseTreeDBSLL ? 3 : N;
        size_t              db_el_size = (RTmemSize() / 3) / (el_size * SLOT_SIZE);
        dbs_size = (int) (log(db_el_size) / log(2));
        dbs_size = dbs_size > DB_SIZE_MAX ? DB_SIZE_MAX : dbs_size;
    }
    Warning (info, "Using a %s with 2^%d elements", db_type==UseDBSLL?"hash table":"tree", dbs_size);
    MAX_SUCC = ( Strat_DFS == strategy ? 1 : INT_MAX );  /* for --grey: */
    if (trc_output && !(strategy & Strat_LTL))
        parent_ref = RTmalloc (sizeof(ref_t[1L<<dbs_size]));

    int                 log2W = 0;
    while ((1L << ++log2W) < (int)W+1)  {}
    sat_bits = log2W + 1; // wip (numworkers) + 1 (RED)
    red_sat_bit = sat_bits - 1;
    int                 global_bits = Strat_LTLG & strategy ? sat_bits : 0;
    switch (db_type) {
    case UseDBSLL:
        if (ZOBRIST) {
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
        inc_sat_bits = (dbs_inc_sat_bits_f) DBSLLinc_sat_bits;
        dec_sat_bits = (dbs_dec_sat_bits_f) DBSLLdec_sat_bits;
        get_sat_bits = (dbs_get_sat_bits_f) DBSLLget_sat_bits;
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
        inc_sat_bits = (dbs_inc_sat_bits_f) TreeDBSLLinc_sat_bits;
        dec_sat_bits = (dbs_dec_sat_bits_f) TreeDBSLLdec_sat_bits;
        get_sat_bits = (dbs_get_sat_bits_f) TreeDBSLLget_sat_bits;
        break;
    }
    contexts = RTmalloc (sizeof (wctx_t *[W]));
    for (size_t i = 0; i < W; i++)
        contexts[i] = wctx_create (i);
    contexts[0]->model = model;

    state_data = RTmalloc (SLOT_SIZE * N);
    GBgetInitialState (model, state_data);

    /* Load balancer assigned last, see exit_ltsmin */
    switch (strategy) {
    case Strat_MCNDFS:
        lb = lb_create_max (W, (algo_f)mcndfs_blue,NULL,G, lb_method, H); break;
    case Strat_NNDFS:
        lb = lb_create_max (W, (algo_f)nndfs_blue, NULL,G, lb_method, H); break;
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
    RTfree (state_data);
    lb_destroy (lb);
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
        Warning (info, "%s%zu levels ~%zu states ~%zu transitions", msg,
                 cnt->level_max, W * cnt->explored,  W * cnt->trans);
}

static inline void
ndfs_maybe_report (char *prefix, counter_t *cnt)
{
    maybe_report (cnt, prefix, &cnt->threshold);
}

void
print_statistics (counter_t *reach, counter_t *red, mytimer_t timer)
{
    char               *name;
    double              mem1, mem2, mem3=0, mem4, compr, ratio;
    float               tot = SCCrealTime (timer);
    size_t              db_elts = reach->stats->elts;
    size_t              db_nodes = reach->stats->nodes;
    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    size_t              el_size = db_type == UseTreeDBSLL ? 3 : N;
    size_t              s = state_info_size();
    mem1 = ((double)(s * (reach->load_max+red->load_max))) / (1 << 20);

    reach->level_max /= W; // not so meaningful for DFS
    if (Strat_LTL & strategy) {
        reach->explored /= W;
        reach->trans /= W;
        red->trans /= W;
        red->level_max /= W;
        if ( 0 == (Strat_LTLG & strategy) ) {
            red->visited /= W;
            red->explored /= W;
            reach->allred /= W;
        }
        SCCreportTimer (timer, "Total exploration time");
        mem3 = ((double)(((2UL<<dbs_size))/8*W)) / (1UL<<20);

        Warning (info, "");
        Warning (info, "%s(%s/%s) stats:", key_search(strategies, strategy),
                 key_search(permutations, permutation), key_search(permutations, permutation_red));
        Warning (info, "State space has %zu states, %zu are accepting", db_elts,
                 red->visited);
        Warning (info, "blue states: %zu (%.2f%%), transitions: %zu (per worker)",
                 reach->explored, ((double)reach->explored/db_elts)*100, reach->trans);
        Warning (info, "red states: %zu (%.2f%%), bogus: %zu  (%.2f%%), transitions: %zu, waits: %zu",
                 red->explored, ((double)red->explored/db_elts)*100, red->bogus_red,
                 ((double)red->bogus_red/db_elts), red->trans, red->waits);
        Warning (info, "all-red states: %zu (%.2f%%), bogus %zu (%.2f%%)",
                 reach->allred, ((double)reach->allred/db_elts)*100,
                 red->allred, ((double)red->allred/db_elts)*100);
        Warning (info, "");
        Warning (info, "Total memory used for local state coloring: %.1fMB", mem3);
    } else {
        size_t              dev, state_dev = 0, trans_dev = 0;
        for (size_t i = 0; i< W; i++) {
            dev = (contexts[i]->counters.explored - (reach->explored/W));
            state_dev += dev * dev;
            dev = (contexts[i]->counters.trans - (reach->trans/W));
            trans_dev += dev * dev;
        }
        if (W > 1)
            Warning (info, "mean standard work distribution: %.1f%% (states) %.1f%% (transitions)",
                     100*(sqrt(((double)state_dev / W)) / (double)(reach->explored/(W)) ),
                     100*(sqrt(((double)trans_dev / W))) / (double)(reach->trans/(W)) );
        Warning (info, "")
        print_state_space_total ("State space has ", reach);
        SCCreportTimer (timer, "Total exploration time");
        Warning(info, "")
    }

    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.2fMB",
             s, reach->load_max, mem1);
    mem2 = ((double)(1UL << (dbs_size)) / (1<<20)) * SLOT_SIZE * el_size;
    mem4 = ((double)(db_nodes * SLOT_SIZE * el_size)) / (1<<20);
    compr = (double)(db_nodes * el_size) / (N * db_elts) * 100;
    ratio = (double)((db_nodes * 100) / (1UL << dbs_size));
    name = db_type == UseTreeDBSLL ? "Tree" : "Table";
    Warning (info, "DB: %s, memory: %.1fMB, compr. ratio: %.1f%%, "
             "fill ratio: %.1f%%", name, mem2, compr, ratio);
    Warning (info, "Est. total memory use: %.1fMB (~%.1fMB paged-in)",
             mem1 + mem4 + mem3, mem1 + mem2 + mem3);
    if (RTverbosity >= 2) {        // detailed output for scripts
        Warning (info, "time:{{{%.2f}}}, elts:{{{%zu}}}, nodes:{{{%zu}}}, "
                 "trans:{{{%zu}}}, misses:{{{%zu}}}, tests:{{{%zu}}}, "
                 "rehashes:{{{%zu}}}, memq:{{{%.0f}}}, tt:{{{%.2f}}}, "
                 "explored:{{{%zu}}}, memdb:{{{%.0f}}}, waits:{{{%zu}}}",
                 reach->runtime, db_elts, db_nodes, reach->trans,
                 reach->stats->misses, reach->stats->tests,
                 reach->stats->rehashes, mem1, tot, reach->explored, mem2,
                 red->waits);
    }
}

static void
print_thread_statistics (wctx_t *ctx)
{
    char                name[128];
    char               *format = "[%zu%s] saw in %.3f sec ";
    if (Strat_Reach & strategy) {
        snprintf (name, sizeof name, format, ctx->id, "", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
    } else if (Strat_LTL & strategy) {
        snprintf (name, sizeof name, format, ctx->id, " B", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
        snprintf (name, sizeof name, format, ctx->id, " R", ctx->counters.runtime);
        print_state_space_total (name, &ctx->red);
    }
    if (ctx->load && !lb_is_stopped(lb)) {
        Warning (info, "Wrong load counter %zu", ctx->load);
    }
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

static int
sort_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    const permute_todo_t     *A = &perm->todos[*((int*)a)];
    const permute_todo_t     *B = &perm->todos[*((int*)b)];
    return A->si.ref - B->si.ref + perm->shiftorder;
}

static int
rand_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    int                *rand = *perm->rand;
    const permute_todo_t     *A = &perm->todos[*((int*)a)];
    const permute_todo_t     *B = &perm->todos[*((int*)b)];
    return rand[A->ti.group] - rand[B->ti.group];
}

static int
dyn_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    wctx_t             *ctx = perm->ctx;
    int                *rand = *perm->rand;
    const permute_todo_t     *A = &perm->todos[*((int*)a)];
    const permute_todo_t     *B = &perm->todos[*((int*)b)];
  
    if (!(Strat_LTL & strategy) || A->seen != B->seen) {
        return B->seen - A->seen;
    } else {
        int Awhite = nn_color_eq(nn_get_color(&ctx->color_map, A->si.ref), NNWHITE);
        int Bwhite = nn_color_eq(nn_get_color(&ctx->color_map, B->si.ref), NNWHITE);
        int Aval = ((A->seen) << 1) | Awhite;
        int Bval = ((B->seen) << 1) | Bwhite;
        if (Aval == Bval)
            return rand[A->ti.group] - rand[B->ti.group];
        return Bval - Aval;
    }
}

static inline void
perm_todo (permute_t *perm, state_data_t dst, transition_info_t *ti)
{
    assert (perm->nstored < perm->trans+TODO_MAX);
    permute_todo_t *next = perm->todos + perm->nstored;
    perm->tosort[perm->nstored] = perm->nstored;
    next->seen = state_info_initialize (&next->si, dst, ti, perm->state, perm->ctx);
    next->ti.group = ti->group;
    next->ti.labels = ti->labels;
    perm->nstored++;
}

static inline void
perm_do (permute_t *perm, int i)
{
    permute_todo_t *todo = perm->todos + i;
    perm->real_cb (perm->ctx, &todo->si, &todo->ti, todo->seen);
}

static inline void
perm_do_all (permute_t *perm)
{
    for (size_t i = 0; i < perm->nstored; i++)
        perm_do (perm, perm->tosort[i]);
}

permute_t *
permute_create (permutation_perm_t permutation, model_t model,
                size_t workers, size_t trans, int worker_index)
{
    permute_t          *perm = RTalign (CACHE_LINE_SIZE, sizeof(permute_t));
    perm->todos = RTalign (CACHE_LINE_SIZE, sizeof(permute_todo_t[trans+TODO_MAX]));
    perm->tosort = RTalign (CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
    perm->shift = ((double)trans)/workers;
    perm->shiftorder = (1L<<dbs_size) / workers * worker_index;
    perm->start_group = perm->shift * worker_index;
    perm->trans = trans;
    perm->model = model;
    perm->permutation = permutation;
    if (Perm_Otf == perm->permutation)
        perm->pad = RTalign (CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
    if (Perm_Random == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*[trans+TODO_MAX]));
        for (size_t i = 1; i < perm->trans+TODO_MAX; i++) {
            perm->rand[i] = RTalign(CACHE_LINE_SIZE, sizeof(int[ i ]));
            randperm (perm->rand[i], i, perm->shiftorder);
        }
    }
    if (Perm_SR == perm->permutation || Perm_Dynamic == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign(CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
        randperm (perm->rand[0], trans+TODO_MAX, (time(NULL) + 9876*worker_index));
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
    RTfree (perm->tosort);
    if (Perm_Otf == perm->permutation)
        RTfree (perm->pad);
    if (Perm_Random == perm->permutation) {
        for (size_t i = 0; i < perm->trans+TODO_MAX; i++)
            RTfree (perm->rand[i]);
        RTfree (perm->rand);
    }
    if (Perm_SR == perm->permutation || Perm_Dynamic == perm->permutation) {
        RTfree (perm->rand[0]);
        RTfree (perm->rand);
    }
    RTfree (perm);
}

static void
permute_one (void *arg, transition_info_t *ti, state_data_t dst)
{
    permute_t          *perm = (permute_t*) arg;
    state_info_t        successor;
    int                 seen;
    switch (perm->permutation) {
    case Perm_Shift:
        if (ti->group < perm->start_group) {
            perm_todo (perm, dst, ti);
            break;
        }
    case Perm_None:
        seen = state_info_initialize (&successor, dst, ti, perm->state, perm->ctx);
        perm->real_cb (perm->ctx, &successor, ti, seen);
        break;
    case Perm_Shift_All:
        if (0 == perm->start_group_index && ti->group >= perm->start_group)
            perm->start_group_index = perm->nstored;
    case Perm_Dynamic:
    case Perm_Random:
    case Perm_SR:
    case Perm_Otf:
    case Perm_Sort:
        perm_todo (perm, dst, ti);
        break;
    default:
        Fatal(1, error, "Unknown permutation!");
    }
}

int
permute_trans (permute_t *perm, state_info_t *state, perm_cb_f cb, void *ctx)
{
    perm->ctx = ctx;
    perm->real_cb = cb;
    perm->state = state;
    perm->nstored = perm->start_group_index = 0;
    int count = GBgetTransitionsAll (perm->model, state->data, permute_one, perm);
    switch (perm->permutation) {
    case Perm_Otf:
        randperm (perm->pad, perm->nstored, state->ref + perm->shiftorder);
        for (size_t i = 0; i < perm->nstored; i++)
            perm_do (perm, perm->pad[i]);
        break;
    case Perm_Random:
        for (size_t i = 0; i < perm->nstored; i++)
            perm_do (perm, perm->rand[perm->nstored][i]);
        break;
    case Perm_Dynamic:
        qsortr (perm->tosort, perm->nstored, sizeof(int), dyn_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_SR:
        qsortr (perm->tosort, perm->nstored, sizeof(int), rand_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_Sort:
        qsortr (perm->tosort, perm->nstored, sizeof(int), sort_cmp, perm);
        perm_do_all (perm);
        break;
    case Perm_Shift:
        perm_do_all (perm);
        break;
    case Perm_Shift_All:
        for (size_t i = 0; i < perm->nstored; i++) {
            size_t j = (perm->start_group_index + i);
            j = j < perm->nstored ? j : 0;
            perm_do (perm, j);
        }
        break;
    case Perm_None:
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
state_info_create_empty (state_info_t *state)
{
    state->tree = NULL;
    state->data = NULL;
    state->ref = DUMMY_IDX;
}

void
state_info_create (state_info_t *state, state_data_t data, tree_t tree,
                   ref_t ref)
{
    state->data = data;
    state->tree = tree;
    state->ref = ref;
}

size_t
state_info_int_size ()
{
    return (state_info_size () + 3) / 4;
}

size_t
state_info_size ()
{
    size_t              ref_size = sizeof (ref_t);
    size_t              data_size = SLOT_SIZE * (UseDBSLL==db_type ? N : 2*N);
    size_t              state_info_size = refs ? ref_size : data_size;
    if (!refs && UseDBSLL==db_type)
        state_info_size += ref_size;
    if (ZOBRIST)
        state_info_size += sizeof (uint32_t);
    return state_info_size;
}

/**
 * Next-state function output --> algorithm
 */
int
state_info_initialize (state_info_t *state, state_data_t data,
                       transition_info_t *ti, state_info_t *src, wctx_t *ctx)
{
    state->data = data;
    return find_or_put (state, ti, src, ctx->store2);
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_serialize (state_info_t *state, raw_data_t data)
{
    if (ZOBRIST) {
        ((uint32_t*)data)[0] = state->hash32;
        data++;
    }
    if (refs) {
        ((ref_t*)data)[0] = state->ref;
    } else if ( UseDBSLL==db_type ) {
        ((ref_t*)(data+N))[0] = state->ref;
        memcpy (data, state->data, (SLOT_SIZE * N));
    } else { // UseTreeDBSLL
        memcpy (data, state->tree, (2 * SLOT_SIZE * N));
    }
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_deserialize (state_info_t *state, raw_data_t data, state_data_t store)
{
    if (ZOBRIST) {
        state->hash32 = ((uint32_t*)data)[0];
        data++;
    }
    if (refs) {
        state->ref  = ((ref_t*)data)[0];
        state->data = data = get (dbs, state->ref, store);
        if (UseTreeDBSLL == db_type) {
            state->tree = data;
            state->data = TreeDBSLLdata (dbs, data);
        }
    } else {
        if (UseDBSLL == db_type) {
            state->ref  = ((ref_t*)(data+N))[0];
            state->data = data;
        } else { // UseTreeDBSLL == db_type
            state->tree = data;
            state->data = TreeDBSLLdata (dbs, data);
            state->ref  = TreeDBSLLindex (data);
        }
    }
}

void
state_info_deserialize_cheap (state_info_t *state, raw_data_t data)
{
    if (ZOBRIST) {
        state->hash32 = ((uint32_t*)data)[0];
        data++;
    }
    assert (refs);
    state->ref  = ((ref_t*)data)[0];
}

static void *
get_state (ref_t ref, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    raw_data_t          state = get (dbs, ref, ctx->store2);
    return UseTreeDBSLL==db_type ? TreeDBSLLdata(dbs, state) : state;
}

static void
find_dfs_stack_trace (wctx_t *ctx, dfs_stack_t stack, ref_t *trace, size_t level)
{
    // gather trace
    state_info_t        state;
    assert (level - 1 == dfs_stack_nframes (ctx->stack));
    for (int i = dfs_stack_nframes (ctx->stack)-1; i >= 0; i--) {
        dfs_stack_leave (stack);
        raw_data_t          data = dfs_stack_pop (stack);
        state_info_deserialize (&state, data, ctx->store);
        trace[i] = state.ref;
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
        ref_t              *trace = RTmalloc(sizeof(ref_t) * level);
        /* Write last state to stack to close cycle */
        trace[level-1] = cycle_closing_state->ref;
        find_dfs_stack_trace (ctx, ctx->stack, trace, level);
        trc_env_t          *trace_env = trc_create (ctx->model, get_state,
                                                    trace[0], ctx);
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
        ref_t               start_ref = initial_state.ref;
        trc_env_t  *trace_env = trc_create (ctx->model, get_state, start_ref, ctx);
        trc_find_and_write (trace_env, trc_output, ctx->state.ref, level, parent_ref);
    }
    Warning (info, "Exiting now!");
}

/*
 * NDFS algorithm by Courcoubetis et al.
 */

/* ndfs_handle and ndfs_explore_state can be used by blue and red search */
static void
ndfs_handle_red (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    if ( successor->ref == ctx->seed )
        /* Found cycle back to the seed */
        ndfs_report_cycle (ctx, successor);
    if ( !ndfs_has_color(&ctx->color_map, successor->ref, NRED) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->load++;
    }
    (void) seen; (void) ti;
}

static void
ndfs_handle_blue (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    if ( !ndfs_has_color(&ctx->color_map, successor->ref, NBLUE) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->load++;
    }
    (void) seen; (void) ti;
}

static inline void
ndfs_explore_state_red (wctx_t *ctx)
{
    counter_t *cnt = &ctx->red;
    dfs_stack_enter (ctx->stack);
    increase_level (ctx, cnt);
    ctx->permute->permutation = permutation_red;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_handle_red, ctx);
    cnt->explored++;
    ndfs_maybe_report ("[R] ", cnt);
}

static inline void
ndfs_explore_state_blue (wctx_t *ctx)
{
    counter_t *cnt = &ctx->counters;
    dfs_stack_enter (ctx->stack);
    increase_level (ctx, cnt);
    ctx->permute->permutation = permutation;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_handle_blue, ctx);
    cnt->explored++;
    ndfs_maybe_report ("[B] ", cnt);
}

/* NDFS dfs_red */
static void
ndfs_red (wctx_t *ctx, ref_t seed)
{
    ctx->seed = seed;
    ctx->red.visited++; //count accepting states
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_try_color(&ctx->color_map, ctx->state.ref, NRED) ) {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            } else
                ndfs_explore_state_red (ctx);
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed == ctx->state.ref)
                break;
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
}

/* NDFS dfs_blue */
void
ndfs_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( ndfs_try_color(&ctx->color_map, ctx->state.ref, NBLUE) ) {
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            } else
                ndfs_explore_state_blue (ctx);
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                ndfs_red (ctx, ctx->state.ref);
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
    (void) work;
}

/*
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

static void
nndfs_red_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    nndfs_color_t color = nn_get_color(&ctx->color_map, successor->ref);
    if ( nn_color_eq(color, NNCYAN) ) {
        /* Found cycle back to the stack */
        ndfs_report_cycle(ctx, successor);
    } else if ( nn_color_eq(color, NNBLUE) && (strategy != Strat_MCNDFS ||
            !global_has_color(ctx->state.ref, GRED)) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->load++;
    }
    (void) seen; (void) ti;
}

static void
nndfs_blue_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                   int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    nndfs_color_t color = nn_get_color (&ctx->color_map, successor->ref);
    if ( nn_color_eq(color, NNCYAN) &&
            (GBbuchiIsAccepting(ctx->model, ctx->state.data) ||
             GBbuchiIsAccepting(ctx->model, get(dbs, successor->ref, ctx->store2))) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, successor);
    } else if ((strategy == Strat_MCNDFS && !global_has_color(ctx->state.ref, GRED)) ||
               (strategy != Strat_MCNDFS && !nn_color_eq(color, NNPINK))) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->load++;
    }
    (void) seen; (void) ti;
}

static inline void
nndfs_explore_state_red (wctx_t *ctx)
{
    counter_t *cnt = &ctx->red;
    dfs_stack_enter (ctx->stack);
    increase_level (ctx, cnt);
    ctx->permute->permutation = permutation_red;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, nndfs_red_handle, ctx);
    ndfs_maybe_report ("[R] ", cnt);
}

static inline void
nndfs_explore_state_blue (wctx_t *ctx)
{
    counter_t *cnt = &ctx->counters;
    dfs_stack_enter (ctx->stack);
    increase_level (ctx, cnt);
    ctx->permute->permutation = permutation;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, nndfs_blue_handle, ctx);
    cnt->explored++;
    ndfs_maybe_report ("[B] ", cnt);
}

/* NNDFS dfs_red */
static void
nndfs_red (wctx_t *ctx, ref_t seed)
{
    ctx->red.visited++; //count accepting states
    nndfs_explore_state_red (ctx);
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNBLUE) ) {
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                nndfs_explore_state_red (ctx);
                ctx->red.explored++;
            } else {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            }
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed == ctx->state.ref)
                break;
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
}

/* NNDFS dfs_blue */
void
nndfs_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) ) {
                bitvector_set ( &ctx->all_red, ctx->counters.level_cur );
                nn_set_color (&ctx->color_map, ctx->state.ref, NNCYAN);
                nndfs_explore_state_blue (ctx);
            } else {
                if ( !nn_color_eq(color, NNPINK) )
                    bitvector_unset ( &ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* exit if backtrack hits seed, leave stack the way it was */
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                ctx->counters.allred++;
                if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                    ctx->red.visited++;
            } else if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                /* call red DFS for accepting states */
                nndfs_red (ctx, ctx->state.ref);
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
            } else {
                if (ctx->counters.level_cur > 0)
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                nn_set_color (&ctx->color_map, ctx->state.ref, NNBLUE);
            }
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
    (void) work;
}

/*
 * MC-NDFS by Laarman/Langerak/vdPol/Weber/Wijs
 */

/* MC-NDFS dfs_red */
static void
mcndfs_red (wctx_t *ctx, ref_t seed)
{
    inc_wip (seed);
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( !nn_color_eq(color, NNPINK) &&
                 !global_has_color(ctx->state.ref, GRED) ) {
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                nndfs_explore_state_red (ctx);
            } else {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            }
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            if (seed == ctx->state.ref) {
                /* exit if backtrack hits seed, leave stack the way it was */
                dec_wip (seed);
                wait_seed (ctx, seed);
                if ( global_try_color(ctx->state.ref, GRED) )
                    ctx->red.visited++; //count accepting states
                return;
            }
            set_red (ctx, &ctx->state);
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
    //halted by the load balancer
    dec_wip (seed);
}

/* MCNDFS dfs_blue */
void
mcndfs_blue (wctx_t *ctx, size_t work)
{
    while ( !lb_is_stopped(lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) &&
                 !global_has_color(ctx->state.ref, GRED) ) {
                bitvector_set (&ctx->all_red, ctx->counters.level_cur);
                nn_set_color (&ctx->color_map, ctx->state.ref, NNCYAN);
                nndfs_explore_state_blue (ctx);
            } else {
                if ( !global_has_color(ctx->state.ref, GRED) )
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
                ctx->load--;
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* all successors are red */
                set_all_red (ctx, &ctx->state);
            } else if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                /* call red DFS for accepting states */
                mcndfs_red (ctx, ctx->state.ref);
            } else if (ctx->counters.level_cur > 0 &&
                       !global_has_color(ctx->state.ref, GRED)) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
            }
            nn_set_color (&ctx->color_map, ctx->state.ref, NNBLUE);
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        }
    }
    (void) work;
}

/*
 * Reachability algorithms
 */

static void
reach_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    if (!seen) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        if (trc_output)
            parent_ref[successor->ref] = ctx->state.ref;
        ctx->load++;
        ctx->counters.visited++;
    }
    ctx->counters.trans++;
    (void) ti;
}

static void
reach_handle_wrap (void *arg, transition_info_t *ti, state_data_t data)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_info_t        successor;
    int                 seen;
    seen = state_info_initialize (&successor, data, ti, &ctx->state, ctx);
    reach_handle (arg, &successor, ti, seen);
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
        count = permute_trans (ctx->permute, &ctx->state, reach_handle, ctx);
    else // UseGreyBox
        for (i = next_index; i<K && count<MAX_SUCC; i++)
            count += GBgetTransitionsLong (ctx->model, i, ctx->state.data,
                                           reach_handle_wrap, ctx);
    if ( dlk_detect && (0==count && 0==next_index) )
        handle_deadlock (ctx);
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
            increase_level (ctx, &ctx->counters);
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
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            dfs_stack_pop (ctx->stack);
            ctx->load--;
        } else {
            dfs_stack_enter (ctx->stack);
            increase_level (ctx, &ctx->counters);
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
            increase_level (ctx, &ctx->counters);
        } else {
            dfs_stack_pop (ctx->in_stack);
            ctx->load--;
            explore_state (ctx, state_data, 0);
            ctx->counters.explored++;
        }
    }
}

size_t
split_bfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    dfs_stack_t         source_stack = source->in_stack;
    size_t              in_size = dfs_stack_size (source_stack);
    if (in_size < 2) {
        in_size = dfs_stack_size (source->out_stack);
        source_stack = source->out_stack;
    }
    handoff = min (in_size >> 1 , handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_peek (source_stack, i);
        dfs_stack_push (target->stack, one);
    }
    dfs_stack_discard (source_stack, handoff);
    return handoff;
}

size_t
split_dfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    size_t              in_size = dfs_stack_size (source->stack);
    handoff = min (in_size >> 1, handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_top (source->stack);
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

    transition_info_t   ti = GB_NO_TRANSITION;
    state_info_initialize (&initial_state, state_data, &ti, &ctx->state, ctx);
    if ( Strat_LTL & strategy )
        ndfs_handle_blue (ctx, &initial_state, &ti, 0);
    else if (0 == ctx->id)
        reach_handle (ctx, &initial_state, &ti, 0);
    ctx->counters.trans = 0; //reset trans count

    /* The load balancer starts the right algorithm, see init_globals */
    lb_local_init (lb, ctx->id, ctx, &ctx->load);
    SCCstartTimer (timer);
    lb_balance ( lb, ctx->id );
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
        add_results (reach, &ctx->counters);
        add_results (red, &ctx->red);
        print_thread_statistics (ctx);
        RTfree (ctx->counters.stats);
    }
    if (RTverbosity >= 1)
        print_statistics (reach, red, timer);
    SCCdeleteTimer (timer); RTfree (reach->stats); RTfree (reach); RTfree (red);
    deinit_globals ();
    exit (EXIT_SUCCESS);
}
