#include <hre/config.h>

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <popt.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <dm/bitvector.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <ltsmin-lib/ltsmin-tl.h>
#include <mc-lib/atomics.h>
#include <mc-lib/color.h>
#include <mc-lib/cctables.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/lmap.h>
#include <mc-lib/lb.h>
#include <mc-lib/statistics.h>
#include <mc-lib/stats.h>
#include <mc-lib/trace.h>
#include <mc-lib/treedbs-ll.h>
#include <mc-lib/zobrist.h>
#include <util-lib/dfs-stack.h>
#include <util-lib/fast_hash.h>
#include <util-lib/fast_set.h>
#include <util-lib/is-balloc.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-impl.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/property-semantics.h>
#include <util-lib/util.h>


#define                     MAX_STRATEGIES 5

/********************************** TYPEDEFS **********************************/

static const size_t         THRESHOLD = 100000 / 100 * SPEC_REL_PERF;
typedef int                *state_data_t;
static const state_data_t   state_data_dummy;
static const size_t         SLOT_SIZE = sizeof(*state_data_dummy);
typedef int        *raw_data_t;
typedef struct state_info_s {
    state_data_t        data;
    tree_t              tree;
    ref_t               ref;
    lattice_t           lattice;
    hash64_t            hash64;
    lm_loc_t            loc;
} state_info_t;

typedef enum { UseGreyBox, UseBlackBox } box_t;

typedef enum {
    HashTable   = 1,
    TreeTable   = 2,
    ClearyTree  = 4,
    Tree        = TreeTable | ClearyTree
} db_type_t;

typedef enum {
    Strat_None   = 0,
    Strat_SBFS   = 1,
    Strat_BFS    = 2,
    Strat_DFS    = 4,
    Strat_PBFS   = 8,
    Strat_NDFS   = 16,
    Strat_LNDFS  = 32,
    Strat_ENDFS  = 64,
    Strat_CNDFS  = 128,
    Strat_OWCTY  = 256,
    Strat_MAP    = 512,
    Strat_ECD    = 1024,
    Strat_DFSFIFO= 2048, // Not exactly LTL, but uses accepting states (for now) and random order
    Strat_TA     = 4096,
    Strat_TA_SBFS= Strat_SBFS | Strat_TA,
    Strat_TA_BFS = Strat_BFS | Strat_TA,
    Strat_TA_DFS = Strat_DFS | Strat_TA,
    Strat_TA_PBFS= Strat_PBFS | Strat_TA,
    Strat_TA_CNDFS= Strat_CNDFS | Strat_TA,
    Strat_2Stacks= Strat_BFS | Strat_SBFS | Strat_CNDFS | Strat_ENDFS | Strat_DFSFIFO | Strat_OWCTY,
    Strat_LTLG   = Strat_LNDFS | Strat_ENDFS | Strat_CNDFS,
    Strat_LTL    = Strat_NDFS | Strat_LTLG | Strat_OWCTY | Strat_DFSFIFO,
    Strat_Reach  = Strat_BFS | Strat_SBFS | Strat_DFS | Strat_PBFS
} strategy_t;

/* permute_get_transitions is a replacement for GBgetTransitionsLong
 * TODO: move this to permute.c
 */
#define                     TODO_MAX 200

typedef enum {
    Perm_None,      /* normal group order */
    Perm_Shift,     /* shifted group order (lazy impl., thus cheap) */
    Perm_Shift_All, /* eq. to Perm_Shift, but non-lazy */
    Perm_Sort,      /* order on the state index in the DB */
    Perm_Random,    /* generate a random fixed permutation */
    Perm_RR,        /* more random */
    Perm_SR,        /* sort according to a random fixed permutation */
    Perm_Otf,       /* on-the-fly calculation of a random perm for num_succ */
    Perm_Dynamic,   /* generate a dynamic permutation based on color feedback */
    Perm_Unknown    /* not set yet */
} permutation_perm_t;


typedef enum {
    Proviso_None,
//    LTLP_ClosedSet, //TODO
    Proviso_Stack,
} proviso_t;

typedef enum ta_update_e {
    TA_UPDATE_NONE = 0,
    TA_UPDATE_WAITING = 1,
    TA_UPDATE_PASSED = 2,
} ta_update_e_t;

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

typedef struct thread_ctx_s wctx_t;
/* predecessor --(transition_info)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     transition_info_t *ti,
                                     state_info_t *predecessor,
                                     state_data_t store);


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


/********************************** SETTINGS **********************************/

static find_or_put_f    find_or_put;
static dbs_get_f        get;
static dbs_stats_f      statistics;
static hash64_f         hasher;
static db_type_t        db_type = TreeTable;
static strategy_t       strategy[MAX_STRATEGIES] = {Strat_None, Strat_None, Strat_None, Strat_None, Strat_None};
static permutation_perm_t permutation = Perm_Unknown;
static permutation_perm_t permutation_red = Perm_Unknown;
static const char      *arg_proviso = "none";
static char*            trc_output = NULL;
static int              act_index = -1;
static int              act_type = -1;
static int              act_label = -1;
static size_t           W = -1;
static size_t           G = 1;
static size_t           H = 1000;
static size_t           D;                  // size of state in explicit state DB
static size_t           N;                  // size of entire state
static size_t           K;                  // number of groups
static size_t           SL;                 // number of state labels
static size_t           EL;                 // number of edge labels
static size_t           MAX_SUCC;           // max succ. count to expand at once
static int              count_bits = 0;
static int              global_bits = 0;
static int              local_bits = 0;
static int              indexing;
static size_t           max_level_size = 0;
static const size_t     MAX_STACK = 100000000;  // length of allred bitset


/********************************** OPTIONS **********************************/

static si_map_entry strategies[] = {
    {"none",    Strat_None},
    {"bfs",     Strat_BFS},
    {"dfs",     Strat_DFS},
    {"sbfs",    Strat_SBFS},
    {"pbfs",    Strat_PBFS},
    {"cndfs",   Strat_CNDFS},
#ifndef OPAAL
    {"ndfs",    Strat_NDFS},
    {"lndfs",   Strat_LNDFS},
    {"endfs",   Strat_ENDFS},
    {"owcty",   Strat_OWCTY},
    {"map",     Strat_MAP},
    {"ecd",     Strat_ECD},
    {"dfsfifo", Strat_DFSFIFO},
#endif
    {NULL, 0}
};

static si_map_entry permutations[] = {
    {"shift",   Perm_Shift},
    {"shiftall",Perm_Shift_All},
    {"sort",    Perm_Sort},
    {"otf",     Perm_Otf},
    {"random",  Perm_Random},
    {"rr",      Perm_RR},
    {"sr",      Perm_SR},
    {"none",    Perm_None},
    {"dynamic", Perm_Dynamic},
    {"unknown", Perm_Unknown},
    {NULL, 0}
};

static si_map_entry db_types[] = {
    {"table",   HashTable},
    {"tree",    TreeTable},
    {"cleary-tree", ClearyTree},
    {NULL, 0}
};

static si_map_entry provisos[]={
    {"none",        Proviso_None},
    {"stack",       Proviso_Stack},
    {NULL, 0}
};

static char            *files[2];
static char            *table_size = "20%";
static int              dbs_size = 0;
static int              refs = 1;
static int              ZOBRIST = 0;
static int              no_red_perm = 0;
static int              strict_dfsfifo = 0;
static int              all_red = 1;
static int              owcty_do_reset = 0;
static int              owcty_ecd_all = 0;
static int              ecd = 1;
static box_t            call_mode = UseBlackBox;
static size_t           max_level = SIZE_MAX;
static size_t           ratio = 2;
static proviso_t        proviso = Proviso_None;
static char            *arg_perm = "unknown";
static char            *state_repr = "tree";
static char            *arg_strategy = "none";
static int              dlk_detect = 0;
static char            *act_detect = NULL;
static char            *inv_detect = NULL;
static int              no_exit = 0;
static int              LATTICE_BLOCK_SIZE = (1UL<<CACHE_LINE) / sizeof(lattice_t);
static int              UPDATE = TA_UPDATE_WAITING;
static int              NONBLOCKING = 0;
static int              write_state=0;
static char*            label_filter=NULL;

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
            Abort ("unknown vector storage mode type %s", state_repr);
        db_type = res;
        int i = 0, begin = 0, end = 0;
        char *strat = strdup (arg_strategy);
        char last;
        do {
            if (i > 0 && !((Strat_ENDFS | Strat_OWCTY) & strategy[i-1]))
                Abort ("Only ENDFS supports recursive repair procedures.");
            while (',' != arg_strategy[end] && '\0' != arg_strategy[end]) ++end;
            last = strat[end];
            strat[end] = '\0';
            res = linear_search (strategies, &strat[begin]);
            if (res < 0)
                Abort ("unknown search strategy %s", &strat[begin]);
            strategy[i++] = res;
            end += 1;
            begin = end;
        } while ('\0' != last && i < MAX_STRATEGIES);
        free (strat); // strdup
        if (Strat_OWCTY == strategy[0]) {
            if (ecd && Strat_None == strategy[1]) {
                Warning (info, "Defaulting to MAP as OWCTY early cycle detection procedure.");
                strategy[1] = Strat_MAP;
            }
        }
        if (Strat_ENDFS == strategy[i-1]) {
            if (MAX_STRATEGIES == i)
                Abort ("Open-ended recursion in ENDFS repair strategies.");
            Warning (info, "Defaulting to NDFS as ENDFS repair procedure.");
            strategy[i] = Strat_NDFS;
        }
        res = linear_search (permutations, arg_perm);
        if (res < 0)
            Abort ("unknown permutation method %s", arg_perm);
        permutation = res;

        int p = proviso = linear_search (provisos, arg_proviso);
        if (p < 0) Abort ("unknown proviso %s", arg_proviso);
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to state_db_popt");
    (void)con; (void)opt; (void)arg; (void)data;
}

static struct poptOption options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)state_db_popt, 0, NULL, NULL},
    {"state", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &state_repr, 0,
      "select the data structure for storing states. Beware for Cleary tree: size <= 28 + 2 * ratio.", "<tree|table|cleary-tree>"},
    {"size", 's', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &table_size, 0,
     "log2 size of the state store or maximum % of memory to use", NULL},
     { "write-state", 0, POPT_ARG_VAL, &write_state, 1, "write the full state vector", NULL },
     { "filter" , 0 , POPT_ARG_STRING , &label_filter , 0 ,
       "Select the labels to be written to file from the state vector elements, "
       "state labels and edge labels." , "<patternlist>" },
#ifdef OPAAL
    {"lattice-blocks", 'l', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &LATTICE_BLOCK_SIZE, 0,
      "Size of blocks preallocated for lattices (> 1). "
         "Small blocks save memory when most states few lattices (< 4). "
         "Larger blocks save memory in case a few states have many lattices. "
         "For the best performance set this to: cache line size (usually 64) divided by lattice size of 8 byte.", NULL},
    {"update", 'u', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &UPDATE,
      0,"cover update strategy: 0 = simple, 1 = update waiting, 2 = update passed (may break traces)", NULL},
    {"non-blocking", 'n', POPT_ARG_VAL, &NONBLOCKING, 1, "Non-blocking TA reachability", NULL},
    {"strategy", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_strategy, 0, "select the search strategy", "<sbfs|bfs|dfs|cndfs>"},
#else
    {"strategy", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_strategy, 0, "select the search strategy", "<bfs|sbfs|dfs|cndfs|lndfs|endfs|endfs,lndfs|endfs,endfs,ndfs|ndfs>"},
    {"no-red-perm", 0, POPT_ARG_VAL, &no_red_perm, 1, "turn off transition permutation for the red search", NULL},
    {"grey", 0, POPT_ARG_VAL, &call_mode, UseGreyBox, "make use of GetTransitionsLong calls", NULL},
    {"max", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &max_level, 0, "maximum search depth", "<int>"},
    {"owcty-reset", 0, POPT_ARG_VAL, &owcty_do_reset, 1, "turn on reset in OWCTY algorithm", NULL},
    {"all-ecd", 0, POPT_ARG_VAL, &owcty_ecd_all, 1, "turn on ECD during all iterations (normally only during initialization)", NULL},
    { "proviso", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &arg_proviso , 0 ,
      "select proviso for ltl/por (only single core!)", "<closedset|stack>"},
#endif
    {"nar", 0, POPT_ARG_VAL, &all_red, 0, "turn off red coloring in the blue search (NNDFS/MCNDFS)", NULL},
    {"strict", 0, POPT_ARG_VAL, &strict_dfsfifo, 1, "turn onn struct BFS in DFS_FIFO", NULL},
    {"no-ecd", 0, POPT_ARG_VAL, &ecd, 0, "turn off early cycle detection (NNDFS/MCNDFS)", NULL},
    {"perm", 'p', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
     &arg_perm, 0, "select the transition permutation method",
     "<dynamic|random|rr|sort|sr|shift|shiftall|otf|none>"},
    {"gran", 'g', POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &G, 0,
     "subproblem granularity ( T( work(P,g) )=min( T(P), g ) )", NULL},
    {"handoff", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &H, 0,
     "maximum balancing handoff (handoff=min(max, stack_size/2))", NULL},
    {"zobrist", 'z', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &ZOBRIST, 0,
     "log2 size of zobrist random table (6 or 8 is good enough; 0 is no zobrist)", NULL},
    {"noref", 0, POPT_ARG_VAL, &refs, 0, "store full states on the stack/queue instead of references (faster)", NULL},
    {"ratio", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &ratio, 0, "log2 tree root to leaf ratio", "<int>"},
    {"deadlock", 'd', POPT_ARG_VAL, &dlk_detect, 1, "detect deadlocks", NULL },
    {"action", 'a', POPT_ARG_STRING, &act_detect, 0, "detect error action", NULL },
    {"invariant", 'i', POPT_ARG_STRING, &inv_detect, 0, "detect invariant violations", NULL },
    {"no-exit", 'n', POPT_ARG_VAL, &no_exit, 1, "no exit on error, just count (for error counters use -v)", NULL },
    {"trace", 0, POPT_ARG_STRING, &trc_output, 0, "file to write trace to", "<lts output>" },
    SPEC_POPT_OPTIONS,
    {NULL, 0, POPT_ARG_INCLUDE_TABLE, greybox_options, 0, "Greybox options", NULL},
    POPT_TABLEEND
};

typedef struct owcty_ext_s {
    uint32_t                count : 30;
    uint32_t                bit : 1;
    uint32_t                acc : 1;
} __attribute ((packed)) owcty_pre_t;

typedef struct global_s {
    lb_t               *lb;             // Load balancer
    void               *dbs;            // Hash table/Tree table/Cleary tree
    lm_t               *lmap;           // Lattice map (Strat_TA)
    ref_t              *parent_ref;     // trace reconstruction / OWCTY MAP
    size_t              threshold;      // print threshold
    wctx_t            **contexts;       // Thread contexts
    zobrist_t           zobrist;        // Zobrist hasher
    cct_map_t          *tables;         // concurrent chunk tables
    pthread_mutex_t     mutex;          // global mutex
    owcty_pre_t        *pre;            // OWCTY predecessor count
    int                 exit_status;    // Exit status
} global_t;

static global_t           *global;

typedef struct counter_s {
    double              runtime;        // measured exploration time
    size_t              visited;        // counter: visited states
    size_t              explored;       // counter: explored states
    size_t              allred;         // counter: allred states
    size_t              trans;          // counter: transitions
    size_t              level_max;      // counter: (BFS) level / (DFS) max level
    size_t              level_cur;      // counter: current (DFS) level
    size_t              waits;          // number of waits for WIP
    size_t              bogus_red;      // number of bogus red colorings
    size_t              rec;            // recursive ndfss
    size_t              splits;         // Splits by LB
    size_t              transfer;       // load transfered by LB
    size_t              deadlocks;      // deadlock count
    size_t              violations;     // invariant violation count
    size_t              errors;         // assertion error count
    size_t              exit;           // recursive ndfss
    rt_timer_t          timer;
    double              time;
    size_t              deletes;        // lattice deletes
    size_t              updates;        // lattice updates
    size_t              inserts;        // lattice inserts
    statistics_t        lattice_ratio;  // On-the-fly calc of stdev/mean of #lat
} counter_t;


struct thread_ctx_s {
    strategy_t          strategy;
    size_t              id;             // thread id (0..NUM_THREADS)
    stream_t            out;            // raw file output stream
    model_t             model;          // Greybox model
    state_data_t        store;          // temporary state storage1
    state_data_t        store2;         // temporary state storage2
    state_info_t        state;          // currently explored state
    state_info_t        initial;         // inital state
    state_info_t       *successor;      // current successor state
    dfs_stack_t         stack;          // Successor stack (for BFS and DFS)
    dfs_stack_t         in_stack;       // Input stack (for BFS)
    dfs_stack_t         out_stack;      // Output stack (for BFS)
    bitvector_t         color_map;      // Local NDFS coloring of states (ref-based)
    isb_allocator_t     group_stack;    // last explored group per frame (grey)
    isb_allocator_t    *queues;         // PBFS queues
    counter_t           counters;       // reachability/NDFS_blue counters
    counter_t           red;            // NDFS_red counters
    ref_t               seed;           // current NDFS seed
    permute_t          *permute;        // transition permutor
    bitvector_t         all_red;        // all_red gaiser/Schwoon
    wctx_t             *rec_ctx;        // ctx for Evangelista's ndfs_p
    int                 rec_bits;       // bit depth of recursive ndfs
    ref_t               work;           // ENDFS work for loadbalancer
    int                 done;           // ENDFS done for loadbalancer
    int                 subsumes;       // TA successor subsumes a state in LMAP
    lm_loc_t            added_at;       // TA successor is added at location
    lm_loc_t            last;           // TA last tombstone location
    rt_timer_t          timer;          // Local exploration time timer
    rt_timer_t          timer2;         // OWCTY extra timer
    stats_t            *stats;          // Statistics
    fset_t             *cyan;           // Cyan states for ta_cndfs or OWCTY_ECD
    fset_t             *pink;           // Pink states for ta_cndfs
    fset_t             *cyan2;          // Cyan states for ta_cndfs_sub
    string_index_t      si;             // Trace index
    int                 flip;           // OWCTY invert state space bit
    ssize_t             iteration;      // OWCTY: 0=init, uneven=reach, even=elim
    int                *progress;       // progress transitions
    size_t              progress_trans; // progress transitions
    lts_file_t          lts;
    ltsmin_parse_env_t  env;
    ltsmin_expr_t       inv_expr;
};

