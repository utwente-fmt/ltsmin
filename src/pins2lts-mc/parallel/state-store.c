/**
 * Next-state permutator
 *
 * Integrated state storage f the multi-core tool to increase efficiency
 */

#include <hre/config.h>

#include <math.h>
#include <stdlib.h>

#include <mc-lib/treedbs-ll.h>
#include <mc-lib/dbs-ll.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/timed.h> // LATTICE_BLOCK_SIZE
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/zobrist.h>

db_type_t        db_type = TreeTable;
find_or_put_f    find_or_put;
dbs_get_f        get;
dbs_stats_f      statistics;
hash64_f         hasher;

char            *state_repr = "tree";
char            *table_size = "20%";
int              dbs_size = 0;
size_t           ratio = 2;
int              refs = 1;
int              ZOBRIST = 0;
int              count_bits = 0;
int              global_bits = 0;
int              local_bits = 0;
int              indexing;

si_map_entry db_types[] = {
   {"table",   HashTable},
   {"tree",    TreeTable},
   {"cleary-tree", ClearyTree},
   {NULL, 0}
};

void
state_store_popt (poptContext con, enum poptCallbackReason reason,
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
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort ("unexpected call to state_store_popt");
    (void)con; (void)opt; (void)arg; (void)data;
}

struct poptOption state_store_options[] = {
    {NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION,
     (void *)state_store_popt, 0, NULL, NULL},
    {"state", 0, POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &state_repr, 0,
     "select the data structure for storing states. Beware for Cleary tree: "
     "size <= 28 + 2 * ratio.", "<tree|table|cleary-tree>"},
    {"size", 's', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &table_size, 0,
     "log2 size of the state store or maximum % of memory to use", NULL},
    {"ratio", 0, POPT_ARG_LONGLONG | POPT_ARGFLAG_SHOW_DEFAULT, &ratio, 0,
     "log2 tree root to leaf ratio", "<int>"},
    {"noref", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &refs, 0,
     "store full states on the stack/queue instead of references (faster)", NULL},
    {"zobrist", 'z', POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &ZOBRIST, 0,
     "log2 size of zobrist random table (6 or 8 is good enough; 0 is no zobrist)", NULL},
    POPT_TABLEEND
};

char *
state_store_full_msg (int dbs_ret_value)
{
    return (DB_FULL == dbs_ret_value ? "hash table" :
            (DB_ROOTS_FULL == dbs_ret_value ? "tree roots table" :
                                              "tree leafs table"));
}

void *
get_state (ref_t ref, void *arg)
{
    wctx_t             *ctx = (wctx_t *) arg;
    state_data_t        state = get (global->dbs, ref, ctx->store2);
    return Tree & db_type ? TreeDBSLLdata(global->dbs, state) : state;
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

static void
init_color_bits () //TODO: ditsibute
{
    int i = 0;
    while (Strat_None != strategy[i] && i < MAX_STRATEGIES) {
        global_bits += num_global_bits (strategy[i]);
        local_bits += (~Strat_DFSFIFO & Strat_LTL & strategy[i++] ? 2 : 0);
    }
    count_bits = (Strat_LNDFS == strategy[i - 1] ? ceil (log2 (W + 1)) : 0);
}

void
state_store_static_init ()
{
    // Color bits
    init_color_bits ();

    // Determine database size
    char* end;
    dbs_size = strtol (table_size, &end, 10);
    if (dbs_size == 0)
        Abort("Not a valid table size: -s %s", table_size);
    if (*end == '%') {
        size_t el_size = (db_type != HashTable ? 3 : D) * SLOT_SIZE; // over estimation for cleary
        size_t map_el_size = (Strat_TA & strategy[0] ? sizeof (lattice_t) : 0);
        size_t db_el_size = (RTmemSize () / 100 * dbs_size) / (el_size + map_el_size);
        dbs_size = (int)log2 (db_el_size);
        dbs_size = dbs_size > DB_SIZE_MAX ? DB_SIZE_MAX : dbs_size;
    }

    // Wrap functions
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
        setup_colors (count_bits, (dbs_get_sat_f)DBSLLget_sat_bit,
                     (dbs_try_set_sat_f) DBSLLtry_set_sat_bit,
                     (dbs_inc_sat_bits_f)DBSLLinc_sat_bits,
                     (dbs_dec_sat_bits_f)DBSLLdec_sat_bits,
                     (dbs_get_sat_bits_f)DBSLLget_sat_bits);
        break;
    case ClearyTree:
        if (indexing) Abort ("Cleary tree not supported in combination with "
                              "error trails or the MCNDFS algorithms.");
    case TreeTable:
        if (ZOBRIST)
            Abort ("Zobrist and treedbs is not implemented");
        statistics = (dbs_stats_f) TreeDBSLLstats;
        get = (dbs_get_f) TreeDBSLLget;
        find_or_put = find_or_put_tree;
        setup_colors (count_bits, (dbs_get_sat_f)TreeDBSLLget_sat_bit,
                     (dbs_try_set_sat_f) TreeDBSLLtry_set_sat_bit,
                     (dbs_inc_sat_bits_f)TreeDBSLLinc_sat_bits,
                     (dbs_dec_sat_bits_f)TreeDBSLLdec_sat_bits,
                     (dbs_get_sat_bits_f)TreeDBSLLget_sat_bits);
        break;
    case Tree: default: Abort ("Unknown state storage type: %d.", db_type);
    }
}

void
state_store_init (model_t model, bool timed)
{
    matrix_t           *m = GBgetDMInfo (model);
    size_t              bits = global_bits + count_bits;

    if (db_type == HashTable) {
        if (ZOBRIST)
            global->zobrist = zobrist_create (D, ZOBRIST, m);
        global->dbs = DBSLLcreate_sized (D, dbs_size, hasher, bits);
    } else {
        global->dbs = TreeDBSLLcreate_dm (D, dbs_size, ratio, m, bits,
                                         db_type == ClearyTree, indexing);
    }
    if (timed) {
        global->lmap = lm_create (W, 1UL<<dbs_size, LATTICE_BLOCK_SIZE);
    }
}
