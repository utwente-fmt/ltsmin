/**
 * Next-state permutator
 *
 * Integrated state storage f the multi-core tool to increase efficiency
 */

#ifndef STATE_STORE_H
#define STATE_STORE_H

#include <stdlib.h>

#include <hre/runtime.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/lmap.h>
#include <mc-lib/treedbs-ll.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <util-lib/fast_hash.h>

extern struct poptOption state_store_options[];

typedef enum {
    HashTable   = 1,
    TreeTable   = 2,
    ClearyTree  = 4,
    Tree        = TreeTable | ClearyTree
} db_type_t;

extern si_map_entry db_types[];

/* predecessor --(transition_info)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     transition_info_t *ti,
                                     state_info_t *predecessor,
                                     state_data_t store);

extern db_type_t        db_type;

extern find_or_put_f    find_or_put;
extern dbs_get_f        get;
extern dbs_stats_f      statistics;
extern hash64_f         hasher;

extern char            *state_repr;
extern char            *table_size;
extern int              dbs_size;
extern size_t           ratio;
extern int              refs;
extern int              ZOBRIST;
extern int              count_bits;
extern int              global_bits;
extern int              local_bits;
extern int              indexing;

extern void init_dbs ();

extern void *get_state (ref_t ref, void *arg);

#endif // STATE_STORE_H