static void
add_results (counter_t *res, counter_t *cnt)
{
    res->runtime += cnt->runtime;
    res->visited += cnt->visited;
    res->explored += cnt->explored;
    res->allred += cnt->allred;
    res->trans += cnt->trans;
    res->level_max += cnt->level_max;
    res->waits += cnt->waits;
    res->rec += cnt->rec;
    res->bogus_red += cnt->bogus_red;
    res->splits += cnt->splits;
    res->transfer += cnt->transfer;
    res->deadlocks += cnt->deadlocks;
    res->violations += cnt->violations;
    res->errors += cnt->errors;
    res->exit += cnt->exit;
    res->time += RTrealTime (cnt->timer);
    res->updates += cnt->updates;
    res->inserts += cnt->inserts;
    res->deletes += cnt->deletes;
}

void
add_stats(stats_t *res, stats_t *stat)
{
    res->elts += stat->elts;
    res->nodes += stat->nodes;
    res->tests += stat->tests;
    res->misses += stat->misses;
    res->rehashes += stat->rehashes;
}

static inline bool
ecd_has_state (fset_t *table, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    return fset_find (table, &hash, &s->ref, NULL, false);
}

static inline uint32_t
ecd_get_state (fset_t *table, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    uint32_t           *p;
    int res = fset_find (table, &hash, &s->ref, (void **)&p, false);
    HREassert (res != FSET_FULL, "ECD table full");
    return res ? *p : UINT32_MAX;
}

static inline void
ecd_add_state (fset_t *table, state_info_t *s, size_t *level)
{
    //Warning (info, "Adding %zu", s->ref);
    uint32_t           *data;
    hash32_t            hash = ref_hash (s->ref);
    int res = fset_find (table, &hash, &s->ref, (void**)&data, true);
    HREassert (res != FSET_FULL, "ECD table full");
    HREassert (!res, "Element %zu already in ECD table", s->ref);
    if (level != NULL && !res) {
        HREassert (*level < UINT32_MAX, "Stack length overflow for ECD");
        *data = *level;
    }
}

static inline void
ecd_remove_state (fset_t *table, state_info_t *s)
{
    //Warning (info, "Removing %zu", s->ref);
    hash32_t            hash = ref_hash (s->ref);
    int success = fset_delete (table, &hash, &s->ref);
    HREassert (success, "Could not remove key from set");
}

static void
ctx_add_counters (wctx_t *ctx, counter_t *cnt, counter_t *red, stats_t *stats)
{
    if (NULL != ctx->rec_ctx)                   // recursion
        ctx_add_counters (ctx->rec_ctx, cnt+1, red+1, NULL);
    if (ctx == global->contexts[ctx->id])        // top level
        add_stats (stats, ctx->stats);
    add_results(cnt, &ctx->counters);
    add_results(red, &ctx->red);
}

static void *
get_state (ref_t ref, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_data_t        state = get (global->dbs, ref, ctx->store2);
    return Tree & db_type ? TreeDBSLLdata(global->dbs, state) : state;
}

static inline void
wait_seed (wctx_t *ctx, ref_t seed)
{
    int didwait = 0;
    while (get_wip(seed) > 0 && !lb_is_stopped(global->lb)) { didwait = 1; } //wait
    if (didwait) {
        ctx->red.waits++;
    }
}

static inline void
increase_level (counter_t *cnt)
{
    cnt->level_cur++;
    if (cnt->level_cur > cnt->level_max) {
        cnt->level_max = cnt->level_cur;
    }
}

static inline void
set_all_red (wctx_t *ctx, state_info_t *state)
{
    if (global_try_color(state->ref, GRED, ctx->rec_bits)) {
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
    if (global_try_color(state->ref, GRED, ctx->rec_bits)) {
        ctx->red.explored++;
        if ( GBbuchiIsAccepting(ctx->model, get_state(state->ref, ctx)) )
            ctx->red.visited++; /* count accepting states */
    } else {
        ctx->red.bogus_red++;
    }
}

static hash64_t
z_rehash (const void *v, int b, hash64_t seed)
{
    return zobrist_rehash (global->zobrist, seed);
    (void)b; (void)v;
}

static int
find_or_put_zobrist (state_info_t *state, transition_info_t *ti,
                     state_info_t *pred, state_data_t store)
{
    state->hash64 = zobrist_hash_dm (global->zobrist, state->data, pred->data,
                                     pred->hash64, ti->group);
    return DBSLLlookup_hash (global->dbs, state->data, &state->ref, &state->hash64);
    (void) store;
}

static int
find_or_put_dbs (state_info_t *state, transition_info_t *ti,
                 state_info_t *predecessor, state_data_t store)
{
    return DBSLLlookup_hash (global->dbs, state->data, &state->ref, NULL);
    (void) predecessor; (void) store; (void) ti;
}

static int
find_or_put_tree (state_info_t *state, transition_info_t *ti,
                  state_info_t *pred, state_data_t store)
{
    int                 ret;
    ret = TreeDBSLLlookup_dm (global->dbs, state->data, pred->tree, store, ti->group);
    state->tree = store;
    state->ref = TreeDBSLLindex (global->dbs, state->tree);
    return ret;
}

//static const lm_status_t LM_WHITE = 0;
static const lm_status_t LM_RED  = 1;
static const lm_status_t LM_BLUE = 2;
static const lm_status_t LM_BOTH = 1 | 2;

typedef union ta_cndfs_state_u {
    struct val_s {
        ref_t           ref;
        lattice_t       lattice;
    } val;
    char                data[16];
} ta_cndfs_state_t;

static inline void
new_state (ta_cndfs_state_t *out, state_info_t *si)
{
    out->val.ref = si->ref;
    out->val.lattice = si->lattice;
}

static void
exit_ltsmin (int sig)
{
    if (HREme(HREglobal()) != 0)
        return;
    if ( !lb_stop(global->lb) ) {
        Abort ("UNGRACEFUL EXIT");
    } else {
        Warning (info, "PREMATURE EXIT (caught signal: %d)", sig);
    }
}

static int num_global_bits (strategy_t s) {
    HREassert (GRED.g == 0);
    HREassert (GGREEN.g == 1);
    HREassert (GDANGEROUS.g == 2);
    return (Strat_ENDFS  & s ? 3 :
           ((Strat_CNDFS | Strat_DFSFIFO) & s ? 2 :
           ((Strat_LNDFS | Strat_OWCTY | Strat_TA) & s ? 1 :
           ((Strat_DFS & s) && proviso == Proviso_Stack ? 1 : 0) )));
}

wctx_t *
wctx_create (model_t model, int depth, wctx_t *shared)
{
    HREassert (NULL == 0, "NULL != 0");
    RTswitchAlloc (!SPEC_MT_SAFE);
    wctx_t             *ctx = RTalignZero (CACHE_LINE_SIZE, sizeof (wctx_t));
    ctx->id = HREme (HREglobal());
    ctx->strategy = strategy[depth];
    ctx->model = model;
    ctx->work = SIZE_MAX; // essential for ENDFS load balancing
    if (strategy[depth] & Strat_PBFS) {
        ctx->queues = RTmalloc (sizeof(isb_allocator_t[2][W]));
        for (size_t q = 0; q < 2; q++)
        for (size_t i = 0; i < W; i++)
            ctx->queues[(i << 1) + q] = isba_create (state_info_int_size());
    } else {
        ctx->stack = dfs_stack_create (state_info_int_size());
        ctx->out_stack = ctx->in_stack = ctx->stack;
        if (strategy[depth] & Strat_2Stacks)
            ctx->in_stack = dfs_stack_create (state_info_int_size());
        if (strategy[depth] & (Strat_CNDFS | Strat_OWCTY | Strat_DFSFIFO)) //third stack for accepting states
            ctx->out_stack = dfs_stack_create (state_info_int_size());
        if (UseGreyBox == call_mode && Strat_DFS == strategy[depth]) {
            ctx->group_stack = isba_create (1);
        }

    }
    RTswitchAlloc (false);

    //allocate two bits for NDFS colorings
    if (strategy[depth] & Strat_LTL) {
        if (strategy[0] & Strat_TA) {
            ctx->cyan = fset_create (sizeof(ta_cndfs_state_t), 0, 10, 28);
            ctx->pink = fset_create (sizeof(ta_cndfs_state_t), 0, FSET_MIN_SIZE, 20);
            ctx->cyan2= fset_create (sizeof(ref_t), sizeof(void *), 10, 20);
        }
        if (~Strat_OWCTY & strategy[depth]) {
            size_t local_bits = 2;
            int res = bitvector_create (&ctx->color_map, local_bits<<dbs_size);
            res &= bitvector_create (&ctx->all_red, MAX_STACK);
            if (-1 == res) Abort ("Failure to allocate a bitvector.");
        } else if (ecd && (strategy[1] & Strat_ECD)) {
            ctx->cyan = fset_create (sizeof(ref_t), sizeof(uint32_t), 10, 20);
        }
        if (strategy[0] & Strat_DFSFIFO) {
            ctx->cyan = fset_create (sizeof(ref_t), 0, 10, 20);
            // find progress transitions
            lts_type_t      ltstype = GBgetLTStype (ctx->model);
            int             statement_label = lts_type_find_edge_label (
                                             ltstype, LTSMIN_EDGE_TYPE_STATEMENT);
            if (statement_label != -1) {
                int             statement_type = lts_type_get_edge_label_typeno (
                                                         ltstype, statement_label);
                size_t          count = GBchunkCount (ctx->model, statement_type);
                if (count >= K) {
                    ctx->progress = RTmallocZero (sizeof(int[K]));
                    for (size_t i = 0; i < K; i++) {
                        chunk c = GBchunkGet (ctx->model, statement_type, i);
                        ctx->progress[i] = strstr(c.data, LTSMIN_VALUE_STATEMENT_PROGRESS) != NULL;
                        ctx->progress_trans += ctx->progress[i];
                    }
                }
            }
        }
    } else if ((strategy[0] & Strat_DFS) && Proviso_Stack) {
        ctx->cyan = fset_create (sizeof(ref_t), 0, 10, 20);
    }
    state_info_create_empty (&ctx->state);
    ctx->store = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->store2 = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2);
    ctx->permute = permute_create (permutation, ctx->model, W, K, ctx->id);
    ctx->rec_bits = (depth ? shared->rec_bits + num_global_bits(strategy[depth-1]) : 0) ;
    ctx->rec_ctx = NULL;
    ctx->red.timer = RTcreateTimer ();
    ctx->counters.timer = RTcreateTimer ();
    statistics_init (&ctx->counters.lattice_ratio);
    ctx->red.time = 0;
    ctx->inv_expr = NULL;
    if (~(Strat_ECD|Strat_MAP) & strategy[depth+1])
        ctx->rec_ctx = wctx_create (model, depth+1, ctx);
    return ctx;
}

void
wctx_free_rec (wctx_t *ctx, int depth)
{
    RTfree (ctx->store);
    RTfree (ctx->store2);
    if (strategy[depth] & Strat_LTL) {  
        if (strategy[0] & Strat_TA) {
            fset_free (ctx->cyan);
            fset_free (ctx->pink);
        }
        if (~Strat_OWCTY & strategy[depth]) {
            bitvector_free (&ctx->all_red);
            bitvector_free (&ctx->color_map);
        } else if (ecd && (strategy[1] & Strat_ECD)) {
            fset_free (ctx->cyan);
        }
    }
    if (NULL != ctx->permute)
        permute_free (ctx->permute);

    RTswitchAlloc (!SPEC_MT_SAFE);
    if (NULL != ctx->group_stack)
        isba_destroy (ctx->group_stack);
    //if (strategy[depth] & (Strat_BFS | Strat_ENDFS | Strat_CNDFS))
    //    dfs_stack_destroy (ctx->in_stack);
    if (strategy[depth] & (Strat_CNDFS | Strat_OWCTY | Strat_DFSFIFO))
        dfs_stack_destroy (ctx->stack); 
    if (strategy[depth] ==  (Strat_2Stacks))
        dfs_stack_destroy (ctx->in_stack);
    if ( NULL != ctx->rec_ctx )
        wctx_free_rec (ctx->rec_ctx, depth+1);
    RTfree (ctx);
    RTswitchAlloc (false);
}

void
wctx_free (wctx_t *ctx)
{
    wctx_free_rec (ctx, 0);
}

/**
 * Performs those allocations that are absolutely necessary for local initiation
 * It initializes a mutex and a table of chunk tables.
 */
void
prelocal_global_init ()
{
    if (HREme(HREglobal()) == 0) {
        RTswitchAlloc (!SPEC_MT_SAFE);
        global = RTmallocZero (sizeof(global_t));
        global->exit_status = LTSMIN_EXIT_SUCCESS;
        RTswitchAlloc (false);
        //                               multi-process && multiple processes
        global->tables = cct_create_map (!SPEC_MT_SAFE && HREdefaultRegion(HREglobal()) != NULL);
#if SPEC_MT_SAFE == 1
        pthread_mutex_init (&global->mutex, NULL);
#endif
        GBloadFileShared (NULL, files[0]); // NOTE: no model argument
    }
    HREreduce (HREglobal(), 1, &global, &global, Pointer, Max);
}

/**
 * Performs those global allocations that can be delayed.
 * No barrier needed before calling this function.
 */
void
postlocal_global_init (wctx_t *ctx)
{
    int id = HREme (HREglobal());
    if (id == 0) {
        matrix_t           *m = GBgetDMInfo (ctx->model);
        size_t              bits = global_bits + count_bits;
        RTswitchAlloc (!SPEC_MT_SAFE);
        if (db_type == HashTable) {
            if (ZOBRIST)
                global->zobrist = zobrist_create (D, ZOBRIST, m);
            global->dbs = DBSLLcreate_sized (D, dbs_size, hasher, bits);
        } else {
            global->dbs = TreeDBSLLcreate_dm (D, dbs_size, ratio, m, bits,
                                             db_type == ClearyTree, indexing);
        }
        if (!ecd && strategy[1] != Strat_None)
            Abort ("Conflicting options --no-ecd and %s.", key_search(strategies, strategy[1]));
        if (strategy[1] == Strat_MAP || (trc_output && !(strategy[0] & (Strat_LTL | Strat_TA))))
            global->parent_ref = RTmalloc (sizeof(ref_t[1UL<<dbs_size]));
        if (strategy[0] & Strat_OWCTY)
            global->pre = RTalignZero (CACHE_LINE_SIZE, sizeof(owcty_pre_t[1UL << dbs_size]));
        if (Strat_TA & strategy[0])
            global->lmap = lm_create (W, 1UL<<dbs_size, LATTICE_BLOCK_SIZE);
        global->lb = lb_create_max (W, G, H);
        global->contexts = RTmalloc (sizeof (wctx_t*[W]));
        global->threshold = strategy[0] & Strat_NDFS ? THRESHOLD : THRESHOLD / W;
        RTswitchAlloc (false);
    }
    HREbarrier (HREglobal());

    global->contexts[id] = ctx; // publish local context
    color_set_dbs (global->dbs);
    (void) signal (SIGINT, exit_ltsmin);

    RTswitchAlloc (!SPEC_MT_SAFE);
    lb_local_init (global->lb, ctx->id, ctx); // Barrier
    RTswitchAlloc (false);
}

