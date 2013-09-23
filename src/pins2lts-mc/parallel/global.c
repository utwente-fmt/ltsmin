/*
 * permutor.c
 *
 *  Created on: Sep 20, 2013
 *      Author: laarman
 */

#include <hre/config.h>

#include <math.h>
#include <popt.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <mc-lib/lb.h>
#include <pins-lib/pins-impl.h>
#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/algorithm/reach.h>
#include <pins2lts-mc/algorithm/timed.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/options.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>

static const size_t     THRESHOLD = 100000 / 100 * SPEC_REL_PERF;

global_t        *global;

int              act_index = -1;
int              act_type = -1;
int              act_label = -1;
size_t           W = -1;
size_t           G = 1;
size_t           H = 1000;
size_t           D;                  // size of state in explicit state DB
size_t           N;                  // size of entire state
size_t           K;                  // number of groups
size_t           SL;                 // number of state labels
size_t           EL;                 // number of edge labels
size_t           max_level_size = 0;

void
global_create  ()
{
    global = RTmallocZero (sizeof(global_t));
}

void
add_stats(stats_t *stat)
{
    global->stats.elts += stat->elts;
    global->stats.nodes += stat->nodes;
    global->stats.tests += stat->tests;
    global->stats.misses += stat->misses;
    global->stats.rehashes += stat->rehashes;
}

void
shared_init  (model_t model)
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
    if (strategy[1] == Strat_MAP || (trc_output && !(strategy[0] & (Strat_LTL | Strat_TA))))
        global->parent_ref = RTmalloc (sizeof(ref_t[1UL<<dbs_size]));
    if (Strat_TA & strategy[0])
        global->lmap = lm_create (W, 1UL<<dbs_size, LATTICE_BLOCK_SIZE);
    global->lb = lb_create_max (W, G, H);
    global->contexts = RTmalloc (sizeof (wctx_t*[W]));
    global->threshold = strategy[0] & Strat_NDFS ? THRESHOLD : THRESHOLD / W;
}

void
global_register (wctx_t *ctx)
{
    global->contexts[ctx->id] = ctx; // publish local context
}

void
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

void
global_deinit ()
{
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
}

void
init_bits () //TODO: ditsibute
{
    int i = 0;
    while (Strat_None != strategy[i] && i < MAX_STRATEGIES) {
        global_bits += num_global_bits (strategy[i]);
        local_bits += (~Strat_DFSFIFO & Strat_LTL & strategy[i++] ? 2 : 0);
    }
    count_bits = (Strat_LNDFS == strategy[i - 1] ? ceil (log2 (W + 1)) : 0);
}

void
statics_init (model_t model)
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
