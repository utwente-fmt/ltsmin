/**

 */


#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdbool.h>
#include <stdlib.h>

#include <mc-lib/atomics.h>
#include <mc-lib/cctables.h>
#include <mc-lib/lmap.h>
#include <mc-lib/trace.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/zobrist.h>

static const size_t     THRESHOLD = 10000;

extern size_t           init_threshold; // Initial threshold
extern int              act_index;
extern int              act_type;
extern int              act_label;
extern size_t           W;
extern size_t           G;
extern size_t           H;
extern size_t           D;                  // size of state in explicit state DB
extern size_t           N;                  // size of entire state
extern size_t           K;                  // number of groups
extern size_t           SL;                 // number of state labels
extern size_t           EL;                 // number of edge labels

typedef struct global_s {
    bool                pthreads;       // Pthreads (or multi-process)
    int                 exit_status;    // Exit status

    cct_map_t          *tables;         // concurrent chunk tables

    void               *dbs;            // Hash table/Tree table/Cleary tree
    lm_t               *lmap;           // Lattice map (Strat_TA)
    zobrist_t           zobrist;        // Zobrist hasher
    stats_t             stats;          // Global statistics

} global_t;

extern global_t        *global;

extern void global_create   (bool pthreads);

extern void global_init     (model_t model, bool timed);

extern void global_print    (model_t model);

extern void global_deinit   ();

extern void global_print_stats (model_t model, size_t local_state_infos);

extern void global_reduce_stats (model_t model);

#endif // GLOBAL_H