void
statics_init (model_t model)
{
    if (strategy[0] == Strat_None)
        strategy[0] = (GBgetAcceptingStateLabelIndex(model) < 0 ?
              (strategy[0] == Strat_TA ? Strat_SBFS : Strat_BFS) : Strat_CNDFS);
#ifdef OPAAL
    if (!(strategy[0] & (Strat_CNDFS|Strat_Reach)))
        Abort ("Wrong strategy for timed verification: %s", key_search(strategies, strategy[0]));
    strategy[0] |= Strat_TA;
#endif
    W = HREpeers(HREglobal());
    if ((strategy[0] & Strat_DFSFIFO) && proviso != Proviso_None) Abort ("DFS_FIFO does not require a proviso.");
    if (GB_POR && (strategy[0] & ~Strat_DFSFIFO)) {
        if (strategy[0] & Strat_OWCTY) Abort ("OWCTY with POR not implemented.");
        if (strategy[0] & Strat_LTL) {
            if (W > 1) Abort ("Cannot use POR with more than one thread/process.");
            if (proviso == Proviso_None) Warning (info, "Forcing use of the stack cycle proviso");
            proviso = Proviso_Stack;
        } else if (inv_detect || act_detect) {
            if ((strategy[0] & ~Strat_DFS) || W > 1) Abort ("Cycle proviso for safety properties with this (parallel) search strategy is not yet implemented, use DFS.");
            if (proviso == Proviso_None) Warning (info, "Forcing use of the stack cycle proviso.");
            proviso = Proviso_Stack;
        }
        permutation = permutation_red = Perm_None;
    }
    lts_type_t          ltstype = GBgetLTStype (model);
    matrix_t           *m = GBgetDMInfo (model);
    SL = lts_type_get_state_label_count (ltstype);
    EL = lts_type_get_edge_label_count (ltstype);
    N = lts_type_get_state_length (ltstype);
    D = (strategy[0] & Strat_TA ? N - 2 : N);
    K = dm_nrows (m);
    if (Perm_Unknown == permutation) //default permutation depends on strategy
        permutation = strategy[0] & Strat_Reach ? Perm_None :
                     (strategy[0] & (Strat_TA | Strat_DFSFIFO) ? Perm_RR : Perm_Dynamic);
    if (Perm_None != permutation) {
         if (call_mode == UseGreyBox)
            Abort ("Greybox not supported with state permutation.");
        refs = 1; //The permuter works with references only!
    }
    if (strategy[0] & Strat_LTL)
        permutation_red = no_red_perm ? Perm_None : permutation;
    if (!(Strat_Reach & strategy[0]) && (dlk_detect || act_detect || inv_detect))
        Abort ("Verification of safety properties works only with reachability algorithms.");
    if (act_detect) {
        // table number of first edge label
        act_label = lts_type_find_edge_label_prefix (ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        if (act_label == -1)
            Abort("No edge label '%s...' for action detection", LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        act_type = lts_type_get_edge_label_typeno(ltstype, act_label);
        chunk c = chunk_str(act_detect);
        act_index = GBchunkPut(model, act_type, c);
    }
    char *end;
    dbs_size = strtol (table_size, &end, 10);
    if (dbs_size == 0) Abort ("Not a valid table size: -s %s", table_size);
    if (*end == '%') {
        size_t el_size = (db_type != HashTable ? 3 : D) * SLOT_SIZE; // over estimation for cleary
        size_t map_el_size = (Strat_TA & strategy[0] ? sizeof(lattice_t) : 0);
        size_t db_el_size = (RTmemSize() / 100 * dbs_size) / (el_size + map_el_size);
        dbs_size = (int) log2(db_el_size);
        dbs_size = dbs_size > DB_SIZE_MAX ? DB_SIZE_MAX : dbs_size;
    }
    MAX_SUCC = ( Strat_DFS == strategy[0] ? 1 : SIZE_MAX );  /* for --grey: */
    int i = 0;
    while (Strat_None != strategy[i] && i < MAX_STRATEGIES) {
        global_bits += num_global_bits (strategy[i]);
        local_bits += (~Strat_DFSFIFO & Strat_LTL & strategy[i++] ? 2 : 0);
    }
    count_bits = (Strat_LNDFS == strategy[i-1] ? ceil(log2(W+1)) : 0);
    indexing = NULL != trc_output || ((Strat_TA | Strat_LTLG) & strategy[0]);
    switch (db_type) {
    case HashTable:
        if (ZOBRIST) {
            find_or_put = find_or_put_zobrist;
            hasher = (hash64_f) z_rehash;
        } else {
            find_or_put = find_or_put_dbs;
            hasher = (hash64_f) MurmurHash64;
        }
        statistics = (dbs_stats_f) DBSLLstats;
        get = (dbs_get_f) DBSLLget;
        setup_colors (NULL, count_bits, (dbs_get_sat_f)DBSLLget_sat_bit,
                     (dbs_try_set_sat_f) DBSLLtry_set_sat_bit,
                     (dbs_inc_sat_bits_f)DBSLLinc_sat_bits,
                     (dbs_dec_sat_bits_f)DBSLLdec_sat_bits,
                     (dbs_get_sat_bits_f)DBSLLget_sat_bits);
        break;
    case ClearyTree:
        if (indexing) Abort ("Cleary tree not supported in combination with error trails or the MCNDFS algorithms.");
    case TreeTable:
        if (ZOBRIST)
            Abort ("Zobrist and treedbs is not implemented");
        statistics = (dbs_stats_f) TreeDBSLLstats;
        get = (dbs_get_f) TreeDBSLLget;
        find_or_put = find_or_put_tree;
        setup_colors (NULL, count_bits, (dbs_get_sat_f)TreeDBSLLget_sat_bit,
                     (dbs_try_set_sat_f) TreeDBSLLtry_set_sat_bit,
                     (dbs_inc_sat_bits_f)TreeDBSLLinc_sat_bits,
                     (dbs_dec_sat_bits_f)TreeDBSLLdec_sat_bits,
                     (dbs_get_sat_bits_f)TreeDBSLLget_sat_bits);
        break;
    case Tree: default: Abort ("Unknown state storage type: %d.", db_type);
    }
}

static void
print_setup (wctx_t *ctx)
{
    if ((strategy[0] & Strat_LTL) && call_mode == UseGreyBox)
        Warning(info, "Greybox not supported with strategy NDFS, ignored.");
    Warning (info, "There are %zu state labels and %zu edge labels", SL, EL);
    Warning (info, "State length is %zu, there are %zu groups", N, K);
    if (act_detect)
        Warning(info, "Detecting action \"%s\"", act_detect);
    Warning (info, "Running %s on %zu %s", key_search(strategies, strategy[0] & ~Strat_TA),
             W, W == 1 ? "core (sequential)" : (SPEC_MT_SAFE ? "threads" : "processes"));
    if (db_type == HashTable) {
        Warning (info, "Using a hash table with 2^%d elements", dbs_size);
    } else
        Warning (info, "Using a%s tree table with 2^%d elements", indexing ? "" : " non-indexing", dbs_size);
    Warning (info, "Global bits: %d, count bits: %d, local bits: %d",
             global_bits, count_bits, local_bits);
    if (strategy[0] & Strat_DFSFIFO)
            Warning (info, "Found %zu progress transitions.", ctx->progress_trans);
    Warning (info, "Successor permutation: %s", key_search(permutations, permutation));
    if (GB_POR) {
        int            *visibility = GBgetPorGroupVisibility (ctx->model);
        size_t          visibles = 0, labels = 0;
        for (size_t i = 0; i < K; i++)
            visibles += visibility[i];
        visibility = GBgetPorStateLabelVisibility (ctx->model);
        for (size_t i = 0; i < SL; i++)
            labels += visibility[i];
        Warning (info, "Visible groups: %zu / %zu, labels: %zu / %zu", visibles, K, labels, SL);
        Warning (info, "POR cycle proviso: %s %s", key_search(provisos, proviso), strategy[0] & Strat_LTL ? "(ltl)" : "");
    }
}

/**
 * Initialize locals: model and settings
 */
wctx_t *
local_init ()
{
    model_t             model = GBcreateBase ();

    cct_cont_t         *map = cct_create_cont (global->tables);
    GBsetChunkMethods (model, (newmap_t)cct_create_vt, map,
                       HREgreyboxI2C,
                       HREgreyboxC2I,
                       HREgreyboxCAtI,
                       HREgreyboxCount);

    Print1 (info, "Loading model from %s", files[0]);
#if SPEC_MT_SAFE == 1
    // some frontends (opaal) do not have a thread-safe initial state function
#ifdef OPAAL
    pthread_mutex_lock (&global->mutex);
#endif
    GBloadFile (model, files[0], &model);
#ifdef OPAAL
    pthread_mutex_unlock (&global->mutex);
#endif

    if (HREme(HREglobal()) == 0)
        statics_init (model);
    HREbarrier(HREglobal());
#else
    GBloadFile (model, files[0], &model);

    // in the multi-process environment, we initialize statics locally:
    statics_init (model);
#endif

    wctx_t          *ctx = wctx_create (model, 0, NULL);

    if (inv_detect) { // local parsing
        ctx->env = LTSminParseEnvCreate();
        ctx->inv_expr = parse_file_env (inv_detect, pred_parse_file, model, ctx->env);
    }

    if (GB_POR) {
        if (strategy[0] & Strat_DFSFIFO) {
            int progress_sl = GBgetProgressStateLabelIndex (model);
            HREassert (progress_sl >= 0, "No progress labels defined for DFS_FIFO");
            pins_add_state_label_visible (model, progress_sl);
        }
        if (ctx->inv_expr) {
            mark_visible (model, ctx->inv_expr, ctx->env);
        }
        if (act_detect) {
            pins_add_edge_label_visible (model, act_label, act_index);
        }
    }

    if (files[1]) {
        lts_type_t ltstype = GBgetLTStype (model);
        Print1 (info,"Writing output to %s",files[1]);
        if (strategy[0] & ~Strat_PBFS) {
            Print1 (info,"Switching to PBFS algorithm for LTS write");
            strategy[0] = Strat_PBFS;
        }
        lts_file_t          template = lts_vset_template ();
        if (label_filter != NULL) {
            string_set_t label_set = SSMcreateSWPset(label_filter);
            Print1 (info, "label filter is \"%s\"", label_filter);
            ctx->lts = lts_file_create_filter (files[1], ltstype, label_set, W, template);
            write_state=1;
        } else {
            ctx->lts = lts_file_create (files[1], ltstype, W, template);
            if (SL > 0) write_state = 1;
        }
        int T = lts_type_get_type_count (ltstype);
        for (int i = 0; i < T; i++)
            lts_file_set_table (ctx->lts, i, GBgetChunkMap(model,i));
        HREbarrier (HREglobal()); // opening is sometimes a collaborative operation. (e.g. *.dir)
    }
    if (HREme(HREglobal()) == 0)
        print_setup (ctx);
    return ctx;
}

static void
deinit_globals ()
{
    RTswitchAlloc (!SPEC_MT_SAFE);
    if (HashTable & db_type)
        DBSLLfree (global->dbs);
    else //TreeDBSLL
        TreeDBSLLfree (global->dbs);
    lb_destroy(global->lb);
    if (global->lmap != NULL)
        lm_free (global->lmap);
    if (global->zobrist != NULL)
        zobrist_free(global->zobrist);
    RTfree (global->contexts);
    RTswitchAlloc (false);
}

static void
deinit_all (wctx_t *ctx)
{
    if (ctx->lts)
        lts_file_close (ctx->lts);
    if (0 == ctx->id)
        deinit_globals ();
    wctx_free (ctx);
    HREbarrier (HREglobal());
}

static inline void
print_state_space_total (char *name, counter_t *cnt)
{
    Warning (info, "%s%zu levels %zu states %zu transitions",
             name, cnt->level_max, cnt->explored, cnt->trans);
}

static inline void
maybe_report (counter_t *cnt, char *msg)
{
    if (EXPECT_TRUE(!log_active(info) || cnt->explored < global->threshold))
        return;
    if (!cas (&global->threshold, global->threshold, global->threshold << 1))
        return;
    if (W == 1 || (strategy[0] & Strat_NDFS))
        print_state_space_total (msg, cnt);
    else
        Warning (info, "%s%zu levels ~%zu states ~%zu transitions", msg,
                 cnt->level_max, W * cnt->explored,  W * cnt->trans);
}

static void
print_totals (counter_t *ar_reach, counter_t *ar_red, int d, size_t db_elts)
{
    counter_t          *reach = ar_reach;
    counter_t          *red = ar_red;
    reach->explored /= W;
    reach->trans /= W;
    red->trans /= W;
    if ( 0 == (Strat_LTLG & strategy[d]) ) {
        red->visited /= W;
        red->explored /= W;
    }
    Warning (info, "%s_%d (%s/%s) stats:",
             key_search(strategies, strategy[d] & ~Strat_TA), d+1,
             key_search(permutations, permutation),
             key_search(permutations, permutation_red));
    Warning (info, "blue states: %zu (%.2f%%), transitions: %zu (per worker)",
             reach->explored, ((double)reach->explored/db_elts)*100, reach->trans);
    Warning (info, "red states: %zu (%.2f%%), bogus: %zu  (%.2f%%), transitions: %zu, waits: %zu (%.2f sec)",
             red->explored, ((double)red->explored/db_elts)*100, red->bogus_red,
             ((double)red->bogus_red/db_elts), red->trans, red->waits, red->time);
    if  ( all_red && (strategy[d] & (Strat_LNDFS | Strat_NDFS | Strat_CNDFS  | Strat_ENDFS)) )
        Warning (info, "all-red states: %zu (%.2f%%), bogus %zu (%.2f%%)",
             reach->allred, ((double)reach->allred/db_elts)*100,
             red->allred, ((double)red->allred/db_elts)*100);
    if (Strat_None != strategy[d+1]) {
        print_totals (ar_reach + 1, ar_red + 1, d+1, db_elts);
    }
}

static void
print_statistics (counter_t *ar_reach, counter_t *ar_red, rt_timer_t timer,
                  stats_t *stats, lts_type_t ltstype)
{
    counter_t          *reach = ar_reach;
    counter_t          *red = ar_red;
    double              mem1, mem2, mem3=0, mem4, compr, fill, leafs;
    float               tot = RTrealTime (timer);
    size_t              db_elts = stats->elts;
    size_t              db_nodes = stats->nodes;
    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    double              el_size =
       db_type & Tree ? (db_type==ClearyTree?1:2) + (2.0 / (1UL<<ratio)) : D+.5;
    size_t              s = state_info_size();
    size_t              max_load;
    size_t              lattices = reach->inserts - reach->updates;
    if (Strat_LTL & strategy[0]) {
        max_load = reach->level_max+red->level_max;
        max_load += (Strat_DFSFIFO & strategy[0] ? max_level_size : 0);
        RTprintTimer (info, timer, "Total exploration time");
        Warning (info, " ");
        Warning (info, "State space has %zu states, %zu are accepting", db_elts,
                 red->visited);
        print_totals (ar_reach, ar_red, 0, db_elts);
        mem3 = ((double)(((((size_t)local_bits)<<dbs_size))/8*W)) / (1UL<<20);
        Warning (info, " ");
        Warning (info, "Total memory used for local state coloring: %.1fMB", mem3);
    } else {
        max_load = (Strat_SBFS & strategy[0] ? max_level_size : lb_max_load(global->lb));
        statistics_t state_stats; statistics_init (&state_stats);
        statistics_t trans_stats; statistics_init (&trans_stats);
        for (size_t i = 0; i< W; i++) {
            statistics_record (&state_stats, global->contexts[i]->counters.explored);
            statistics_record (&trans_stats, global->contexts[i]->counters.trans);
        }
        if (W > 1)
            Warning (info, "mean standard work distribution: %.1f%% (states) %.1f%% (transitions)",
                     (100 * statistics_stdev(&state_stats) / statistics_mean(&state_stats)),
                     (100 * statistics_stdev(&trans_stats) / statistics_mean(&trans_stats)));
        Warning (info, " ");
        reach->level_max /= W;
        print_state_space_total ("State space has ", reach);
        RTprintTimer (info, timer, "Total exploration time");
        double time = RTrealTime (timer);
        Warning(info, "States per second: %.0f, Transitions per second: %.0f",
                ar_reach->explored/time, ar_reach->trans/time);
        Warning(info, " ");
    }
    if (Strat_TA & strategy[0]) {
        size_t alloc = lm_allocated (global->lmap);
        mem3 = ((double)(sizeof(lattice_t[alloc + db_elts]))) / (1<<20);
        double lm = ((double)(sizeof(lattice_t[alloc + (1UL<<dbs_size)]))) / (1<<20);
        double redundancy = (((double)(db_elts + alloc)) / lattices - 1) * 100;
        Warning (info, "Lattice map: %.1fMB (~%.1fMB paged-in) overhead: %.2f%%, allocated: %zu", mem3, lm, redundancy, alloc);
    }

    mem1 = ((double)(s * max_load)) / (1 << 20);
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.2fMB",
             s, max_load, mem1);
    mem2 = ((double)(1UL << (dbs_size)) / (1<<20)) * SLOT_SIZE * el_size;
    mem4 = ((double)(db_nodes * SLOT_SIZE * el_size)) / (1<<20);
    fill = (double)((db_elts * 100) / (1UL << dbs_size));
    if (db_type & Tree) {
        compr = (double)(db_nodes * el_size) / ((D+1) * db_elts) * 100;
        leafs = (double)(((db_nodes - db_elts) * 100) / (1UL << (dbs_size-ratio)));
        Warning (info, "Tree memory: %.1fMB, compr.: %.1f%%, fill (roots/leafs): "
                "%.1f%%/%.1f%%", mem4, compr, fill, leafs);
    } else {
        Warning (info, "Table memory: %.1fMB, fill ratio: %.1f%%", mem4, fill);
    }
    double chunks = cct_print_stats (info, infoLong, ltstype, global->tables) / (1<<20);
    Warning (info, "Est. total memory use: %.1fMB (~%.1fMB paged-in)",
             mem1 + mem4 + mem3 + chunks, mem1 + mem2 + mem3 + chunks);

    if (no_exit || log_active(infoLong))
        HREprintf (info, "\nDeadlocks: %zu\nInvariant/valid-end state violations: %zu\n"
                 "Error actions: %zu\n", reach->deadlocks, reach->violations,
                 reach->errors);

    HREprintf (infoLong, "\nInternal statistics:\n\n"
             "Algorithm:\nWork time: %.2f sec\nUser time: %.2f sec\nExplored: %zu\n"
                 "Transitions: %zu\nWaits: %zu\nRec. calls: %zu\n\n"
             "Database:\nElements: %zu\nNodes: %zu\nMisses: %zu\nEq. tests: %zu\nRehashes: %zu\n\n"
             "Memory:\nQueue: %.1f MB\nDB: %.1f MB\nDB alloc.: %.1f MB\nColors: %.1f MB\n\n"
             "Load balancer:\nSplits: %zu\nLoad transfer: %zu\n\n"
             "Lattice MAP:\nRatio: %.2f\nInserts: %zu\nUpdates: %zu\nDeletes: %zu\n"
             "Red subsumed: %zu\nCyan is subsumed: %zu\n",
             tot, reach->runtime, reach->explored, reach->trans, red->waits,
             reach->rec, db_elts, db_nodes, stats->misses, stats->tests,
             stats->rehashes, mem1, mem4, mem2, mem3,
             reach->splits, reach->transfer,
             ((double)lattices/db_elts), reach->inserts, reach->updates,
             reach->deletes, red->updates, red->deletes);
}

void
reduce_and_print_result (wctx_t *ctx)
{
    RTswitchAlloc (!SPEC_MT_SAFE);
    ctx->stats = statistics (global->dbs);
    RTswitchAlloc (false);

    HREbarrier (HREglobal());
    if (0 == ctx->id) {
        counter_t          *reach = RTmallocZero (sizeof(counter_t[MAX_STRATEGIES]));
        counter_t          *red = RTmallocZero (sizeof(counter_t[MAX_STRATEGIES]));
        stats_t            *stats = RTmallocZero (sizeof (stats_t));
        for (size_t i = 0; i < W; i++)
            ctx_add_counters (global->contexts[i], reach, red, stats);
        if (log_active(info)) {
            print_statistics (reach, red, global->contexts[0]->timer, stats,
                              GBgetLTStype(ctx->model));
        }
        RTfree (reach); RTfree (red); RTfree (stats);
    }
    HREbarrier (HREglobal());
}

static void
print_thread_statistics (wctx_t *ctx)
{
    char                name[128];
    char               *format = "[%zu%s] saw in %.3f sec ";
    if (W < 4 || log_active(infoLong)) {
    if ((Strat_Reach | Strat_OWCTY) & strategy[0]) {
        snprintf (name, sizeof name, format, ctx->id, "", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
        if (Strat_ECD & strategy[1])
            fset_print_statistics (ctx->cyan, "ECD set");
    } else if (Strat_LTL & strategy[0]) {
        snprintf (name, sizeof name, format, ctx->id, " B", ctx->counters.runtime);
        print_state_space_total (name, &ctx->counters);
        snprintf (name, sizeof name, format, ctx->id, " R", ctx->counters.runtime);
        print_state_space_total (name, &ctx->red);

        if (Strat_TA & strategy[0]) {
            fset_print_statistics (ctx->cyan, "Cyan set");
            fset_print_statistics (ctx->pink, "Pink set");
        }
    }}
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

static const int            RR_ARRAY_SIZE = 16;

static int
rr_cmp (const void *a, const void *b, void *arg)
{
    permute_t          *perm = (permute_t *) arg;
    int                *rand = *perm->rand;
    ref_t               A = perm->todos[*((int*)a)].si.ref;
    ref_t               B = perm->todos[*((int*)b)].si.ref;
    return ((((1UL<<dbs_size)-1)&rand[A & ((1<<RR_ARRAY_SIZE)-1)])^A) -
           ((((1UL<<dbs_size)-1)&rand[B & ((1<<RR_ARRAY_SIZE)-1)])^B);
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

    int Aval = A->seen;
    int Bval = B->seen;
    if ((Strat_LTL & ~Strat_OWCTY & ctx->strategy) && A->seen == B->seen) {
        Aval = nn_color_eq(nn_get_color(&ctx->color_map, A->si.ref), NNWHITE);
        Bval = nn_color_eq(nn_get_color(&ctx->color_map, B->si.ref), NNWHITE);
    }
    if (Aval == Bval) // if dynamically no difference, then randomize:
        return rand[A->ti.group] - rand[B->ti.group];
    return Bval - Aval;
}

static inline void
perm_todo (permute_t *perm, state_data_t dst, transition_info_t *ti)
{
    HREassert (perm->nstored < perm->trans+TODO_MAX);
    permute_todo_t *next = perm->todos + perm->nstored;
    perm->tosort[perm->nstored] = perm->nstored;
    next->seen = state_info_initialize (&next->si, dst, ti, perm->state, perm->ctx);
    next->si.data = (raw_data_t) -2; // we won't copy these around, since they
    next->si.tree = (raw_data_t) -2; // are is stored in the DB and we have a reference
    next->ti.group = ti->group;
    next->ti.labels = ti->labels;
    perm->nstored++;
}

static char *
full_msg(int ret)
{
    return (DB_FULL == ret ? "hash table" : (DB_ROOTS_FULL == ret ? "tree roots table" : "tree leafs table"));
}

static inline void
perm_do (permute_t *perm, int i)
{
    permute_todo_t *todo = perm->todos + i;
    if (todo->seen < 0)
        if (lb_stop(global->lb)) Warning (info, "Error: %s full! Change -s/--ratio.", full_msg(todo->seen));
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
    perm->shiftorder = (1UL<<dbs_size) / workers * worker_index;
    perm->start_group = perm->shift * worker_index;
    perm->trans = trans;
    perm->model = model;
    perm->permutation = permutation;
    if (Perm_Otf == perm->permutation)
        perm->pad = RTalign (CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
    if (Perm_Random == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*[trans+TODO_MAX]));
        for (size_t i = 1; i < perm->trans+TODO_MAX; i++) {
            perm->rand[i] = RTalign (CACHE_LINE_SIZE, sizeof(int[ i ]));
            randperm (perm->rand[i], i, perm->shiftorder);
        }
    }
    if (Perm_RR == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign (CACHE_LINE_SIZE, sizeof(int[1<<RR_ARRAY_SIZE]));
        srandom (time(NULL) + 9876432*worker_index);
        for (int i =0; i < (1<<RR_ARRAY_SIZE); i++)
            perm->rand[0][i] = random();
    }
    if (Perm_SR == perm->permutation || Perm_Dynamic == perm->permutation) {
        perm->rand = RTalignZero (CACHE_LINE_SIZE, sizeof(int*));
        perm->rand[0] = RTalign (CACHE_LINE_SIZE, sizeof(int[trans+TODO_MAX]));
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
    if ( Perm_RR == perm->permutation || Perm_SR == perm->permutation ||
         Perm_Dynamic == perm->permutation ) {
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
        if (seen < 0)
            if (lb_stop(global->lb)) Warning (info, "Error: %s full! Change -s/--ratio.", full_msg(seen));
        perm->real_cb (perm->ctx, &successor, ti, seen);
        break;
    case Perm_Shift_All:
        if (0 == perm->start_group_index && ti->group >= perm->start_group)
            perm->start_group_index = perm->nstored;
    case Perm_Dynamic:
    case Perm_Random:
    case Perm_SR:
    case Perm_RR:
    case Perm_Otf:
    case Perm_Sort:
        perm_todo (perm, dst, ti);
        break;
    default:
        Abort ("Unknown permutation!");
    }
}

int
permute_trans (permute_t *perm, state_info_t *state, perm_cb_f cb, void *ctx)
{
    perm->ctx = ctx;
    perm->real_cb = cb;
    perm->state = state;
    perm->nstored = perm->start_group_index = 0;
    int v[N];
    int count;
    if ((Strat_TA & strategy[0]) && (refs || (Tree & db_type))) {
        memcpy (v, state->data, D<<2);
        ((lattice_t*)(v + D))[0] = state->lattice;
        count = GBgetTransitionsAll (perm->model, v, permute_one, perm);
    } else {
        count = GBgetTransitionsAll (perm->model, state->data, permute_one, perm);
    }
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
    case Perm_RR:
        qsortr (perm->tosort, perm->nstored, sizeof(int), rr_cmp, perm);
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
        Abort ("Unknown permutation!");
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
    size_t              data_size = SLOT_SIZE * (HashTable & db_type ? D : 2*D);
    size_t              state_info_size = refs ? ref_size : data_size;
    if (!refs && (HashTable & db_type))
        state_info_size += ref_size;
    if (ZOBRIST)
        state_info_size += sizeof (hash64_t);
    if (Strat_OWCTY & strategy[0])
        state_info_size++;
    if (Strat_TA & strategy[0])
        state_info_size += sizeof (lattice_t) + sizeof (lm_loc_t);
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
    if (Strat_TA & strategy[0]) {
        state->lattice = *(lattice_t*)(data + D);
        state->loc = LM_NULL_LOC;
    }
    return find_or_put (state, ti, src, ctx->store2);
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_serialize (state_info_t *state, raw_data_t data)
{
    if (ZOBRIST) {
        ((uint64_t*)data)[0] = state->hash64;
        data += 2;
    }
    if (refs) {
        ((ref_t*)data)[0] = state->ref;
        data += 2;
    } else if (HashTable & db_type) {
        ((ref_t*)data)[0] = state->ref;
        data += 2;
        memcpy (data, state->data, (SLOT_SIZE * D));
        data += D;
    } else { // Tree
        memcpy (data, state->tree, (2 * SLOT_SIZE * D));
        data += D<<1;
    }
    if (Strat_TA & strategy[0]) {
        ((lattice_t*)data)[0] = state->lattice;
        HREassert (state->lattice != 0);
        data += 2;
        ((lm_loc_t*)data)[0] = state->loc;
    }
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_deserialize (state_info_t *state, raw_data_t data, state_data_t store)
{
    if (ZOBRIST) {
        state->hash64 = ((hash64_t*)data)[0];
        data += 2;
    }
    if (refs) {
        state->ref  = ((ref_t*)data)[0];
        data += 2;
        state->data = get (global->dbs, state->ref, store);
        if (Tree & db_type) {
            state->tree = state->data;
            state->data = TreeDBSLLdata (global->dbs, state->data);
        }
    } else {
        if (HashTable & db_type) {
            state->ref  = ((ref_t*)data)[0];
            data += 2;
            state->data = data;
            data += D;
        } else { // Tree
            state->tree = data;
            state->data = TreeDBSLLdata (global->dbs, data);
            state->ref  = TreeDBSLLindex (global->dbs, data);
            data += D<<1;
        }
    }
    if (Strat_TA & strategy[0]) {
        state->lattice = ((lattice_t*)data)[0];
        HREassert (state->lattice != 0);
        data += 2;
        state->loc = ((lm_loc_t*)data)[0];
    }
}

void
state_info_deserialize_cheap (state_info_t *state, raw_data_t data)
{
    HREassert (refs);
    if (ZOBRIST) {
        state->hash64 = ((hash64_t*)data)[0];
        data += 2;
    }
    state->ref  = ((ref_t*)data)[0];
    data += 2;
    if (Strat_TA & strategy[0]) {
        state->lattice = ((lattice_t*)data)[0];
        HREassert (state->lattice != 0);
        data += 2;
        state->loc = ((lm_loc_t*)data)[0];
    }
}

static void *
get_stack_state (ref_t ref, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    ta_cndfs_state_t   *state = (ta_cndfs_state_t *) SIget(ctx->si, ref);
    state_data_t        data  = get_state (state->val.ref, ctx);
    Debug ("Trace %zu (%zu,%zu)", ref, state->val.ref, state->val.lattice);
    if (strategy[0] & Strat_TA) {
        memcpy (ctx->store, data, D<<2);
        ((lattice_t*)(ctx->store + D))[0] = state->val.lattice;
        data = ctx->store;
    }
    return data;
}

static void
find_and_write_dfs_stack_trace (wctx_t *ctx, int level)
{
    ref_t          *trace = RTmalloc (sizeof(ref_t) * level);
    ctx->si = SIcreate();
    for (int i = level - 1; i >= 0; i--) {
        state_data_t data = dfs_stack_peek_top (ctx->stack, i);
        state_info_deserialize_cheap (&ctx->state, data);
        ta_cndfs_state_t state;
        new_state(&state, &ctx->state);
        if (!(strategy[0] & Strat_TA)) state.val.lattice = 0;
        int val = SIputC (ctx->si, state.data, sizeof(struct val_s));
        trace[level - i - 1] = (ref_t) val;
    }
    trc_env_t          *trace_env = trc_create (ctx->model, get_stack_state, ctx);
    Warning (info, "Writing trace to %s", trc_output);
    trc_write_trace (trace_env, trc_output, trace, level);
    SIdestroy (&ctx->si);
    RTfree (trace);
}

static void
ndfs_report_cycle (wctx_t *ctx, state_info_t *cycle_closing_state)
{
    /* Stop other workers, exit if some other worker was first here */
    if ( !lb_stop(global->lb) )
        return;
    size_t              level = dfs_stack_nframes (ctx->stack) + 1;
    Warning (info, " ");
    Warning (info, "Accepting cycle FOUND at depth %zu!", level);
    Warning (info, " ");
    if (trc_output) {
        double uw = cct_finalize (global->tables, "BOGUS, you should not see this string.");
        Warning (infoLong, "Parallel chunk tables under-water mark: %.2f", uw);
        /* Write last state to stack to close cycle */
        state_data_t data = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (cycle_closing_state, data);
        find_and_write_dfs_stack_trace (ctx, level);
    }
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
}

static void
handle_error_trace (wctx_t *ctx)
{
    size_t              level = ctx->counters.level_cur;
    if (trc_output) {
        double uw = cct_finalize (global->tables, "BOGUS, you should not see this string.");
        Warning (infoLong, "Parallel chunk tables under-water mark: %.2f", uw);
        if (strategy[0] & Strat_TA) {
            if (W != 1 || strategy[0] != Strat_TA_DFS)
                Abort("Opaal error traces only supported with a single thread and DFS order");
            dfs_stack_leave (ctx->stack);
            level = dfs_stack_nframes (ctx->stack) + 1;
            find_and_write_dfs_stack_trace (ctx, level);
        } else {
            trc_env_t  *trace_env = trc_create (ctx->model, get_state, ctx);
            Warning (info, "Writing trace to %s", trc_output);
            trc_find_and_write (trace_env, trc_output, ctx->state.ref, level,
                                global->parent_ref, ctx->initial.ref);
        }
    }
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
}

/* Courcoubetis et al. NDFS, with extensions:
 * New NDFS algorithm by Schwoon/Esparza/Gaiser
 */

static void
ndfs_red_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    nndfs_color_t color = nn_get_color(&ctx->color_map, successor->ref);
    ti->por_proviso = 1; // only visit blue states to stay in reduced search space

    if (proviso != Proviso_None && !nn_color_eq(color, NNBLUE))
        return; // only revisit blue states to determinize POR
    if ( nn_color_eq(color, NNCYAN) ) {
        /* Found cycle back to the stack */
        ndfs_report_cycle(ctx, successor);
    } else if ( nn_color_eq(color, NNBLUE) && (ctx->strategy != Strat_LNDFS ||
            !global_has_color(ctx->state.ref, GRED, ctx->rec_bits)) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static void
ndfs_blue_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                  int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    nndfs_color_t color = nn_get_color (&ctx->color_map, successor->ref);

    if (proviso == Proviso_Stack)
        ti->por_proviso = !nn_color_eq(color, NNCYAN);
    /**
     * The following lines bear little resemblance to the algorithms in the
     * respective papers (NNDFS / LNDFS), but we must store all non-red states
     * on the stack here, in order to calculate all-red correctly later.
     */
    if ( ecd && nn_color_eq(color, NNCYAN) &&
            (GBbuchiIsAccepting(ctx->model, ctx->state.data) ||
             GBbuchiIsAccepting(ctx->model, get_state(successor->ref, ctx))) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, successor);
    } else if ((ctx->strategy == Strat_LNDFS && !global_has_color(ctx->state.ref, GRED, ctx->rec_bits)) ||
               (ctx->strategy != Strat_LNDFS && !nn_color_eq(color, NNPINK))) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static inline void
ndfs_explore_state_red (wctx_t *ctx)
{
    counter_t *cnt = &ctx->red;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation_red;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_red_handle, ctx);
    maybe_report (cnt, "[R] ");
}

static inline void
ndfs_explore_state_blue (wctx_t *ctx)
{
    counter_t *cnt = &ctx->counters;
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ndfs_blue_handle, ctx);
    cnt->explored++;
    maybe_report (cnt, "[B] ");
}

/* NNDFS dfs_red */
static void
ndfs_red (wctx_t *ctx, ref_t seed)
{
    ctx->red.visited++; //count accepting states
    ndfs_explore_state_red (ctx);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNBLUE) ) {
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                ndfs_explore_state_red (ctx);
                ctx->red.explored++;
            } else {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (ctx->stack);
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
        }
    }
}

