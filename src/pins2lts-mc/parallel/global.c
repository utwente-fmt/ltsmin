/*
 * permutor.c
 *
 *  Created on: Sep 20, 2013
 *      Author: laarman
 */

#include <hre/config.h>

#include <math.h>
#include <popt.h>

#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/lb.h>
#include <pins-lib/pins.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>

static const size_t     THRESHOLD = 10000;

global_t        *global = NULL;

int              act_index = -1;
int              act_type = -1;
int              act_label = -1;

//TODO: rename/eliminate these variables
size_t           W = -1;
size_t           G = 1;
size_t           H = 1000;
size_t           D;                  // size of state in explicit state DB
size_t           N;                  // size of entire state
size_t           K;                  // number of groups
size_t           SL;                 // number of state labels
size_t           EL;                 // number of edge labels
size_t           max_level_size = 0; // TODO, max BFS level

void
global_alloc  (bool pthreads)
{
    RTswitchAlloc (!pthreads);

    global = RTmallocZero (sizeof(global_t));

    global->pthreads = pthreads;

    global->exit_status = LTSMIN_EXIT_SUCCESS;


    GBloadFileShared (NULL, files[0]); // NOTE: no model argument

    RTswitchAlloc (false);

    //                               multi-process && multiple processes
    global->tables = cct_create_map (!pthreads && HREdefaultRegion(HREglobal()) != NULL);
}

void
global_create  (bool pthreads)
{
    if (HREme(HREglobal()) == 0) {
        global_alloc (pthreads);
    }

    if (pthreads) {
        HREbarrier (HREglobal());
    } else {
        HREreduce (HREglobal(), 1, &global, &global, Pointer, Max);
    }
}

static void
global_add_stats (stats_t *stat)
{
    global->stats.elts += stat->elts;
    global->stats.nodes += stat->nodes;
    global->stats.tests += stat->tests;
    global->stats.misses += stat->misses;
    global->stats.rehashes += stat->rehashes;
}

static void
init_bits () //TODO: ditsibute
{
    int i = 0;
    while (Strat_None != strategy[i] && i < MAX_STRATEGIES) {
        global_bits += num_global_bits (strategy[i]);
        local_bits += (~Strat_DFSFIFO & Strat_LTL & strategy[i++] ? 2 : 0);
    }
    count_bits = (Strat_LNDFS == strategy[i - 1] ? ceil (log2 (W + 1)) : 0);
}

