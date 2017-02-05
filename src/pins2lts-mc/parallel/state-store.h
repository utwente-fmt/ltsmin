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
#include <mc-lib/treedbs-ll.h>
#include <pins2lts-mc/parallel/color.h>
#include <pins2lts-mc/parallel/state-info.h>

typedef enum {
    HashTable   = 1,
    TreeTable   = 2,
    ClearyTree  = 4,
    Tree        = TreeTable | ClearyTree
} db_type_t;

extern struct poptOption state_store_options[];

extern si_map_entry db_types[];

typedef struct state_store_s state_store_t;

/* predecessor --(transition_info)--> successor */
typedef int         (*find_or_put_f)(state_info_t *successor,
                                     transition_info_t *ti,
                                     state_info_t *predecessor,
                                     state_data_t store);

extern db_type_t        db_type;
extern char            *state_repr;
extern char            *table_size;
extern int              dbs_size;
extern size_t           ratio;
extern int              refs;
extern int              ZOBRIST;
extern int              indexing;

extern void     state_store_static_init ();

extern state_store_t *state_store_init (model_t model, bool timed);

extern void     state_store_deinit (state_store_t *store);

extern stats_t *state_store_stats (state_store_t *store);

extern lm_t     *state_store_lmap (state_store_t *store);

extern size_t   state_store_local_bits (state_store_t *store);

extern void     state_store_print (state_store_t *store);

extern int      state_store_has_color (ref_t ref, global_color_t color, int rec_bits);

//RED and BLUE are independent
extern int      state_store_try_color (ref_t ref, global_color_t color, int rec_bits);

extern uint32_t state_store_inc_wip (ref_t ref);

extern uint32_t state_store_dec_wip (ref_t ref);

extern uint32_t state_store_get_wip (ref_t ref);

extern uint32_t state_store_get_colors (ref_t ref);

extern int state_store_try_set_counters (ref_t ref, size_t bits,
                                         uint64_t old_val, uint64_t new_val);

extern int state_store_try_set_colors (ref_t ref, size_t bits,
                                       uint64_t old_val, uint64_t new_val);

extern char *state_store_full_msg (int dbs_ret_value);


typedef struct store_s store_t;

extern store_t *store_create (state_info_t *si);

extern void store_clear (store_t* store);

extern int  store_new_state (store_t *store, state_data_t data,
                            transition_info_t *ti, store_t *src);

extern int  store_first (store_t *store, state_data_t data);

extern void store_set_state (store_t *store, state_data_t state);

extern state_data_t store_state (store_t *store);

extern int store_find (store_t *store, state_data_t data, transition_info_t *ti,
                       store_t *src);

extern tree_t store_tree (store_t *store);

extern void store_tree_index (store_t *store, tree_t tree);

#endif // STATE_STORE_H