/* NDFS dfs_blue */
void
ndfs_blue (wctx_t *ctx)
{
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) ) {
                bitvector_set ( &ctx->all_red, ctx->counters.level_cur );
                nn_set_color (&ctx->color_map, ctx->state.ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( ctx->counters.level_cur != 0 && !nn_color_eq(color, NNPINK) )
                    bitvector_unset ( &ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_t            seed;
            state_info_deserialize (&seed, state_data, ctx->store);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* exit if backtrack hits seed, leave stack the way it was */
                nn_set_color (&ctx->color_map, seed.ref, NNPINK);
                ctx->counters.allred++;
                if ( GBbuchiIsAccepting(ctx->model, seed.data) )
                    ctx->red.visited++;
            } else if ( GBbuchiIsAccepting(ctx->model, seed.data) ) {
                /* call red DFS for accepting states */
                ndfs_red (ctx, seed.ref);
                nn_set_color (&ctx->color_map, seed.ref, NNPINK);
            } else {
                if (ctx->counters.level_cur > 0)
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                nn_set_color (&ctx->color_map, seed.ref, NNBLUE);
            }
            dfs_stack_pop (ctx->stack);
        }
    }
}

/*
 * LNDFS by Laarman/Langerak/vdPol/Weber/Wijs (originally MCNDFS)
 *
 *  @incollection {springerlink:10.1007/978-3-642-24372-1_23,
       author = {Laarman, Alfons and Langerak, Rom and van de Pol, Jaco and Weber, Michael and Wijs, Anton},
       affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
       title = {{Multi-core Nested Depth-First Search}}},
       booktitle = {Automated Technology for Verification and Analysis},
       series = {Lecture Notes in Computer Science},
       editor = {Bultan, Tevfik and Hsiung, Pao-Ann},
       publisher = {Springer Berlin / Heidelberg},
       isbn = {978-3-642-24371-4},
       keyword = {Computer Science},
       pages = {321-335},
       volume = {6996},
       url = {http://eprints.eemcs.utwente.nl/20337/},
       note = {10.1007/978-3-642-24372-1_23},
       year = {2011}
    }
 */

/* LNDFS dfs_red */
static void
lndfs_red (wctx_t *ctx, ref_t seed)
{
    inc_wip (seed);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( !nn_color_eq(color, NNPINK) &&
                 !global_has_color(ctx->state.ref, GRED, ctx->rec_bits) ) {
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                ndfs_explore_state_red (ctx);
            } else {
                if (seed == ctx->state.ref)
                    break;
                dfs_stack_pop (ctx->stack);
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
                if ( global_try_color(ctx->state.ref, GRED, ctx->rec_bits) )
                    ctx->red.visited++; //count accepting states
                return;
            }
            set_red (ctx, &ctx->state);
            dfs_stack_pop (ctx->stack);
        }
    }
    //halted by the load balancer
    dec_wip (seed);
}

