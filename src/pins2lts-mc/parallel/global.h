/**

 */


#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdlib.h>

#include <mc-lib/cctables.h>
#include <mc-lib/lmap.h>
#include <mc-lib/lb.h>
#include <mc-lib/trace.h>
#include <pins2lts-mc/parallel/worker.h>
#include <util-lib/zobrist.h>


typedef struct global_s {
    void               *dbs;            // Hash table/Tree table/Cleary tree
    ref_t              *parent_ref;     // trace reconstruction / OWCTY MAP TODO
    size_t              threshold;      // print threshold
    wctx_t            **contexts;       // Thread contexts
    cct_map_t          *tables;         // concurrent chunk tables
    int                 exit_status;    // Exit status
    lb_t               *lb;             // Load balancer  TODO: make run-local
    lm_t               *lmap;           // Lattice map (Strat_TA)
    zobrist_t           zobrist;        // Zobrist hasher
    stats_t             stats;          // Global statistics
} global_t;

extern global_t        *global;

extern void global_create   ();
extern void statics_init    (model_t model);
extern void shared_init     (model_t model);
extern void global_deinit   ();

extern void add_stats       (stats_t *stat);

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
extern size_t           max_level_size;

#endif // GLOBAL_H
