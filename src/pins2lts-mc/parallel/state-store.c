/**
 * Integrated state storage for the multi-core tool to increase efficiency
 */

#include <hre/config.h>

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include <mc-lib/treedbs-ll.h>
#include <mc-lib/dbs-ll.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/timed.h> // LATTICE_BLOCK_SIZE
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/stream-serializer.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/fast_hash.h>
#include <util-lib/zobrist.h>

db_type_t       db_type = TreeTable;

char            *state_repr = "tree";
char            *table_size = "20%";
int              dbs_size = 0;
size_t           ratio = 2;
int              refs = 1;
int              ZOBRIST = 0;
int              indexing;

si_map_entry db_types[] = {
   {"table",   HashTable},
   {"tree",    TreeTable},
   {"cleary-tree", ClearyTree},
   {NULL, 0}
};

struct state_store_s {
    void               *dbs;            // Hash table/Tree table/Cleary tree
    lm_t               *lmap;           // Lattice map (Strat_TA)
    zobrist_t           zobrist;        // Zobrist hasher
    //dbs_get_f           get;
    //hash64_f            hasher;
    dbs_stats_f         statistics;
    dbs_get_sat_f       get_sat_bit;
    dbs_try_set_sat_f   try_set_sat_bit;
    dbs_try_set_sats_f  try_set_sat_bits;
    dbs_inc_sat_bits_f  inc_sat_bits;
    dbs_dec_sat_bits_f  dec_sat_bits;
    dbs_get_sat_bits_f  get_sat_bits;
    int                 count_bits;
    int                 global_bits;
    int                 local_bits;
    size_t              count_mask;
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
    {"no-ref", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &refs, 0,
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

static hash64_t
z_rehash (const void *v, int b, hash64_t seed)
{
    return zobrist_rehash (global->store->zobrist, seed);
    (void)b; (void)v;
}

void
state_store_static_init ()
{
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
}

state_store_t *
state_store_init (model_t model, bool timed)
{
    state_store_t      *store = RTmallocZero (sizeof(state_store_t));
    matrix_t           *m = GBgetDMInfo (model);

    int i = 0;
    store->global_bits = 0;
    store->local_bits = 0;
    while (Strat_None != strategy[i] && i < MAX_STRATEGIES) {
        store->global_bits += num_global_bits (strategy[i]);
        store->local_bits += (~Strat_DFSFIFO & Strat_LTL & ~Strat_UFSCC & ~Strat_CNDFS & strategy[i] ? 2 : 0);
        i++;
    }
    store->count_bits = (Strat_LNDFS == strategy[i - 1] ? ceil (log2 (W + 1)) :
            (Strat_CNDFS == strategy[i - 1] && PINS_POR ? 2 : 0) );
    store->count_mask = (1<<store->count_bits) - 1;
    size_t              bits = store->global_bits + store->count_bits;

    // Wrap functions
    indexing = NULL != trc_output || ((Strat_TA | Strat_LTL) & strategy[0]);
    switch (db_type) {
    case HashTable:
        store->statistics = (dbs_stats_f) DBSLLstats;
        //store->get = (dbs_get_f) DBSLLget;
        store->get_sat_bit     = (dbs_get_sat_f) DBSLLget_sat_bit;
        store->try_set_sat_bit = (dbs_try_set_sat_f)  DBSLLtry_set_sat_bit;
        store->try_set_sat_bits= (dbs_try_set_sats_f) DBSLLtry_set_sat_bits;
        store->inc_sat_bits    = (dbs_inc_sat_bits_f) DBSLLinc_sat_bits;
        store->dec_sat_bits    = (dbs_dec_sat_bits_f) DBSLLdec_sat_bits;
        store->get_sat_bits    = (dbs_get_sat_bits_f) DBSLLget_sat_bits;
        if (ZOBRIST) {
            store->dbs = DBSLLcreate_sized (D, dbs_size, (hash64_f)z_rehash, bits);
            //store->hasher = (hash64_f) z_rehash;
        } else {
            store->dbs = DBSLLcreate_sized (D, dbs_size, (hash64_f)MurmurHash64, bits);
            //store->hasher = (hash64_f) MurmurHash64;
        }
        break;
    case ClearyTree:
        if (indexing) Abort ("Cleary tree not supported in combination with "
                              "error trails or the MCNDFS algorithms.");
    case TreeTable:
        if (ZOBRIST)
            Abort ("Zobrist and treedbs is not implemented");
        store->statistics = (dbs_stats_f) TreeDBSLLstats;
        //store->get = (dbs_get_f) TreeDBSLLget;
        store->get_sat_bit     = (dbs_get_sat_f)      TreeDBSLLget_sat_bit;
        store->try_set_sat_bit = (dbs_try_set_sat_f)  TreeDBSLLtry_set_sat_bit;
        store->try_set_sat_bits= (dbs_try_set_sats_f) TreeDBSLLtry_set_sat_bits;
        store->inc_sat_bits    = (dbs_inc_sat_bits_f) TreeDBSLLinc_sat_bits;
        store->dec_sat_bits    = (dbs_dec_sat_bits_f) TreeDBSLLdec_sat_bits;
        store->get_sat_bits    = (dbs_get_sat_bits_f) TreeDBSLLget_sat_bits;

        store->dbs = TreeDBSLLcreate_dm (D, dbs_size, ratio, m, bits,
                                         db_type == ClearyTree, indexing);

        break;
    case Tree: default: Abort ("Unknown state storage type: %d.", db_type);
    }

    if (ZOBRIST) {
        store->zobrist = zobrist_create (D, ZOBRIST, m);
    }

    if (timed) {
        store->lmap = lm_create (W, 1UL<<dbs_size, LATTICE_BLOCK_SIZE);
    }

    return store;
}

void
state_store_deinit (state_store_t *store)
{
    if (HashTable & db_type) {
        DBSLLfree (store->dbs);
    } else {
        TreeDBSLLfree (store->dbs);
    }
    if (store->lmap != NULL) {
        lm_free (store->lmap);
    }
    if (store->zobrist != NULL) {
        zobrist_free(store->zobrist);
    }
    RTfree (store);
}

void
state_store_print (state_store_t *store)
{
    Warning (info, "Global bits: %d, count bits: %d, local bits: %d",
             store->global_bits, store->count_bits, store->local_bits);
}

size_t
state_store_local_bits (state_store_t *store)
{
    return store->local_bits;
}

stats_t *
state_store_stats (state_store_t *store)
{
    return global->store->statistics(store->dbs);
}

lm_t *
state_store_lmap (state_store_t *store)
{
    return store->lmap;
}

int
state_store_has_color (ref_t ref, global_color_t color, int rec_bits)
{
    return global->store->get_sat_bit (global->store->dbs, ref,
                        rec_bits + global->store->count_bits + color.g);
}

int //RED and BLUE are independent
state_store_try_color (ref_t ref, global_color_t color, int rec_bits)
{
    return global->store->try_set_sat_bit (global->store->dbs, ref,
                            rec_bits + global->store->count_bits + color.g);
}

uint32_t
state_store_inc_wip (ref_t ref)
{
    return global->store->inc_sat_bits (global->store->dbs, ref)
            & global->store->count_mask;
}

uint32_t
state_store_dec_wip (ref_t ref)
{
    return global->store->dec_sat_bits (global->store->dbs, ref)
            & global->store->count_mask;
}

uint32_t
state_store_get_wip (ref_t ref)
{
    return global->store->get_sat_bits (global->store->dbs, ref)
            & global->store->count_mask;
}

uint32_t
state_store_get_colors (ref_t ref)
{
    return global->store->get_sat_bits (global->store->dbs, ref)
            >> global->store->count_bits;
}

int
state_store_try_set_counters (ref_t ref, size_t bits,
                              uint64_t old_val, uint64_t new_val)
{
    return global->store->try_set_sat_bits (global->store->dbs, ref, bits, 0,
                                            old_val, new_val);
}

int
state_store_try_set_colors (ref_t ref, size_t bits,
                            uint64_t old_val, uint64_t new_val)
{
    return global->store->try_set_sat_bits (global->store->dbs, ref, bits,
                                            global->store->count_bits,
                                            old_val, new_val);
}

struct store_s {
    state_data_t        tmp; // temporary state storage for tree
    streamer_t         *serializer; // stream of deserializer of PINS and state get
    streamer_t         *first; // stream of first state get
    hash64_t            hash64;
    state_data_t        data;
    tree_t              tree;
    ref_t              *ref; // location to serialize the references to
};

typedef struct init_ctx_s {
    store_t            *predecessor;
    transition_info_t  *ti;      // transition info
    int                 seen;
} init_ctx_t;

static void
table_new (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    init_ctx_t         *init_ctx = (init_ctx_t *) ctx;
    store->data = data;
    init_ctx->seen = DBSLLlookup_hash (global->store->dbs, data, store->ref, NULL);
}

static void
z_new (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    init_ctx_t         *init_ctx = (init_ctx_t *) ctx;
    store->hash64 = zobrist_hash (global->store->zobrist, data,
                                  store_state(init_ctx->predecessor),
                                  init_ctx->predecessor->hash64);
    store->data = data;
    init_ctx->seen = DBSLLlookup_hash (global->store->dbs, data, store->ref,
                                       &store->hash64);
}

static void
z_first (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    init_ctx_t         *init_ctx = (init_ctx_t *) ctx;
    store->hash64 = zobrist_hash (global->store->zobrist, data, NULL, 0);
    store->data = data;
    init_ctx->seen = DBSLLlookup_hash (global->store->dbs, data, store->ref,
                                       &store->hash64);
}

static void
tree_first (void *ctx, void *ptr, raw_data_t data)
{
    init_ctx_t         *init_ctx = (init_ctx_t *) ctx;
    store_t            *store = (store_t *) ptr;
    init_ctx->seen = TreeDBSLLlookup_dm (global->store->dbs, data, NULL,
                                         store->tmp, GB_UNKNOWN_GROUP);
    store->tree = store->tmp;
    store->data = TreeDBSLLdata (global->store->dbs, store->tree);
    store->ref[0] = TreeDBSLLindex (global->store->dbs, store->tree);
}

static void
tree_new (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    init_ctx_t         *init_ctx = (init_ctx_t *) ctx;
    init_ctx->seen = TreeDBSLLlookup_dm (global->store->dbs, data,
                                         store_tree(init_ctx->predecessor),
                                         store->tmp, init_ctx->ti->group);
    store->tree = store->tmp;
    store->data = TreeDBSLLdata (global->store->dbs, store->tree);
    store->ref[0] = TreeDBSLLindex (global->store->dbs, store->tree);
}

static void
table_get (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    if (store->data == NULL) {
       store->data = DBSLLget (global->store->dbs, store->ref[0], store->tmp);
    }
    (void) data;
    (void) ctx;
}

static void
tree_get (void *ctx, void *ptr, raw_data_t data)
{
    store_t            *store = (store_t *) ptr;
    if (store->tree == NULL) {
        store->tree = TreeDBSLLget (global->store->dbs, store->ref[0], store->tmp);
    }
    if (store->data == NULL) {
        store->data = TreeDBSLLdata (global->store->dbs, store->tree);
    }
    (void) data;
    (void) ctx;
}

static void
ta_ser (void *ctx, void *ptr, raw_data_t data)
{
    lattice_t           lattice = *(lattice_t *)ptr;
    HREassert (lattice != LM_NULL_LATTICE);
    ((lattice_t *)data)[0] = lattice;
    (void) ctx;
}

static void
ta_des (void *ctx, void *ptr, raw_data_t data)
{
    lattice_t          *lattice = (lattice_t *)ptr;
    lattice[0] = ((lattice_t *)data)[0];
    HREassert (lattice[0] != LM_NULL_LATTICE);
    (void) ctx;
}

static void
z_ser (void *ctx, void *ptr, raw_data_t data)
{
    ((hash64_t *) data)[0] = ((hash64_t *) ptr)[0];
    (void) ctx;
}

void
z_des (void *ctx, void *ptr, raw_data_t data)
{
    ((hash64_t *) ptr)[0] = ((hash64_t *) data)[0];
    (void) ctx;
}

/**
 * SERIALIZE = get state (only state data, not a full PINS state with lattice)
 * DESERIALIZE = new state (from PINS)
 *
 * The get_state stream does not actually serialize a state, rather it lazily
 * fills in the information in state_info_t
 *
 */
store_t *
store_create (state_info_t *si)
{
    store_t            *store = RTmallocZero (sizeof(store_t));
    store->serializer = streamer_create ();
    store->first = streamer_create ();
    store->tmp = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2 +
                                   sizeof(lattice_t));
    store->ref =  &si->ref;
    store->data = NULL;
    store->tree = NULL;

    size_t              slat = sizeof(lattice_t*);
    size_t              stab = SLOT_SIZE * D;
    switch (db_type) {
    case HashTable:
        if (ZOBRIST) {
            streamer_add (store->serializer, table_get, z_new, stab, store);
            streamer_add (store->first, NULL, z_first, stab, store);

            // also add the hash serialization to the search stack!
            state_info_add (si, z_ser, z_des, sizeof(hash64_t), &store->hash64);
        } else {
            streamer_add (store->serializer, table_get, table_new, stab, store);
            streamer_add (store->first, NULL, table_new, stab, store);
        }
        break;
    case ClearyTree:
    case TreeTable:
        streamer_add (store->serializer, tree_get, tree_new, stab, store);
        streamer_add (store->first, NULL, tree_first, stab, store);
        break;
    default: Abort ("State store not implemented");
    }

    if (strategy[0] & Strat_TA) {
        streamer_add (store->serializer, NULL, ta_des, slat, &si->lattice);
        streamer_add (store->first, NULL, ta_des, slat, &si->lattice);

        // also add the lattice serialization to the search stack!
        state_info_add (si, ta_ser, ta_des, slat, &si->lattice);
    }

    return store;
}

void
store_clear (store_t* store)
{
    store->tree = NULL;
    store->data = NULL;
}

int
store_new_state (store_t *store, state_data_t data,
                 transition_info_t *ti, store_t *src)
{
    init_ctx_t          ctx;
    ctx.ti = ti;
    ctx.predecessor = src;
    store_clear (store);
    streamer_walk (store->serializer, &ctx, (raw_data_t) data, DESERIALIZE);
    Debug ("New state %"PRIu32" --> %zu (H:%"PRIu64") from group %d",
           MurmurHash32 (data, D*4, 0), store->ref[0], store->hash64, ti->group);
    HREassert (store->ref[0] != DUMMY_IDX);
    return ctx.seen;
}

int
store_first (store_t *store, state_data_t data)
{
    init_ctx_t          ctx;
    store_clear (store);
    streamer_walk (store->first, &ctx, (raw_data_t) data, DESERIALIZE);
    HREassert (store->ref[0] != DUMMY_IDX);
    Debug ("First state %"PRIu32" --> %zu (H:%"PRIu64")",
           MurmurHash32 (data, D*4, 0), store->ref[0], store->hash64);
    return ctx.seen;
}

void
store_set_state (store_t *store, state_data_t state)
{
    store->data = state;
}

state_data_t
store_state (store_t *store)
{
    init_ctx_t          ctx;
    streamer_walk (store->serializer, &ctx, NULL, SERIALIZE); // get
    Debug ("Get state %zu --> %"PRIu32" (H:%"PRIu64")", store->ref[0],
           MurmurHash32 (store->data, D*4, 0), store->hash64);
    return store->data;
}

tree_t
store_tree (store_t *store)
{
    HREassert (db_type & Tree);
    store_state (store);
    return store->tree;
}

void
store_tree_index (store_t *store, tree_t tree)
{
    store->ref[0] = TreeDBSLLindex (global->store->dbs, tree);
    store->data = TreeDBSLLdata (global->store->dbs, tree);
    store->tree = tree;
}
