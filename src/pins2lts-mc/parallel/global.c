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
#include <pins-lib/pins.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>

global_t        *global = NULL;

size_t           init_threshold;
int              act_index = -1;
int              act_type = -1;
int              act_label = -1;

//TODO: rename/eliminate these variables
size_t           W = -1;            // Number of workers (threads or processes)
size_t           G = 1;             // Granularity for load balancer
size_t           H = 1000;          // Hand off size for load balancer
size_t           D;                 // size of state in explicit state DB
size_t           N;                 // size of entire state
size_t           K;                 // number of groups
size_t           SL;                // number of state labels
size_t           EL;                // number of edge labels

void
global_alloc  (bool procs)
{
    GBloadFileShared (NULL, files[0]); // NOTE: no model argument
    RTswitchAlloc (procs);
    global = RTmallocZero (sizeof(global_t));
    RTswitchAlloc (false);

    global->procs = procs;
    global->exit_status = LTSMIN_EXIT_SUCCESS;
    global->tables = cct_create_map (procs); // HRE-aware object
}

void
global_create  (bool procs)
{
    if (HREme(HREglobal()) == 0) {
        global_alloc (procs);
    }

    if (procs) {
        HREreduce (HREglobal(), 1, &global, &global, Pointer, Max);
    } else {
        HREbarrier (HREglobal());
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
        act_index = pins_chunk_put  (model, act_type, c);
    }
}

static void
global_static_init (model_t model, size_t speed, bool timed)
{
    lts_type_t          ltstype = GBgetLTStype (model);
    matrix_t           *m = GBgetDMInfo (model);
    SL = lts_type_get_state_label_count (ltstype);
    EL = lts_type_get_edge_label_count (ltstype);
    N = lts_type_get_state_length (ltstype);
    D = (timed ? N - sizeof(lattice_t) / sizeof(int) : N);
    K = dm_nrows (m);
    W = HREpeers(HREglobal());
    init_threshold = THRESHOLD / W / 100 * speed;

    init_action_labels (model);
}

static void
global_static_setup  (model_t model, size_t speed, bool timed)
{
    global_static_init (model, speed, timed);

    state_store_static_init ();

    options_static_init (model, timed);
}

static void
global_setup  (model_t model, bool timed)
{
    RTswitchAlloc (global->procs);
    global->store = state_store_init (model, timed);
    RTswitchAlloc (false);
}

void
global_init  (model_t model, size_t speed, bool timed)
{
    if (global->procs) {
        // in the multi-process environment, the memory is local be default and
        // all workers need to do their own setup
        global_static_setup (model, speed, timed);
    } else {
        // for pthreads the shared variables need only be initialized once
        if (HREme(HREglobal())==0) {
            global_static_setup (model, speed, timed);
        }
    }

    if (HREme(HREglobal())==0) {
        global_setup (model, timed);
    }

    HREbarrier (HREglobal());
}

void
global_deinit ()
{
    RTswitchAlloc (global->procs);
    state_store_deinit (global->store);
    RTfree (global);
    RTswitchAlloc (false);
}

void
global_print  (model_t model)
{
    if (HREme(HREglobal()) == 0) {
        print_options (model);
        state_store_print (global->store);
    }

    HREbarrier (HREglobal()); // just to ensure progress/summary printing is last
}

void
global_print_stats (model_t model, size_t local_state_infos, size_t stack)
{
    // Print memory statistics
    double              memQ, pagesDB, memC, memDB, memDBB, memT, memTB, compr,
                        fill, leaves, el_size;
    size_t              db_elts = global->stats.elts;
    size_t              db_nodes = global->stats.nodes;
    size_t              local_bits = state_store_local_bits (global->store);

    db_nodes = db_nodes == 0 ? db_elts : db_nodes;
    el_size = db_type & Tree ? (db_type == ClearyTree ? 1 : 2) : D + .5;
    pagesDB = ((double)(1ULL << dbs_size) * SLOT_SIZE * el_size) / (1ULL<<20);
    memC = ((double)(((((size_t)local_bits)<<dbs_size))/8*W)) / (1ULL<<20);
    memDBB = db_elts * SLOT_SIZE * (D + .5); // D slots + 16bit memoized hash
    memDB = memDBB / (1ULL<<20);
    fill = (double)((db_elts * 100) / (1ULL << dbs_size));

    // print additional local queue/stack memory statistics
    Warning (info, " ");
    memQ = ((double)(stack * local_state_infos)) / (1<<20);
    Warning (info, "Queue width: %zuB, total height: %zu, memory: %.2fMB",
             stack, local_state_infos, memQ);

    if (db_type & Tree) {
        size_t          db_leaves = db_nodes - db_elts;
        memTB = db_elts * SLOT_SIZE * (db_type == ClearyTree ? 1 : 2);
        memTB += db_leaves * SLOT_SIZE * 2.0;
        memT = memTB / (1ULL<<20);
        compr = (memTB / memDBB) * 100;

        Warning (info, "Tree memory: %.1fMB, %.1f B/state, compr.: %.1f%%",
                 memT, memTB / db_elts, compr);
        leaves = (double)((db_leaves * 100) / (1ULL << (dbs_size - ratio)));
        Warning (info, "Tree fill ratio (roots/leafs): %.1f%%/%.1f%%", fill, leaves);

        memDB = memT;

    } else {
        Warning (info, "Table memory: %.1fMB, fill ratio: %.1f%%", memDB, fill);
    }
    if (PINS_POR_ALG == POR_LIPTON || PINS_POR_ALG == POR_TR)
        por_stats (model);
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
global_reduce_stats (model_t model)
{
    RTswitchAlloc (global->procs);
    size_t              id = HREme(HREglobal());
    for (size_t i = 0; i < W; i++) {
        if (i == id) {
            stats_t            *stats = state_store_stats (global->store);
            global_add_stats (stats);
        }
        HREbarrier (HREglobal());
    }
    RTswitchAlloc (false);
    (void) model;
}