/* LNDFS dfs_blue */
void
lndfs_blue (wctx_t *ctx)
{
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( nn_color_eq(color, NNWHITE) &&
                 !global_has_color(ctx->state.ref, GRED, ctx->rec_bits) ) {
                bitvector_set (&ctx->all_red, ctx->counters.level_cur);
                nn_set_color (&ctx->color_map, ctx->state.ref, NNCYAN);
                ndfs_explore_state_blue (ctx);
            } else {
                if ( ctx->counters.level_cur != 0 && !global_has_color(ctx->state.ref, GRED, ctx->rec_bits) )
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes (ctx->stack))
                return;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            state_data = dfs_stack_top (ctx->stack);
            state_info_t            seed;
            state_info_deserialize (&seed, state_data, ctx->store);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* all successors are red */
                wait_seed (ctx, seed.ref);
                set_all_red (ctx, &seed);
            } else if ( GBbuchiIsAccepting(ctx->model, seed.data) ) {
                /* call red DFS for accepting states */
                lndfs_red (ctx, seed.ref);
            } else if (ctx->counters.level_cur > 0 &&
                       !global_has_color(seed.ref, GRED, ctx->rec_bits)) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
            }
            nn_set_color (&ctx->color_map, seed.ref, NNBLUE);
            dfs_stack_pop (ctx->stack);
        }
    }
}

extern void rec_ndfs_call (wctx_t *ctx, ref_t state);

/**
 * o Parallel NDFS algorithm by Evangelista/Pettruci/Youcef (ENDFS)
 * o Improved (Combination) NDFS algorithm (CNDFS).
     <Submitted to ATVA 2012>
 * o Combination of ENDFS and LNDFS (NMCNDFS)
     @inproceedings{pdmc11,
       month = {July},
       official_url = {http://dx.doi.org/10.4204/EPTCS.72.2},
       issn = {2075-2180},
       author = {A. W. {Laarman} and J. C. {van de Pol}},
       series = {Electronic Proceedings in Theoretical Computer Science},
       editor = {J. {Barnat} and K. {Heljanko}},
       title = {{Variations on Multi-Core Nested Depth-First Search}},
       address = {USA},
       publisher = {EPTCS},
       id_number = {10.4204/EPTCS.72.2},
       url = {http://eprints.eemcs.utwente.nl/20618/},
       volume = {72},
       location = {Snowbird, Utah},
       booktitle = {Proceedings of the 10th International Workshop on Parallel and Distributed Methods in verifiCation, PDMC 2011, Snowbird, Utah},
       year = {2011},
       pages = {13--28}
      }
 */
static void
endfs_lb (wctx_t *ctx)
{
    atomic_write (&ctx->done, 1);
    size_t workers[W];
    int idle_count = W-1;
    for (size_t i = 0; i<((size_t)W); i++)
        workers[i] = (i==ctx->id ? 0 : 1);
    while (0 != idle_count)
    for (size_t i=0; i<W; i++) {
        if (0==workers[i])
            continue;
        if (1 == atomic_read(&(global->contexts[i]->done))) {
            workers[i] = 0;
            idle_count--;
            continue;
        }
        ref_t work = atomic_read (&global->contexts[i]->work);
        if (SIZE_MAX == work)
            continue;
        rec_ndfs_call (ctx, work);
    }
}

static void
endfs_handle_dangerous (wctx_t *ctx)
{
    while ( dfs_stack_size(ctx->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (ctx->in_stack);
        state_info_deserialize_cheap (&ctx->state, state_data);
        if ( !global_has_color(ctx->state.ref, GDANGEROUS, ctx->rec_bits) &&
              ctx->state.ref != ctx->seed )
            if (global_try_color(ctx->state.ref, GRED, ctx->rec_bits))
                ctx->red.explored++;
    }
    if (global_try_color(ctx->seed, GRED, ctx->rec_bits)) {
        ctx->red.explored++;
        ctx->red.visited++;
    }
    if ( global_has_color(ctx->seed, GDANGEROUS, ctx->rec_bits) ) {
        rec_ndfs_call (ctx, ctx->seed);
    }
}

static void
cndfs_handle_nonseed_accepting (wctx_t *ctx)
{
    size_t nonred, accs;
    nonred = accs = dfs_stack_size(ctx->out_stack);
    if (nonred) {
        ctx->red.waits++;
        ctx->counters.rec += accs;
    }
    if (nonred) {
        RTstartTimer (ctx->red.timer);
        while ( nonred && !lb_is_stopped(global->lb) ) {
            nonred = 0;
            for (size_t i = 0; i < accs; i++) {
                raw_data_t state_data = dfs_stack_peek (ctx->out_stack, i);
                state_info_deserialize_cheap (&ctx->state, state_data);
                if (!global_has_color(ctx->state.ref, GRED, ctx->rec_bits))
                    nonred++;
            }
        }
        RTstopTimer (ctx->red.timer);
    }
    for (size_t i = 0; i < accs; i++)
        dfs_stack_pop (ctx->out_stack);
    while ( dfs_stack_size(ctx->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (ctx->in_stack);
        state_info_deserialize_cheap (&ctx->state, state_data);
        if (global_try_color(ctx->state.ref, GRED, ctx->rec_bits))
            ctx->red.explored++;
    }
}

static void
endfs_handle_red (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    /* Find cycle back to the seed */
    nndfs_color_t color = nn_get_color (&ctx->color_map, successor->ref);

    ti->por_proviso = 1; // only sequentially!
    if (proviso != Proviso_None && !nn_color_eq(color, NNBLUE))
         return; // only revisit blue states to determinize POR
    if ( nn_color_eq(color, NNCYAN) )
        ndfs_report_cycle (ctx, successor);
    /* Mark states dangerous if necessary */
    if ( Strat_ENDFS == ctx->strategy &&
         GBbuchiIsAccepting(ctx->model, get_state(successor->ref, ctx)) &&
         !global_has_color(successor->ref, GRED, ctx->rec_bits) )
        global_try_color(successor->ref, GDANGEROUS, ctx->rec_bits);
    if ( !nn_color_eq(color, NNPINK) &&
         !global_has_color(successor->ref, GRED, ctx->rec_bits) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static void
endfs_handle_blue (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    nndfs_color_t color = nn_get_color (&ctx->color_map, successor->ref);

    if (proviso == Proviso_Stack) // only sequentially!
        ti->por_proviso = !nn_color_eq(color, NNCYAN);

    /**
     * The following lines bear little resemblance to the algorithms in the
     * respective papers (Evangelista et al./ Laarman et al.), but we must
     * store all non-red states on the stack in order to calculate
     * all-red correctly later. Red states are also stored as optimization.
     */
    if ( ecd && nn_color_eq(color, NNCYAN) &&
         (GBbuchiIsAccepting(ctx->model, ctx->state.data) ||
         GBbuchiIsAccepting(ctx->model, get_state(successor->ref, ctx))) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle(ctx, successor);
    } else if ( all_red || (!nn_color_eq(color, NNCYAN) && !nn_color_eq(color, NNBLUE) &&
                            !global_has_color(successor->ref, GGREEN, ctx->rec_bits)) ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static inline void
endfs_explore_state_red (wctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation_red;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, endfs_handle_red, ctx);
    maybe_report (cnt, "[R] ");
}

static inline void
endfs_explore_state_blue (wctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, endfs_handle_blue, ctx);
    cnt->explored++;
    maybe_report (cnt, "[B] ");
}

/* ENDFS dfs_red */
static void
endfs_red (wctx_t *ctx, ref_t seed)
{
    size_t              seed_level = dfs_stack_nframes (ctx->stack);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( !nn_color_eq(color, NNPINK) &&
                 !global_has_color(ctx->state.ref, GRED, ctx->rec_bits) ) {
                nn_set_color (&ctx->color_map, ctx->state.ref, NNPINK);
                dfs_stack_push (ctx->in_stack, state_data);
                if ( Strat_CNDFS == strategy[0] && ctx->state.ref != seed && 
                     GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                    dfs_stack_push (ctx->out_stack, state_data);
                endfs_explore_state_red (ctx, &ctx->red);
            } else {
                if (seed_level == dfs_stack_nframes (ctx->stack))
                    break;
                dfs_stack_pop (ctx->stack);
            }
        } else { //backtrack
            dfs_stack_leave (ctx->stack);
            ctx->red.level_cur--;
            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes(ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }
}

void // just for checking correctness of all-red implementation. Unused.
check (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t *ctx=arg;
    HREassert (global_has_color(successor->ref, GRED, ctx->rec_bits) );
    (void) ti; (void) seen;
}

/* ENDFS dfs_blue */
void
endfs_blue (wctx_t *ctx)
{
    HREassert (ecd, "CNDFS's correctness depends crucially on ECD");
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            nndfs_color_t color = nn_get_color (&ctx->color_map, ctx->state.ref);
            if ( !nn_color_eq(color, NNCYAN) && !nn_color_eq(color, NNBLUE) &&
                 !global_has_color(ctx->state.ref, GGREEN, ctx->rec_bits) ) {
                if (all_red)
                    bitvector_set (&ctx->all_red, ctx->counters.level_cur);
                nn_set_color (&ctx->color_map, ctx->state.ref, NNCYAN);
                endfs_explore_state_blue (ctx, &ctx->counters);
            } else {
                if ( all_red && ctx->counters.level_cur != 0 && !global_has_color(ctx->state.ref, GRED, ctx->rec_bits) )
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
            }
        } else { //backtrack
            if (0 == dfs_stack_nframes(ctx->stack))
                break;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            /* Mark state GGREEN on backtrack */
            global_try_color (ctx->state.ref, GGREEN, ctx->rec_bits);
            nn_set_color (&ctx->color_map, ctx->state.ref, NNBLUE);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* all successors are red */
                //permute_trans (ctx->permute, &ctx->state, check, ctx); 
                set_all_red (ctx, &ctx->state);
            } else if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                ctx->seed = ctx->work = ctx->state.ref;
                endfs_red (ctx, ctx->seed);
                if (Strat_ENDFS == ctx->strategy)
                    endfs_handle_dangerous (ctx);
                else
                    cndfs_handle_nonseed_accepting (ctx);
                ctx->work = SIZE_MAX;
            } else if (all_red && ctx->counters.level_cur > 0 &&
                       !global_has_color(ctx->state.ref, GRED, ctx->rec_bits)) { 
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
            }
            dfs_stack_pop (ctx->stack);
        }
    }
    if ( Strat_ENDFS == ctx->strategy &&         // if ENDFS,
         ctx == global->contexts[ctx->id] &&     // if top-level ENDFS, and
         (Strat_LTLG & ctx->rec_ctx->strategy) ) // if rec strategy uses global bits (global pruning)
        endfs_lb (ctx);                          // then do simple load balancing
}

void
rec_ndfs_call (wctx_t *ctx, ref_t state)
{
    dfs_stack_push (ctx->rec_ctx->stack, (int*)&state);
    ctx->counters.rec++;
    switch (ctx->rec_ctx->strategy) {
    case Strat_ENDFS:
       endfs_blue (ctx->rec_ctx); break;
    case Strat_LNDFS:
       lndfs_blue (ctx->rec_ctx); break;
    case Strat_NDFS:
       ndfs_blue (ctx->rec_ctx); break;
    default:
       Abort ("Invalid recursive strategy.");
    }
}

/*
 * Reachability algorithms
 *  @incollection {springerlink:10.1007/978-3-642-20398-5_40,
     author = {Laarman, Alfons and van de Pol, Jaco and Weber, Michael},
     affiliation = {Formal Methods and Tools, University of Twente, The Netherlands},
     title = {{Multi-Core LTSmin: Marrying Modularity and Scalability}},
     booktitle = {NASA Formal Methods},
     series = {Lecture Notes in Computer Science},
     editor = {Bobaru, Mihaela and Havelund, Klaus and Holzmann, Gerard and Joshi, Rajeev},
     publisher = {Springer Berlin / Heidelberg},
     isbn = {978-3-642-20397-8},
     keyword = {Computer Science},
     pages = {506-511},
     volume = {6617},
     url = {http://eprints.eemcs.utwente.nl/20004/},
     note = {10.1007/978-3-642-20398-5_40},
     year = {2011}
   }
 */
ssize_t
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
        state_data_t        one = dfs_stack_pop (source_stack);
        HREassert (NULL != one);
        dfs_stack_push (target->in_stack, one);
    }
    source->counters.splits++;
    source->counters.transfer += handoff;
    return handoff;
}

ssize_t
split_sbfs (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    size_t              in_size = dfs_stack_size (source->in_stack);
    handoff = min (in_size >> 1, handoff);
    for (size_t i = 0; i < handoff; i++) {
        state_data_t        one = dfs_stack_pop (source->in_stack);
        HREassert (NULL != one);
        dfs_stack_push (target->in_stack, one);
    }
    source->counters.splits++;
    source->counters.transfer += handoff;
    return handoff;
}

ssize_t
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
    source->counters.splits++;
    source->counters.transfer += handoff;
    return handoff;
}

static inline size_t
in_load (wctx_t *ctx)
{
    return dfs_stack_frame_size(ctx->in_stack);
}

static inline size_t
bfs_load (wctx_t *ctx)
{
    return dfs_stack_frame_size(ctx->in_stack) + dfs_stack_frame_size(ctx->out_stack);
}