static void
init_action_labels (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    if (act_detect) {
        // table number of first edge label
        act_label = lts_type_find_edge_label_prefix (
                ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        if (act_label == -1)
            Abort("No edge label '%s...' for action detection",
                  LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        act_type = lts_type_get_edge_label_typeno (ltstype, act_label);
        chunk c = chunk_str(act_detect);
        act_index = GBchunkPut (model, act_type, c);
    }
}

static void
global_statics_init (model_t model)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    matrix_t           *m = GBgetDMInfo (model);
    SL = lts_type_get_state_label_count (ltstype);
    EL = lts_type_get_edge_label_count (ltstype);
    N = lts_type_get_state_length (ltstype);
    D = (strategy[0] & Strat_TA ? N - 2 : N);
    K = dm_nrows (m);
    W = HREpeers(HREglobal());

    init_action_labels ( model);

    init_dbs ();

    init_bits ();
}


static void
global_static_setup  (model_t model, bool timed)
{
    global_statics_init (model);

    options_static_setup (model, timed);
}

static void
global_setup  (model_t model, bool timed)
{
    RTswitchAlloc (!global->pthreads);

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
    if (strategy[1] == Strat_MAP || (trc_output && !(strategy[0] & (Strat_LTL | Strat_TA))))
        global->parent_ref = RTmalloc (sizeof(ref_t[1UL<<dbs_size]));
    if (timed)
        global->lmap = lm_create (W, 1UL<<dbs_size, LATTICE_BLOCK_SIZE);
    global->lb = lb_create_max (W, G, H);
    global->threshold = strategy[0] & Strat_NDFS ? THRESHOLD : THRESHOLD / W;

    RTswitchAlloc (false);
}

void
global_init  (model_t model, bool timed)
{
    if (global->pthreads) {
        // for pthreads the shared variables need only be initialized once
        if (HREme(HREglobal())==0) {
            global_static_setup (model, timed);
        }
    } else {
        // in the multi-process environment, the memory is local be default and
        // all workers need to do their own setup
        global_static_setup (model, timed);
    }


    if (HREme(HREglobal())==0) {
        global_setup (model, timed);
    }

    HREbarrier (HREglobal());
}

void
global_print  (model_t model)
{
    if (HREme(HREglobal()) == 0) {
        print_options (model);
    }
}

void
global_print_stats (model_t model)
{
    // Print memory statistics
    double              memQ, pagesDB, memC, memDB, compr, fill, leafs;
    size_t              db_elts = global->stats.elts;
    size_t              db_nodes = global->stats.nodes;
    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    double              el_size =
       db_type & Tree ? (db_type==ClearyTree?1:2) + (2.0 / (1UL<<ratio)) : D+.5;
    size_t              s = state_info_size();
    size_t              max_load;

    memQ = ((double)(s * max_load)) / (1 << 20);
    Warning (info, " ");
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.2fMB",
             s, max_load, memQ);
    pagesDB = ((double)(1UL << (dbs_size)) / (1<<20)) * SLOT_SIZE * el_size;
    memC = ((double)(((((size_t)local_bits)<<dbs_size))/8*W)) / (1UL<<20);
    memDB = ((double)(db_nodes * SLOT_SIZE * el_size)) / (1<<20);
    fill = (double)((db_elts * 100) / (1UL << dbs_size));
    if (db_type & Tree) {
        compr = (double)(db_nodes * el_size) / ((D+1) * db_elts) * 100;
        leafs = (double)(((db_nodes - db_elts) * 100) / (1UL << (dbs_size-ratio)));
        Warning (info, "Tree memory: %.1fMB, compr.: %.1f%%, fill (roots/leafs): "
                "%.1f%%/%.1f%%", memDB, compr, fill, leafs);
    } else {
        Warning (info, "Table memory: %.1fMB, fill ratio: %.1f%%", memDB, fill);
    }
    double chunks = cct_print_stats (info, infoLong, GBgetLTStype(model),
                                     global->tables) / (1<<20);
    Warning (info, "Est. total memory use: %.1fMB (~%.1fMB paged-in)",
             memQ + memDB + memC + chunks, memQ + pagesDB + memC + chunks);

    Warning (infoLong, " ");
    Warning (infoLong, "Database statistics:");
    Warning (infoLong, "Elements: %zu",  db_elts);
    Warning (infoLong, "Nodes: %zu", db_nodes);
    Warning (infoLong, "Misses: %zu", global->stats.misses);
    Warning (infoLong, "Tests: %zu", global->stats.tests);
    Warning (infoLong, "Rehashes: %zu", global->stats.rehashes);

    Warning (infoLong, " ");
    Warning (infoLong, "Memory usage statistics:");
    Warning (infoLong, "Queue: %.1f MB",  memQ);
    Warning (infoLong, "DB: %.1f MB", memDB);
    Warning (infoLong, "Colors: %.1f MB",  memC);
    Warning (infoLong, "Chunks: %.1f MB",  chunks);
    Warning (infoLong, "DB paged in: %.1f MB",  pagesDB);
}

void
global_reduce_and_print_stats (model_t model)
{
    size_t              id = HREme(HREglobal());
    for (size_t i = 0; i < W; i++) {
        if (i == id) {
            stats_t            *stats = statistics (global->dbs);
            global_add_stats (stats);
        }
        HREbarrier (HREglobal());
    }

    if (id == 0) {
        global_print_stats (model);
    }
}

void
global_deinit ()
{
    RTswitchAlloc (!global->pthreads);

    if (HashTable & db_type)
        DBSLLfree (global->dbs); //TODO
    else //TreeDBSLL
        TreeDBSLLfree (global->dbs);
    lb_destroy (global->lb);
    if (global->lmap != NULL)
        lm_free (global->lmap);
    if (global->zobrist != NULL)
        zobrist_free(global->zobrist);

    RTswitchAlloc (false);
}
