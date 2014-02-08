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
#include <pins2lts-mc/parallel/state-store.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/zobrist.h>

static const size_t     THRESHOLD = 10000;

extern size_t           init_threshold;     // Initial threshold for progress reporting
extern int              act_index;          // Index of action label sought after
extern int              act_type;           // Type of action label sought after
extern int              act_label;          // LTS table of action label sought after
extern size_t           W;                  // Number of workers (threads or processes)
extern size_t           G;                  // Granularity for load balancer
extern size_t           H;                  // Hand off size for load balancer
extern size_t           D;                  // size of state in explicit state DB
extern size_t           N;                  // size of entire state
extern size_t           K;                  // number of groups
extern size_t           SL;                 // number of state labels
extern size_t           EL;                 // number of edge labels

typedef struct global_s {
    bool                procs;          // Multi-process (or pthreads)
    int                 exit_status;    // Exit status
    cct_map_t          *tables;         // concurrent chunk tables
    stats_t             stats;          // Global statistics
    state_store_t      *store;          // Global stores for states / lattices
} global_t;

extern global_t        *global;

extern void global_create   (bool pthreads);

extern void global_init     (model_t model, size_t speed, bool timed);

extern void global_print    (model_t model);

extern void global_print_stats (model_t model, size_t local_state_infos,
                                size_t stack);

extern void global_reduce_stats (model_t model);

extern void global_deinit   ();

#endif // GLOBAL_H