static inline void
deadlock_detect (wctx_t *ctx, int count)
{
    if (count > 0) return;
    ctx->counters.deadlocks++; // counting is costless
    if (GBstateIsValidEnd(ctx->model, ctx->state.data)) return;
    if ( !ctx->inv_expr ) ctx->counters.violations++;
    if (dlk_detect && (!no_exit || trc_output) && lb_stop(global->lb)) {
        Warning (info, " ");
        Warning (info, "Deadlock found in state at depth %zu!", ctx->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
invariant_detect (wctx_t *ctx, raw_data_t state)
{
    if ( !ctx->inv_expr ||
         eval_predicate(ctx->model, ctx->inv_expr, NULL, state, N, ctx->env) ) return;
    ctx->counters.violations++;
    if ((!no_exit || trc_output) && lb_stop(global->lb)) {
        Warning (info, " ");
        Warning (info, "Invariant violation (%s) found at depth %zu!", inv_detect, ctx->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static inline void
action_detect (wctx_t *ctx, transition_info_t *ti, state_info_t *successor)
{
    if (-1 == act_index || NULL == ti->labels || ti->labels[act_label] != act_index) return;
    ctx->counters.errors++;
    if ((!no_exit || trc_output) && lb_stop(global->lb)) {
        if (trc_output && successor->ref != ctx->state.ref) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        state_data_t data = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, data);
        dfs_stack_enter (ctx->stack);
        Warning (info, " ");
        Warning (info, "Error action '%s' found at depth %zu!", act_detect, ctx->counters.level_cur);
        Warning (info, " ");
        handle_error_trace (ctx);
    }
}

static void
reach_handle (void *arg, state_info_t *successor, transition_info_t *ti,
              int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    action_detect (ctx, ti, successor);
    ti->por_proviso = 1;
    if (!seen) {
        raw_data_t stack_loc = dfs_stack_push (ctx->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        if (EXPECT_FALSE( trc_output &&
                          successor->ref != ctx->state.ref &&
                          global->parent_ref[successor->ref] == 0 &&
                          ti != &GB_NO_TRANSITION )) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        ctx->counters.visited++;
    } else if (proviso == Proviso_Stack) {
        ti->por_proviso = !ecd_has_state (ctx->cyan, successor);
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
    if (seen < 0)
        if (lb_stop(global->lb)) Warning (info, "Error: %s full! Change -s or --ratio.", full_msg(seen));
    reach_handle (arg, &successor, ti, seen);
}

static inline size_t
explore_state (wctx_t *ctx, int next_index)
{
    size_t              count = 0;
    size_t              i = K;
    if (0 == next_index) { // first (grey) call with this state
        invariant_detect (ctx, ctx->state.data);
        if (ctx->counters.level_cur >= max_level) return K;
    }
    if ( UseBlackBox == call_mode )
        count = permute_trans (ctx->permute, &ctx->state, reach_handle, ctx);
    else { // UseGreyBox
        for (i = next_index; i<K && count<MAX_SUCC; i++)
            count += GBgetTransitionsLong (ctx->model, i, ctx->state.data,
                                           reach_handle_wrap, ctx);
    }
    if (0 == next_index) // last (grey) call with this state
        deadlock_detect (ctx, count);
    maybe_report (&ctx->counters, "");
    return i;
}

void
dfs_grey (wctx_t *ctx)
{
    uint32_t            next_index = 0;
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(ctx->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL == state_data) {
            if (0 != dfs_stack_nframes (ctx->stack)) {
                dfs_stack_leave (ctx->stack);
                ctx->counters.level_cur--;
                next_index = isba_pop_int (ctx->group_stack)[0];
            }
            continue;
        }
        if (next_index == K) {
            ctx->counters.explored++;
            dfs_stack_pop (ctx->stack);
        } else {
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            next_index = explore_state (ctx, next_index);
            isba_push_int (ctx->group_stack, (int *)&next_index);
        }
        next_index = 0;
    }
}

void
dfs_proviso (wctx_t *ctx)
{
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(ctx->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        // strict DFS (use extra bit because the permutor already adds successors to V)
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if (global_try_color(ctx->state.ref, GRED, 0)) {
                dfs_stack_enter (ctx->stack);
                increase_level (&ctx->counters);
                ecd_add_state (ctx->cyan, &ctx->state, NULL);
                explore_state (ctx, 0);
                ctx->counters.explored++;
            } else {
                dfs_stack_pop (ctx->stack);
            }
        } else {
            if (0 == dfs_stack_nframes (ctx->stack))
                continue;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            state_data = dfs_stack_pop (ctx->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            ecd_remove_state (ctx->cyan, &ctx->state);
        }
    }
    HREassert (lb_is_stopped(global->lb) || fset_count(ctx->cyan) == 0);
}

void
dfs (wctx_t *ctx)
{
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(ctx->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            explore_state (ctx, 0);
            ctx->counters.explored++;
        } else {
            if (0 == dfs_stack_nframes (ctx->stack))
                continue;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            dfs_stack_pop (ctx->stack);
        }
    }
}

void
bfs (wctx_t *ctx)
{
    while (lb_balance(global->lb, ctx->id, bfs_load(ctx), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            explore_state (ctx, 0);
            ctx->counters.explored++;
        } else {
            swap (ctx->out_stack, ctx->in_stack);
            ctx->stack = ctx->out_stack;
            increase_level (&ctx->counters);
        }
    }
}

static size_t
sbfs_level (wctx_t *ctx, size_t local_size)
{
    size_t             next_level_size;
    HREreduce (HREglobal(), 1, &local_size, &next_level_size, SizeT, Sum);
    increase_level (&ctx->counters);
    if (0 == ctx->id) {
        if (next_level_size > max_level_size)
            max_level_size = next_level_size;
        Warning(infoLong, "BFS level %zu has %zu states %zu total", ctx->counters.level_cur, next_level_size, ctx->counters.visited);
    }
    return next_level_size;
}

void
sbfs (wctx_t *ctx)
{
    size_t              next_level_size, local_next_size;
    do {
        while (lb_balance (global->lb, ctx->id, in_load(ctx), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
            if (NULL != state_data) {
                state_info_deserialize (&ctx->state, state_data, ctx->store);
                explore_state (ctx, 0);
                ctx->counters.explored++;
            }
        }
        local_next_size = dfs_stack_frame_size (ctx->out_stack);
        next_level_size = sbfs_level (ctx, local_next_size);
        lb_reinit (global->lb, ctx->id);
        swap (ctx->out_stack, ctx->in_stack);
        ctx->stack = ctx->out_stack;
    } while (next_level_size > 0 && !lb_is_stopped(global->lb));
}

static void
pbfs_queue_state (wctx_t *ctx, state_info_t *successor)
{
    hash64_t            h = ref_hash (successor->ref);
    wctx_t             *remote = global->contexts[h % W];
    size_t              local_next = (ctx->id << 1) + (1 - ctx->flip);
    raw_data_t stack_loc = isba_push_int (remote->queues[local_next], NULL); // communicate
    state_info_serialize (successor, stack_loc);
    ctx->red.explored++;
}

static void
pbfs_handle (void *arg, state_info_t *successor, transition_info_t *ti,
             int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    action_detect (ctx, ti, successor);
    if (!seen) {
        pbfs_queue_state (ctx, successor);
        if (EXPECT_FALSE( trc_output &&
                          successor->ref != ctx->state.ref &&
                          global->parent_ref[successor->ref] == 0) ) // race, but ok:
            atomic_write(&global->parent_ref[successor->ref], ctx->state.ref);
        ctx->counters.visited++;
    }
    if (EXPECT_FALSE(ctx->lts != NULL)) {
        int             src = ctx->counters.explored;
        int            *tgt = successor->data;
        int             tgt_owner = ref_hash (successor->ref) % W;
        lts_write_edge (ctx->lts, ctx->id, &src, tgt_owner, tgt, ti->labels);
    }
    ctx->counters.trans++;
    (void) ti;
}

void
pbfs (wctx_t *ctx)
{
    size_t              count;
    raw_data_t          state_data;
    int                 labels[SL];
    do {
        ctx->red.explored = 0;     // count states in next level
        ctx->flip = 1 - ctx->flip; // switch in;out stacks
        for (size_t i = 0; i < W; i++) {
            size_t          current = (i << 1) + ctx->flip;
            while ((state_data = isba_pop_int (ctx->queues[current])) &&
                    !lb_is_stopped(global->lb)) {
                state_info_deserialize (&ctx->state, state_data, ctx->store);
                invariant_detect (ctx, ctx->state.data);
                count = permute_trans (ctx->permute, &ctx->state, pbfs_handle, ctx);
                deadlock_detect (ctx, count);
                maybe_report (&ctx->counters, "");
                if (EXPECT_FALSE(ctx->lts && write_state)){
                    if (SL > 0)
                        GBgetStateLabelsAll (ctx->model, ctx->state.data, labels);
                    lts_write_state (ctx->lts, ctx->id, ctx->state.data, labels);
                }
                ctx->counters.explored++;
            }
        }
        count = sbfs_level (ctx, ctx->red.explored);
    } while (count && !lb_is_stopped(global->lb));
}

/**
 * DFS-FIFO for non-progress detection
 */

#define setV GRED
#define setF GGREEN

static void
dfs_fifo_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                 int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    ctx->counters.trans++;
    bool is_progress = ctx->progress_trans > 0 ? ctx->progress[ti->group] :  // ! progress transition
            GBstateIsProgress(ctx->model, get_state(successor->ref, ctx)); // ! progress state

    if (!is_progress && seen && ecd_has_state(ctx->cyan, successor))
         ndfs_report_cycle (ctx, successor);

    // dfs_fifo_dfs/dfs_fifo_bfs also check this, but we want a clean stack for LB!
    if (global_has_color(ctx->state.ref, setV, 0))
        return;
    if (!is_progress) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->counters.visited++;
    } else if (global_try_color(successor->ref, setF, 0)) { // new F state
        raw_data_t stack_loc = dfs_stack_push (ctx->out_stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->red.visited += !seen;
    }
    (void) ti;
}

typedef size_t (*lb_load_f)(wctx_t *);

static void
dfs_fifo_dfs (wctx_t *ctx, ref_t seed, lb_load_f load, lb_split_problem_f split)
{
    while (!lb_is_stopped(global->lb)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if (!global_has_color(ctx->state.ref, setV, 0)) {
                if (ctx->state.ref != seed && ctx->state.ref != ctx->seed)
                    ecd_add_state (ctx->cyan, &ctx->state, NULL);
                dfs_stack_enter (ctx->stack);
                increase_level (&ctx->counters);
                permute_trans (ctx->permute, &ctx->state, dfs_fifo_handle, ctx);
                maybe_report (&ctx->counters, "");
                ctx->counters.explored++;
            } else {
                dfs_stack_pop (ctx->stack);
            }
        } else {
            if (dfs_stack_nframes(ctx->stack) == 0)
                break;
            dfs_stack_leave (ctx->stack);
            state_data = dfs_stack_pop (ctx->stack);
            state_info_deserialize_cheap (&ctx->state, state_data);
            ctx->counters.level_cur--;
            if (ctx->state.ref != seed && ctx->state.ref != ctx->seed) {
                ecd_remove_state (ctx->cyan, &ctx->state);
            }
            global_try_color (ctx->state.ref, setV, 0);
        }
        // load balance the FIFO queue (this is a synchronizing affair)
        lb_balance (global->lb, ctx->id, load(ctx)+1, split); //never report 0 load!
    }
    HREassert (lb_is_stopped(global->lb) || fset_count(ctx->cyan) == 0,
               "DFS stack not empty, size: %zu", fset_count(ctx->cyan));
}

static void
dfs_fifo_sbfs (wctx_t *ctx)
{
    size_t              total = 0;
    size_t              out_size, size;
    do {
        while (lb_balance (global->lb, ctx->id, in_load(ctx), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
            if (NULL != state_data) {
                state_info_deserialize_cheap (&ctx->state, state_data);
                //if (!global_has_color(ctx->state.ref, setV, 0)) { //checked in dfs_fifo_dfs
                    dfs_stack_push (ctx->stack, state_data);
                    dfs_fifo_dfs (ctx, ctx->state.ref, in_load, split_sbfs);
                //}
            }
        }
        size = dfs_stack_frame_size (ctx->out_stack);
        HREreduce (HREglobal(), 1, &size, &out_size, SizeT, Sum);
        lb_reinit (global->lb, ctx->id);
        increase_level (&ctx->red);
        if (ctx->id == 0) {
            if (out_size > max_level_size) max_level_size = out_size;
            total += out_size;
            Warning(infoLong, "DFS-FIFO level %zu has %zu states %zu total", ctx->red.level_cur, out_size, total);
        }
        swap (ctx->out_stack, ctx->in_stack);
    } while (out_size > 0 && !lb_is_stopped(global->lb));
}

static void
dfs_fifo_bfs (wctx_t *ctx)
{
    while (lb_balance (global->lb, ctx->id, bfs_load(ctx), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
        if (NULL != state_data) {
            state_info_deserialize_cheap (&ctx->state, state_data);
            //if (!global_has_color(ctx->state.ref, GRED, 0)) { //checked in dfs_fifo_dfs
                dfs_stack_push (ctx->stack, state_data);
                dfs_fifo_dfs (ctx, ctx->state.ref, bfs_load, split_bfs);
            //}
        } else {
            size_t out_size = dfs_stack_frame_size (ctx->out_stack);
            if (out_size > atomic_read(&max_level_size))
                atomic_write(&max_level_size, out_size * W); // over-estimation
            increase_level (&ctx->counters);
            swap (ctx->out_stack, ctx->in_stack);
        }
    }
}

/**
 * OWCTY-MAP Barnat et al.
 *
 * Input-output diagram of OWCTY procedures:
 *
 *      reach(init)         swap          (reset)       reach
 * stack--------> out_stack ----> in_stack ----> stack ------> out_stack
 *                                   ^  [pre>0;pre=0]  [pre++] /| [pre==0]
 *                                    \_______________________/ |pre_elimination
 *                                       pre_elimination        v
 *                                            [pre>0]        stack-\ [--pre==0]
 *                                                             ^___/ elimination
 *
 * Only accepting states are transfered between methods.
 * The predecessor count (pre) is kept in a global array (global->pre). It also
 * includes a flip bit to distinguish new states in each iteration of
 * reachability. By doing a compare and swap on pre + flip, we can grab
 * states with accepting predecessors and reset pre = 0, hence
 * reset van be inlined in reach(ability).
 */

#define                     state_ext32 ((uint32_t *)global->pre)

static inline owcty_pre_t
owcty_pre_read (ref_t ref)
{
    return *(owcty_pre_t *)&atomic_read (&global->pre[ref]);
}

static inline void
owcty_pre_write (ref_t ref, owcty_pre_t val)
{
    uint32_t           *v = (uint32_t *)&val;
    atomic_write (((uint32_t *)global->pre) + ref, *v);
}

static inline int
owcty_pre_cas (ref_t ref, owcty_pre_t now, owcty_pre_t val)
{
    uint32_t           *n = (uint32_t *)&now;
    uint32_t           *v = (uint32_t *)&val;
    return cas (((uint32_t *)global->pre) + ref, *n, *v);
}

static inline uint32_t
owcty_pre_inc (ref_t ref, uint32_t val)
{
    uint32_t            x = add_fetch (&state_ext32[ref], val);
    owcty_pre_t        *y = (owcty_pre_t *)&x;
    return y->count;
}

static inline uint32_t
owcty_pre_count (ref_t ref)
{
    return owcty_pre_read (ref).count;
}

static inline void
owcty_pre_reset (ref_t ref, int bit, int acc)
{
    owcty_pre_t             pre;
    pre.acc = acc;
    pre.bit = bit;
    pre.count = 0;
    owcty_pre_write (ref, pre);
}

static bool
owcty_pre_try_reset (ref_t ref, uint32_t reset_val, int bit, bool check_zero)
{
    owcty_pre_t             pre, orig;
    do {
        orig = owcty_pre_read (ref);
        if (orig.bit == bit || (check_zero && orig.count == 0))
            return false;
        pre.acc = orig.acc;
        pre.bit = bit;
        pre.count = reset_val;
    } while (!owcty_pre_cas(ref, orig, pre));
    return true;
}

/**
 * A reset can be skipped, because the in_stack is also checked during the
 * owcty_reachability call. However, this inlined reset costs more cas operations,
 * while an explicit reset costs an additional synchronization but is not
 * load balanced.
 */
static size_t
owcty_reset (wctx_t *ctx)
{
    state_data_t            data;
    if (ctx->iteration == 0) {
        while ((data = dfs_stack_pop (ctx->in_stack)) && !lb_is_stopped(global->lb)) {
            state_info_deserialize_cheap (&ctx->state, data);
            owcty_pre_reset (ctx->state.ref, ctx->flip, 1);
            dfs_stack_push (ctx->stack, data);
        }
    } else {
        while ((data = dfs_stack_pop (ctx->in_stack)) && !lb_is_stopped(global->lb)) {
            state_info_deserialize_cheap (&ctx->state, data);
            if ( 0 != owcty_pre_count(ctx->state.ref) ) {
                dfs_stack_push (ctx->stack, data);
                owcty_pre_reset (ctx->state.ref, ctx->flip, 1);
            }
        }
    }

    size_t size = dfs_stack_size (ctx->stack);
    HREreduce (HREglobal(), 1, &size, &size, SizeT, Sum);
    return size;
}

static inline void
owcty_map (wctx_t *ctx, state_info_t *successor)
{
    ref_t               map_pred = atomic_read (global->parent_ref+ctx->state.ref);
    if ( GBbuchiIsAccepting(ctx->model, get_state(successor->ref, ctx)) ) {
        if (successor->ref == ctx->state.ref || map_pred == successor->ref) {
            ndfs_report_cycle (ctx, successor);
        }
        size_t              num = successor->ref + 1;
        map_pred = max (num, map_pred);
    }
    atomic_write (global->parent_ref+successor->ref, map_pred);
}

static inline void
owcty_ecd (wctx_t *ctx, state_info_t *successor)
{
    uint32_t acc_level = ecd_get_state (ctx->cyan, successor);
    if (acc_level < ctx->red.level_cur) {
        ndfs_report_cycle (ctx, successor);
    }
}

static ssize_t
owcty_split (void *arg_src, void *arg_tgt, size_t handoff)
{
    wctx_t             *source = arg_src;
    wctx_t             *target = arg_tgt;
    HREassert (target->red.level_cur == 0, "Target accepting level counter is off");
    size_t              in_size = dfs_stack_size (source->stack);
    size_t              todo = min (in_size >> 1, handoff);
    for (size_t i = 0; i < todo; i++) {
        state_data_t        one = dfs_stack_top (source->stack);
        if (!one) { // drop the state as it already explored!!!
            dfs_stack_leave (source->stack);
            source->counters.level_cur--;
            one = dfs_stack_pop (source->stack);
            if (((source->iteration & 1) == 1 || source->iteration == 0) && // only in the initialization / reachability phase
                    Strat_ECD == strategy[1]) {
                state_info_deserialize (&source->state, one, source->store);
                if (GBbuchiIsAccepting(source->model, source->state.data)) {
                    HREassert (source->red.level_cur != 0, "Source accepting level counter is off");
                    source->red.level_cur--;
                }
                ecd_remove_state (source->cyan, &source->state);
            }
        } else {
            dfs_stack_push (target->stack, one);
            dfs_stack_pop (source->stack);
        }
    }
    if ( (source->iteration & 1) == 1 ) { // only in the reachability phase
        size_t              in_size = dfs_stack_size (source->in_stack);
        size_t              todo2 = min (in_size >> 1, handoff);
        for (size_t i = 0; i < todo2; i++) {
            state_data_t        one = dfs_stack_pop (source->in_stack);
            dfs_stack_push (target->in_stack, one);
        }
        todo += todo2;
    }
    return todo;
}

static void
owcty_initialize_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                         int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    if (!seen) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    } else if (strategy[1] == Strat_ECD)
        owcty_ecd (ctx, successor);
    if (strategy[1] == Strat_MAP)
        owcty_map (ctx, successor);
    ctx->counters.trans++;
    (void) ti;
}

static void
owcty_reachability_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                           int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    if (owcty_pre_try_reset(successor->ref, 1, ctx->flip, false)) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    } else {
        if (strategy[1] == Strat_ECD)
            owcty_ecd (ctx, successor);
        uint32_t num = owcty_pre_inc (successor->ref, 1);
        HREassert (num < (1UL<<30)-1, "Overflow in accepting predecessor counter");
    }
    if (strategy[1] == Strat_MAP)
        owcty_map (ctx, successor);
    ctx->counters.trans++;
    (void) ti; (void) seen;
}

static inline size_t
owcty_load (wctx_t *ctx)
{
    return dfs_stack_size(ctx->stack) + dfs_stack_size(ctx->in_stack);
}

/**
 * bit == flip (visited by other workers reachability)
 *    ignore
 * bit != flip
 *    count > 0:    reset(0) & explore
 *    count == 0:   ignore (eliminated)
 */
static size_t
owcty_reachability (wctx_t *ctx)
{
    ctx->counters.visited = 0; // number of states reachable from accepting states
    perm_cb_f handle = ctx->iteration == 0 ? owcty_initialize_handle : owcty_reachability_handle;

    while (lb_balance(global->lb, ctx->id, owcty_load(ctx), owcty_split)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            bool accepting = GBbuchiIsAccepting(ctx->model, ctx->state.data);
            if (strategy[1] == Strat_ECD) {
                ecd_add_state (ctx->cyan, &ctx->state, &ctx->red.level_cur);
                ctx->red.level_cur += accepting;
            }
            if ( accepting )
                dfs_stack_push (ctx->out_stack, state_data);
            permute_trans (ctx->permute, &ctx->state, handle, ctx);
            ctx->counters.visited++;
            ctx->counters.explored++;
            maybe_report (&ctx->counters, "");
        } else {
            if (0 == dfs_stack_nframes (ctx->stack)) {
                while ((state_data = dfs_stack_pop (ctx->in_stack))) {
                    state_info_deserialize_cheap (&ctx->state, state_data);
                    if (owcty_pre_try_reset(ctx->state.ref, 0, ctx->flip,
                                            ctx->iteration > 1)) { // grab & reset
                        dfs_stack_push (ctx->stack, state_data);
                        break;
                    }
                }
                continue;
            }
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            if (strategy[1] == Strat_ECD) {
                state_data = dfs_stack_top (ctx->stack);
                state_info_deserialize (&ctx->state, state_data, ctx->store);
                if (GBbuchiIsAccepting(ctx->model, ctx->state.data)) {
                    HREassert (ctx->red.level_cur != 0, "Accepting level counter is off");
                    ctx->red.level_cur--;
                }
                ecd_remove_state (ctx->cyan, &ctx->state);
            }
            dfs_stack_pop (ctx->stack);
        }
    }

    if (strategy[1] == Strat_ECD && !lb_is_stopped(global->lb))
        HREassert (fset_count(ctx->cyan) == 0 && ctx->red.level_cur == 0, "ECD stack not empty, size: %zu, depth: %zu", fset_count(ctx->cyan), ctx->red.level_cur);
    size_t size[2] = { ctx->counters.visited, dfs_stack_size(ctx->out_stack) };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return ctx->iteration == 0 ? size[1] : size[0];
}

/**
 * We could also avoid this procedure by grabbing states as in the reachability
 * phase, but it may be the case that the entire candidate set is already set
 * to zero, in that case we avoid exploration
 */
static size_t
owcty_elimination_pre (wctx_t *ctx)
{
    state_data_t            data;
    while ((data = dfs_stack_pop (ctx->out_stack)) && !lb_is_stopped(global->lb)) {
        state_info_deserialize_cheap (&ctx->state, data);
        if (0 == owcty_pre_count(ctx->state.ref) ) {
            dfs_stack_push (ctx->stack, data); // to eliminate
        } else {
            dfs_stack_push (ctx->in_stack, data); // to reachability (maybe eliminated)
        }
    }

    size_t size[2] = { dfs_stack_size(ctx->stack), dfs_stack_size(ctx->in_stack) };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return size[0];
}

static void
owcty_elimination_handle (void *arg, state_info_t *successor, transition_info_t *ti,
                          int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    size_t num = owcty_pre_inc (successor->ref, -1);
    HREassert (num < 1ULL<<28); // overflow
    if (0 == num) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
        ctx->counters.visited++;
    }
    ctx->counters.trans++;
    (void) ti; (void) seen;
}

/**
 * returns explored - initial
 */
size_t
owcty_elimination (wctx_t *ctx)
{
    size_t before = ctx->counters.explored + dfs_stack_size(ctx->stack);

    raw_data_t          state_data;
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(ctx->stack), owcty_split)) {
        state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            dfs_stack_enter (ctx->stack);
            increase_level (&ctx->counters);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            permute_trans (ctx->permute, &ctx->state, owcty_elimination_handle, ctx);
            maybe_report (&ctx->counters, "");
            ctx->counters.explored++;
        } else {
            if (0 == dfs_stack_nframes (ctx->stack))
                continue;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            dfs_stack_pop (ctx->stack);
        }
    }

    size_t size[2] = { ctx->counters.explored, before };
    HREreduce (HREglobal(), 2, &size, &size, SizeT, Sum);
    return size[0] - size[1];
}

static void
owcty_do (wctx_t *ctx, size_t *size, size_t (*phase)(wctx_t *ctx), char *name,
          bool reinit_explore)
{
    if (!owcty_do_reset && phase == owcty_reset) return;
    if (reinit_explore) {
        ctx->iteration++;
        lb_reinit (global->lb, ctx->id); // barrier
        if (owcty_ecd_all) {
            permute_free (ctx->permute); // reinitialize random exploration order:
            ctx->permute = permute_create (permutation, ctx->model, W, K, ctx->id);
        }
    }
    if (0 == ctx->id) RTrestartTimer (ctx->timer2);
    size_t new_size = phase(ctx);
    *size = phase == owcty_elimination || phase == owcty_elimination_pre ? *size - new_size : new_size;
    if (0 != ctx->id || lb_is_stopped(global->lb)) return;
    RTstopTimer (ctx->timer2);
    Warning (info, "candidates after %s(%zd):\t%12zu (%4.2f sec)", name,
             (ctx->iteration + 1) / 2, *size, RTrealTime(ctx->timer2));
}

static void
owcty (wctx_t *ctx)
{
    if (strategy[1] == Strat_MAP && 0 == ctx->id && GBbuchiIsAccepting(ctx->model, ctx->initial.data))
        atomic_write (global->parent_ref+ctx->initial.ref, ctx->initial.ref + 1);
    owcty_pre_t             reset = { .bit = 0, .acc = 0, .count = 1 };
    uint32_t               *r32 = (uint32_t *) &reset;
    HREassert (1 == *r32); fetch_add (r32, -1); HREassert (0 == reset.count);
    ctx->timer2 = RTcreateTimer();

    // collect first reachable accepting states
    size_t              size, old_size = 0;
    ctx->iteration = ctx->flip = 0;
    owcty_do (ctx, &size, owcty_reachability,       "initialization", false);
    swap (ctx->in_stack, ctx->out_stack);
    if (!owcty_ecd_all) strategy[1] = Strat_None;

    while (size != 0 && old_size != size && !lb_is_stopped(global->lb)) {
        ctx->flip = 1 - ctx->flip;
        owcty_do (ctx, &size, owcty_reset,          "reset\t",        false);
        owcty_do (ctx, &size, owcty_reachability,   "reachability",   true);
        old_size = size;
        owcty_do (ctx, &size, owcty_elimination_pre,"pre_elimination",false);
        if (size == 0 || size == old_size) break; // early exit
        owcty_do (ctx, &size, owcty_elimination,    "elimination",    true);
    }

    if (0 == ctx->id && !lb_is_stopped(global->lb) && (size == 0 || old_size == size))
        Warning (info, "Accepting cycle %s after %zu iteration(s)!",
                 (size > 0 ? "FOUND" : "NOT found"), (ctx->iteration + 1) / 2);
    HREbarrier(HREglobal()); // print result before (local) statistics
}

/**
 * Multi-core reachability algorithm for timed automata.
 * @inproceedings{eemcs21972,
             month = {September},
            author = {A. E. {Dalsgaard} and A. W. {Laarman} and K. G. {Larsen} and M. C. {Olesen} and J. C. {van de Pol}},
         num_pages = {16},
            series = {Lecture Notes in Computer Science},
            editor = {M. {Jurdzinski} and D. {Nickovic}},
           address = {London},
         publisher = {Springer Verlag},
          location = {London, UK},
              note = {http://eprints.eemcs.utwente.nl/21972/},
         booktitle = {10th International Conference on Formal Modeling and Analysis of Timed Systems, FORMATS 2012, London, UK},
             title = {{Multi-Core Reachability for Timed Automata}},
              year = {2012}
   }
 */

typedef enum ta_set_e {
    TA_WAITING = 0,
    TA_PASSED  = 1,
} ta_set_e_t;

lm_cb_t
ta_covered (void *arg, lattice_t l, lm_status_t status, lm_loc_t loc)
{
    wctx_t         *ctx = (wctx_t*) arg;
    lattice_t lattice = ctx->successor->lattice;
    int *succ_l = (int*)&lattice;
    if (UPDATE == TA_UPDATE_NONE
            ? lattice == l
            : !ctx->subsumes && GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
        ctx->done = 1;
        return LM_CB_STOP; //A l' : (E (s,l)eL : l>=l')=>(A (s,l)eL : l>=l')
    } else if (TA_UPDATE_NONE != UPDATE &&
            (TA_UPDATE_PASSED == UPDATE || TA_WAITING == (ta_set_e_t)status) &&
            GBisCoveredByShort(ctx->model, (int*)&l, succ_l)) {
        ctx->subsumes = 1;
        lm_delete (global->lmap, loc);
        ctx->last = (LM_NULL_LOC == ctx->last ? loc : ctx->last);
        ctx->counters.deletes++;
    }
    ctx->work++;
    return LM_CB_NEXT;
}

lm_cb_t
ta_covered_nb (void *arg, lattice_t l, lm_status_t status, lm_loc_t loc)
{
    wctx_t         *ctx = (wctx_t*) arg;
    lattice_t lattice = ctx->successor->lattice;
    int *succ_l = (int*)&lattice;
    if (UPDATE == TA_UPDATE_NONE
            ? lattice == l
            : GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
        ctx->done = 1;
        return LM_CB_STOP; //A l' : (E (s,l)eL : l>=l')=>(A (s,l)eL : l>=l')
    } else if (TA_UPDATE_NONE != UPDATE &&
            (TA_UPDATE_PASSED == UPDATE || TA_WAITING == (ta_set_e_t)status) &&
            GBisCoveredByShort(ctx->model, (int*)&l, succ_l)) {
        if (LM_NULL_LOC == ctx->added_at) { // replace first waiting, will be added to waiting set
            if (!lm_cas_update (global->lmap, loc, l, status, lattice, (lm_status_t)TA_WAITING)) {
                lattice_t n = lm_get (global->lmap, loc);
                if (n == NULL_LATTICE) // deleted
                    return LM_CB_NEXT;
                lm_status_t s = lm_get_status (global->lmap, loc);
                return ta_covered_nb (arg, n, s, loc); // retry
            } else {
                ctx->added_at = loc;
            }
        } else {                            // delete second etc
            lm_cas_delete (global->lmap, loc, l, status);
        }
        ctx->last = (LM_NULL_LOC == ctx->last ? loc : ctx->last);
        ctx->counters.deletes++;
    }
    ctx->work++;
    return LM_CB_NEXT;
}


static void
ta_queue_state_normal (wctx_t *ctx, state_info_t *successor)
{
    raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL );
    state_info_serialize (successor, stack_loc);
}

static void (*ta_queue_state)(wctx_t *, state_info_t *) = ta_queue_state_normal;

static void
ta_handle (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t         *ctx = (wctx_t*) arg;
    ctx->done = 0;
    ctx->subsumes = 0; // if a successor subsumes one in the set, it cannot be subsumed itself (see invariant paper)
    ctx->work = 0;
    ctx->added_at = LM_NULL_LOC;
    ctx->last = LM_NULL_LOC;
    ctx->successor = successor;
    lm_lock (global->lmap, successor->ref);
    lm_loc_t last = lm_iterate (global->lmap, successor->ref, ta_covered, ctx);
    if (!ctx->done) {
        last = (LM_NULL_LOC == ctx->last ? last : ctx->last);
        successor->loc = lm_insert_from (global->lmap, successor->ref,
                                    successor->lattice, TA_WAITING, &last);
        lm_unlock (global->lmap, successor->ref);
        ctx->counters.inserts++;
        if (0) { // quite costly: flops
            if (ctx->work > 0)
                statistics_unrecord (&ctx->counters.lattice_ratio, ctx->work);
            statistics_record (&ctx->counters.lattice_ratio, ctx->work+1);
        }
        ta_queue_state (ctx, successor);
        ctx->counters.updates += LM_NULL_LOC != ctx->last;
        ctx->counters.visited++;
    } else {
        lm_unlock (global->lmap, successor->ref);
    }
    action_detect (ctx, ti, successor);
    if (EXPECT_FALSE(ctx->lts != NULL)) {
        int             src = ctx->counters.explored;
        int            *tgt = successor->data;
        int             tgt_owner = ref_hash (successor->ref) % W;
        lts_write_edge (ctx->lts, ctx->id, &src, tgt_owner, tgt, ti->labels);
    }
    ctx->counters.trans++;
    (void) seen;
}

static void
ta_handle_nb (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t         *ctx = (wctx_t*) arg;
    ctx->done = 0;
    ctx->subsumes = 0;
    ctx->work = 0;
    ctx->added_at = LM_NULL_LOC;
    ctx->last = LM_NULL_LOC;
    ctx->successor = successor;
    lm_loc_t last = lm_iterate (global->lmap, successor->ref, ta_covered_nb, ctx);
    if (!ctx->done) {
        if (LM_NULL_LOC == ctx->added_at) {
            last = (LM_NULL_LOC == ctx->last ? last : ctx->last);
            successor->loc = lm_insert_from_cas (global->lmap, successor->ref,
                                    successor->lattice, TA_WAITING, &last);
            ctx->counters.inserts++;
        } else {
            successor->loc = ctx->added_at;
        }
        ta_queue_state (ctx, successor);
        ctx->counters.updates += LM_NULL_LOC != ctx->last;
        ctx->counters.visited++;
    }
    ctx->counters.trans++;
    (void) ti; (void) seen;
}

static inline void
ta_explore_state (wctx_t *ctx)
{
    int                 count = 0;
    invariant_detect (ctx, ctx->state.data);
    count = permute_trans (ctx->permute, &ctx->state,
                           NONBLOCKING ? ta_handle_nb : ta_handle, ctx);
    deadlock_detect (ctx, count);
    maybe_report (&ctx->counters, "");
    ctx->counters.explored++;
}

static inline int
grab_waiting (wctx_t *ctx, raw_data_t state_data)
{
    state_info_deserialize (&ctx->state, state_data, ctx->store);
    if ((TA_UPDATE_NONE == UPDATE) && !NONBLOCKING)
        return 1; // we don't need to update the global waiting info
    return lm_cas_update(global->lmap, ctx->state.loc, ctx->state.lattice, TA_WAITING,
                                               ctx->state.lattice, TA_PASSED);
    // lockless! May cause newly created passed state to be deleted by a,
    // waiting set update. However, this behavior is valid since it can be
    // simulated by swapping these two operations in the schedule.
}

void
ta_dfs (wctx_t *ctx)
{
    while (lb_balance(global->lb, ctx->id, dfs_stack_size(ctx->stack), split_dfs)) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            if (grab_waiting(ctx, state_data)) {
                dfs_stack_enter (ctx->stack);
                increase_level (&ctx->counters);
                ta_explore_state (ctx);
            } else {
                dfs_stack_pop (ctx->stack);
            }
        } else {
            if (0 == dfs_stack_size (ctx->stack))
                continue;
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            dfs_stack_pop (ctx->stack);
        }
    }
}

void
ta_bfs (wctx_t *ctx)
{
    while (lb_balance(global->lb, ctx->id, bfs_load(ctx), split_bfs)) {
        raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
        if (NULL != state_data) {
            if (grab_waiting(ctx, state_data)) {
                ta_explore_state (ctx);
            }
        } else {
            swap (ctx->in_stack, ctx->out_stack);
            increase_level (&ctx->counters);
        }
    }
}

void
ta_sbfs (wctx_t *ctx)
{
    size_t              next_level_size, local_next_size;
    do {
        while (lb_balance(global->lb, ctx->id, in_load(ctx), split_sbfs)) {
            raw_data_t          state_data = dfs_stack_pop (ctx->in_stack);
            if (NULL != state_data) {
                if (grab_waiting(ctx, state_data)) {
                    ta_explore_state (ctx);
                }
            }
        }
        local_next_size = dfs_stack_frame_size(ctx->out_stack);
        next_level_size = sbfs_level (ctx, local_next_size);
        lb_reinit (global->lb, ctx->id);
        swap (ctx->out_stack, ctx->in_stack);
        ctx->stack = ctx->out_stack;
    } while (next_level_size > 0 && !lb_is_stopped(global->lb));
}

void
ta_pbfs (wctx_t *ctx)
{
    size_t              count;
    raw_data_t          state_data;
    int                 labels[SL];
    do {
        ctx->red.explored = 0; // count states in next level
        ctx->flip = 1 - ctx->flip;
        for (size_t i = 0; i < W; i++) {
            size_t          current = (i << 1) + ctx->flip;
            while ((state_data = isba_pop_int (ctx->queues[current])) &&
                    !lb_is_stopped (global->lb)) {
                if (grab_waiting(ctx, state_data)) {
                    ta_explore_state (ctx);
                    if (EXPECT_FALSE(ctx->lts && write_state)){
                        if (SL > 0)
                            GBgetStateLabelsAll (ctx->model, ctx->state.data, labels);
                        lts_write_state (ctx->lts, ctx->id, ctx->state.data, labels);
                    }
                }
            }
        }
        count = sbfs_level (ctx, ctx->red.explored);
    } while (count && !lb_is_stopped(global->lb));
}


/**
 * CNDFS for TA
 */

lm_cb_t
ta_cndfs_covered (void *arg, lattice_t l, lm_status_t status, lm_loc_t loc)
{
    wctx_t             *ctx = (wctx_t *) arg;
    lm_status_t         color = (lm_status_t)ctx->subsumes;
    if ( (status & color) && (ctx->successor->lattice == l) ) {
        ctx->done = 1;
        return LM_CB_STOP;
    }
    int *succ_l = (int *) &ctx->successor->lattice;
    if (UPDATE != 0 && (color & status & LM_RED)) {
        if ( GBisCoveredByShort(ctx->model, succ_l, (int*)&l) ) {
            ctx->done = 1;
            ctx->red.updates++; // count (strictly) subsumed by reds
            return LM_CB_STOP;
        }
    }

    return LM_CB_NEXT;
    (void) loc;
}

int
ta_cndfs_subsumed (wctx_t *ctx, state_info_t *state, lm_status_t color)
{
    ctx->subsumes = color;
    ctx->done = 0;
    ctx->successor = state;
    if (NONBLOCKING) {
        lm_iterate (global->lmap, state->ref, ta_cndfs_covered, ctx);
    } else {
        lm_lock (global->lmap, state->ref);
        lm_iterate (global->lmap, state->ref, ta_cndfs_covered, ctx);
        lm_unlock (global->lmap, state->ref);
    }
    return ctx->done;
}

lm_cb_t
ta_cndfs_spray (void *arg, lattice_t l, lm_status_t status, lm_loc_t loc)
{
    wctx_t             *ctx = (wctx_t *) arg;
    lm_status_t         color = (lm_status_t)ctx->subsumes;

    if (UPDATE != 0) {
        int *succ_l = (int *) &ctx->successor->lattice;
        if ( ((status & color) && ctx->successor->lattice == l) ||
             ((status & LM_RED) &&
                    GBisCoveredByShort(ctx->model, succ_l, (int*)&l)) ) {
            ctx->done = 1;
            if (color & LM_BLUE) // only red marking should continue to remove blue states
                return LM_CB_STOP;
        } else if ((color & LM_RED)) { // remove subsumed blue and red states
            if ( GBisCoveredByShort(ctx->model, (int*)&l, succ_l) ) {
                lm_delete (global->lmap, loc);
                ctx->last = (LM_NULL_LOC == ctx->last ? loc : ctx->last);
                ctx->counters.deletes++;
            }
        }
    } else {
        if ( ctx->successor->lattice == l ) {
            if ((status & color) == 0)
                lm_set_status (global->lmap, loc, status | color);
            ctx->done = 1;
            return LM_CB_STOP;
        }
    }

    return LM_CB_NEXT;
}

lm_cb_t
ta_cndfs_spray_nb (void *arg, lattice_t l, lm_status_t status, lm_loc_t loc)
{
    wctx_t             *ctx = (wctx_t *) arg;
    lm_status_t         color = (lm_status_t)ctx->subsumes;
    lattice_t           lattice = ctx->successor->lattice;

    if (UPDATE != 0) {
        int *succ_l = (int *)&lattice;
        if ( ((status & color) && ctx->successor->lattice == l) ||
             ((status & LM_RED) &&
                    GBisCoveredByShort(ctx->model, succ_l, (int*)&l)) ) {
            ctx->done = 1;
            if (color & LM_BLUE) // only red marking should continue to remove blue states
                return LM_CB_STOP;
        } else if (color & LM_RED) { // remove subsumed blue and red states
            if ( GBisCoveredByShort(ctx->model, (int*)&l, succ_l) ) {
                if (!ctx->done) {
                    if (!lm_cas_update (global->lmap, loc, l, status, lattice, color)) {
                        l = lm_get (global->lmap, loc);
                        if (l == NULL_LATTICE) // deleted
                            return LM_CB_NEXT;
                        status = lm_get_status (global->lmap, loc);
                        return ta_cndfs_spray_nb (arg, l, status, loc); // retry
                    } else {
                        ctx->done = 1;
                    }
                } else {                            // delete second etc
                    lm_cas_delete (global->lmap, loc, l, status);
                    ctx->counters.deletes++;
                }
            }
        }
    } else {
        if ( ctx->successor->lattice == l ) {
            if (!lm_cas_update (global->lmap, loc, l, status, lattice, status|color)) {
                l = lm_get (global->lmap, loc);
                if (l == NULL_LATTICE) // deleted
                    return LM_CB_NEXT;
                status = lm_get_status (global->lmap, loc);
                return ta_cndfs_spray_nb (arg, l, status, loc); // retry
            } else {
                ctx->done = 1;
                return LM_CB_STOP;
            }
        }
    }

    return LM_CB_NEXT;
}

static inline int
ta_cndfs_mark (wctx_t *ctx, state_info_t *state, lm_status_t color)
{
    lm_loc_t            last;
    ctx->successor = state;
    ctx->subsumes = color;
    ctx->done = 0;
    ctx->last = LM_NULL_LOC;

    if (NONBLOCKING) {
        last = lm_iterate (global->lmap, state->ref, ta_cndfs_spray_nb, ctx);
        if (!ctx->done) {
            lm_insert_from_cas (global->lmap, state->ref, state->lattice, color, &last);
            ctx->counters.inserts++;
        }
    } else {
        lm_lock (global->lmap, state->ref);
        last = lm_iterate (global->lmap, state->ref, ta_cndfs_spray, ctx);
        if (!ctx->done) {
            last = (LM_NULL_LOC == ctx->last ? last : ctx->last);
            lm_insert_from (global->lmap, state->ref, state->lattice, color, &last);
            ctx->counters.inserts++;
        }
        lm_unlock (global->lmap, state->ref);
    }
    return ctx->done;
}

/* maintain on-stack states */

static inline int
ta_cndfs_has_state (fset_t *table, state_info_t *s, bool add_if_absent)
{
    struct val_s        state;
    state.ref = s->ref;
    state.lattice = s->lattice;
    int res = fset_find (table, NULL, &state, NULL, add_if_absent);
    HREassert (res != FSET_FULL, "Cyan table full");
    return res;
}

static inline void
ta_cndfs_remove_state (fset_t *table, state_info_t *s)
{
    struct val_s        state;
    state.ref = s->ref;
    state.lattice = s->lattice;
    int success = fset_delete (table, NULL, &state);
    HREassert (success, "Could not remove lattice state (%zu,%zu) from set", s->ref, s->lattice);
}

/* maintain a linked list of cyan states with same concrete part on the stack */

static inline void
ta_cndfs_next (wctx_t *ctx, raw_data_t stack_loc, state_info_t *s)
{
    void               *data;
    hash32_t            hash = ref_hash (s->ref);
    int res = fset_find (ctx->cyan2, &hash, &s->ref, &data, true);
    HREassert (res != FSET_FULL, "Cyan2 table full");
    if (res) {
        s->loc = *(lm_loc_t*)data;
    } else {
        s->loc = LM_NULL_LOC;
    }
    state_info_serialize (s, stack_loc);    // write previous pointer to stack
    *(raw_data_t *)data = stack_loc;      // write current pointer to hash map
}

static inline void
ta_cndfs_previous (wctx_t *ctx, state_info_t *s)
{
    hash32_t            hash = ref_hash (s->ref);
    if (s->loc == (lm_loc_t)LM_NULL_LOC) {
        int res = fset_delete (ctx->cyan2, &hash, &s->ref);
        HREassert (res, "state %zu not in Cyan2 table", s->ref);
        return;
    }
    void              *data;
    int res = fset_find (ctx->cyan2, &hash, &s->ref, &data, false);
    HREassert (res, "state %zu not in Cyan2 table", s->ref);
    *(lm_loc_t *)data = s->loc;                  // write new current pointer to hash map
}

static inline bool
ta_cndfs_subsumes_cyan (wctx_t *ctx, state_info_t *s)
{
    void               *data;
    hash32_t            hash = ref_hash (s->ref);
    int res = fset_find (ctx->cyan2, &hash, &s->ref, &data, false);
    if (!res)
        return false;
    state_info_t        state;
    raw_data_t          stack_loc;
    state.loc = *(lm_loc_t*)data;
    size_t              iteration = 0;
    while (state.loc != LM_NULL_LOC) {
        stack_loc = (raw_data_t)state.loc;
        state_info_deserialize_cheap (&state, stack_loc);
        if (state.lattice == s->lattice) {
            return true;
        }
        if (GBisCoveredByShort(ctx->model, (int*)&state.lattice, (int*)&s->lattice)) {
            ctx->red.deletes++;
            return true;
        }
        iteration++;
    }
    return false;
}

static void
ta_cndfs_handle_nonseed_accepting (wctx_t *ctx)
{
    size_t              nonred, accs;
    nonred = accs = dfs_stack_size (ctx->out_stack);
    if (nonred) {
        ctx->red.waits++;
        ctx->counters.rec += accs;
        RTstartTimer (ctx->red.timer);
        while ( nonred && !lb_is_stopped(global->lb) ) {
            nonred = 0;
            for (size_t i = 0; i < accs; i++) {
                raw_data_t state_data = dfs_stack_peek (ctx->out_stack, i);
                state_info_deserialize_cheap (&ctx->state, state_data);
                if (!ta_cndfs_subsumed(ctx, &ctx->state, LM_RED)) {
                    nonred++;
                }
            }
        }
        RTstopTimer (ctx->red.timer);
    }
    for (size_t i = 0; i < accs; i++)
        dfs_stack_pop (ctx->out_stack);
    size_t pre = dfs_stack_size (ctx->in_stack);
    while ( dfs_stack_size(ctx->in_stack) ) {
        raw_data_t state_data = dfs_stack_pop (ctx->in_stack);
        state_info_deserialize_cheap (&ctx->state, state_data);
        ta_cndfs_mark (ctx, &ctx->state, LM_RED);
        //remove_state (ctx->pink, &ctx->state);
    }
    if (pre)
        fset_clear (ctx->pink);
    //HREassert (fset_count(ctx->pink) == 0, "Pink set not empty: %zu", fset_count(ctx->pink));
    if (fset_count(ctx->pink) != 0)
        Warning (info, "Pink set not empty: %zu", fset_count(ctx->pink));
}

static inline bool
ta_cndfs_is_cyan (wctx_t *ctx, state_info_t *s, raw_data_t d, bool add_if_absent)
{
    if (UPDATE == 1) {
        if (add_if_absent) { // BOTH stacks:
            bool result = ta_cndfs_subsumes_cyan (ctx, s);
            result = ta_cndfs_has_state(ctx->cyan, s, add_if_absent);
            if (!result && add_if_absent)
                ta_cndfs_next (ctx, d, &ctx->state);
            return result;
        } else {
            return ta_cndfs_subsumes_cyan (ctx, s);
        }
    } else if (UPDATE == 2) {
        bool result = ta_cndfs_subsumes_cyan (ctx, s);
        if (!result && add_if_absent)
            ta_cndfs_next (ctx, d, &ctx->state);
        return result;
    } else {
        return ta_cndfs_has_state(ctx->cyan, s, add_if_absent);
    }
}

static void
ta_cndfs_handle_red (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    /* Find cycle back to the seed */
    if ( ta_cndfs_is_cyan(ctx, successor, NULL, false) )
        ndfs_report_cycle (ctx, successor);
    if ( !ta_cndfs_has_state(ctx->pink, successor, false) //&&
         /*!ta_cndfs_subsumed(ctx, successor, LM_RED)*/ ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static void
ta_cndfs_handle_blue (void *arg, state_info_t *successor, transition_info_t *ti, int seen)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_data_t succ_data = get_state (successor->ref, ctx);
    int acc = GBbuchiIsAccepting(ctx->model, succ_data);
    ctx->red.visited += ~seen & acc;
    int cyan = ta_cndfs_is_cyan (ctx, successor, NULL, false);
    if ( ecd && cyan && (GBbuchiIsAccepting(ctx->model, ctx->state.data) || acc) ) {
        /* Found cycle in blue search */
        ndfs_report_cycle (ctx, successor);
    }
    if ( all_red || !cyan ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->stack, NULL);
        state_info_serialize (successor, stack_loc);
    }
    (void) ti; (void) seen;
}

static inline void
ta_cndfs_explore_state_red (wctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation_red;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ta_cndfs_handle_red, ctx);
    cnt->explored++;
    maybe_report (cnt, "[R] ");
}

static inline void
ta_cndfs_explore_state_blue (wctx_t *ctx, counter_t *cnt)
{
    dfs_stack_enter (ctx->stack);
    increase_level (cnt);
    ctx->permute->permutation = permutation;
    cnt->trans += permute_trans (ctx->permute, &ctx->state, ta_cndfs_handle_blue, ctx);
    cnt->explored++;
    maybe_report (cnt, "[B] ");
}

/* ENDFS dfs_red */
static void
ta_cndfs_red (wctx_t *ctx, ref_t seed, lattice_t l_seed)
{
    size_t              seed_level = dfs_stack_nframes (ctx->stack);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( !ta_cndfs_subsumed(ctx, &ctx->state, LM_RED) &&
                 !ta_cndfs_has_state(ctx->pink, &ctx->state, true) ) {
                dfs_stack_push (ctx->in_stack, state_data);
                if ( ctx->state.ref != seed && ctx->state.lattice != l_seed &&
                     GBbuchiIsAccepting(ctx->model, ctx->state.data) )
                    dfs_stack_push (ctx->out_stack, state_data);
                ta_cndfs_explore_state_red (ctx, &ctx->red);
            } else {
                if (seed_level == dfs_stack_nframes (ctx->stack))
                    break;
                dfs_stack_pop (ctx->stack);
            }
        } else { // backtrack
            dfs_stack_leave (ctx->stack);
            HREassert (ctx->red.level_cur != 0);
            ctx->red.level_cur--;

            /* exit search if backtrack hits seed, leave stack the way it was */
            if (seed_level == dfs_stack_nframes(ctx->stack))
                break;
            dfs_stack_pop (ctx->stack);
        }
    }
}

/* ENDFS dfs_blue */
void
ta_cndfs_blue (wctx_t *ctx)
{
    lm_status_t BLUE_CHECK = (UPDATE != 0 ? LM_BOTH : LM_BLUE);
    while ( !lb_is_stopped(global->lb) ) {
        raw_data_t          state_data = dfs_stack_top (ctx->stack);
        if (NULL != state_data) {
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if ( !ta_cndfs_subsumed(ctx, &ctx->state, BLUE_CHECK) &&
                 !ta_cndfs_is_cyan(ctx, &ctx->state, state_data, true) ) {
                if (all_red)
                    bitvector_set (&ctx->all_red, ctx->counters.level_cur);
                ta_cndfs_explore_state_blue (ctx, &ctx->counters);
            } else {
                if ( all_red && ctx->counters.level_cur != 0 &&
                     !ta_cndfs_subsumed(ctx, &ctx->state, LM_RED) )
                    bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
                dfs_stack_pop (ctx->stack);
            }
        } else { // backtrack
            if (0 == dfs_stack_nframes(ctx->stack)) {
                if (fset_count(ctx->cyan) != 0)
                    Warning (info, "Cyan set not empty: %zu", fset_count(ctx->cyan));
                break;
            }
            dfs_stack_leave (ctx->stack);
            ctx->counters.level_cur--;
            /* call red DFS for accepting states */
            state_data = dfs_stack_top (ctx->stack);
            state_info_deserialize (&ctx->state, state_data, ctx->store);
            if (UPDATE == 1) {
                ta_cndfs_previous (ctx, &ctx->state);
                ta_cndfs_remove_state (ctx->cyan, &ctx->state);
            } else if (UPDATE == 2) {
                ta_cndfs_previous (ctx, &ctx->state);
            } else {
                ta_cndfs_remove_state (ctx->cyan, &ctx->state);
            }
            /* Mark state BLUE on backtrack */
            ta_cndfs_mark (ctx, &ctx->state, LM_BLUE);
            if ( all_red && bitvector_is_set(&ctx->all_red, ctx->counters.level_cur) ) {
                /* all successors are red */
                //permute_trans (ctx->permute, &ctx->state, check, ctx);
                int red = ta_cndfs_mark (ctx, &ctx->state, LM_RED);
                ctx->counters.allred += red;
                ctx->red.allred += 1 - red;
            } else if ( GBbuchiIsAccepting(ctx->model, ctx->state.data) ) {
                ta_cndfs_red (ctx, ctx->state.ref, ctx->state.lattice);
                ta_cndfs_handle_nonseed_accepting (ctx);
            } else if (all_red && ctx->counters.level_cur > 0 &&
                       !ta_cndfs_subsumed(ctx, &ctx->state, LM_RED) ) {
                /* unset the all-red flag (only for non-initial nodes) */
                bitvector_unset (&ctx->all_red, ctx->counters.level_cur - 1);
            }
            dfs_stack_pop (ctx->stack);
        }
    }
}

/* explore is started for each thread (worker) */
static void
explore (wctx_t *ctx)
{
    transition_info_t       ti = GB_NO_TRANSITION;
    state_data_t            initial = RTmalloc (sizeof(int[N]));
    GBgetInitialState (ctx->model, initial);
    state_info_initialize (&ctx->initial, initial, &ti, &ctx->state, ctx);
    if ( Strat_DFSFIFO & strategy[0] ) {
        raw_data_t stack_loc = dfs_stack_push (ctx->in_stack, NULL);
        state_info_serialize (&ctx->initial, stack_loc);
        int acc = GBbuchiIsAccepting (ctx->model, initial);
        ctx->seed = acc ? (ref_t)-1 : ctx->initial.ref;
        ctx->red.visited += acc;
    } else if ( Strat_OWCTY & strategy[0] ) {
        if (0 == ctx->id) owcty_initialize_handle (ctx, &ctx->initial, &ti, 0);
    } else if ( Strat_LTL & strategy[0] ) {
        if ( Strat_TA & strategy[0] ) {
            ta_cndfs_handle_blue (ctx, &ctx->initial, &ti, 0);
        } else {
            ndfs_blue_handle (ctx, &ctx->initial, &ti, 0);
        }
    } else if (0 == ctx->id) { // only w1 receives load, as it is propagated later
        if ( Strat_TA & strategy[0] ) {
            if ( Strat_PBFS & strategy[0] )
                ta_queue_state = pbfs_queue_state;
            ta_handle (ctx, &ctx->initial, &ti, 0);
        } else if ( Strat_PBFS & strategy[0] ) {
            if (ctx->lts != NULL) {
                int             src_owner = ref_hash(ctx->initial.ref) % W;
                lts_write_init (ctx->lts, src_owner, initial);
            }
            pbfs_queue_state (ctx, &ctx->initial);
        } else {
            reach_handle (ctx, &ctx->initial, &ti, 0);
        }
    }
    ctx->counters.trans = 0; //reset trans count
    ctx->timer = RTcreateTimer ();

    HREbarrier (HREglobal());
    RTstartTimer (ctx->timer);
    switch (strategy[0]) {
    case Strat_TA_PBFS: ta_queue_state = pbfs_queue_state;
                        ta_pbfs (ctx); break;
    case Strat_TA_SBFS: ta_sbfs (ctx); break;
    case Strat_TA_BFS:  ta_bfs (ctx); break;
    case Strat_TA_DFS:  ta_dfs (ctx); break;
    case Strat_TA_CNDFS:ta_cndfs_blue (ctx); break;
    case Strat_SBFS:    sbfs (ctx); break;
    case Strat_PBFS:    pbfs (ctx); break;
    case Strat_BFS:     bfs (ctx); break;
    case Strat_DFS:     if (UseGreyBox == call_mode)        dfs_grey (ctx);
                        else if (proviso == Proviso_Stack)  dfs_proviso (ctx);
                        else                                dfs (ctx);
                        break;
    case Strat_NDFS:    ndfs_blue (ctx); break;
    case Strat_LNDFS:   lndfs_blue (ctx); break;
    case Strat_CNDFS:
    case Strat_ENDFS:   endfs_blue (ctx); break;
    case Strat_OWCTY:   owcty (ctx); break;
    case Strat_DFSFIFO: if (strict_dfsfifo) dfs_fifo_sbfs (ctx); // default
                        else                dfs_fifo_bfs (ctx); break;
    default: Abort ("Strategy is unknown or incompatible with the current front-end (%d).", strategy[0]);
    }
    RTstopTimer (ctx->timer);
    ctx->counters.runtime = RTrealTime (ctx->timer);
    for (size_t i = 0; i < W && !lb_is_stopped(global->lb); i++) {
        if (i == ctx->id) print_thread_statistics (ctx);
        HREbarrier (HREglobal());
    }
    RTfree (initial);
}

int
main (int argc, char *argv[])
{
    /* Init structures */
    HREinitBegin (argv[0]);
    HREaddOptions (options,"Perform a parallel reachability analysis of <model>\n\nOptions");
    lts_lib_setup();
#if SPEC_MT_SAFE == 1
    HREenableThreads (1);
#else
    HREenableFork (1); // enable multi-process env for mCRL/mCrl2 and PBES
#endif
    HREinitStart (&argc,&argv,1,2,files,"<model> [lts]");      // spawns threads!

    wctx_t                 *ctx;
    prelocal_global_init ();
    ctx = local_init ();
    postlocal_global_init (ctx);

    explore (ctx);

    reduce_and_print_result (ctx);

    deinit_all (ctx);

    HREabort (global->exit_status);
}
