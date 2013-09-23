/**
 *
 */

#ifndef STATE_INFO_H
#define STATE_INFO_H

#include <stdlib.h>
#include <mc-lib/lmap.h>
#include <mc-lib/treedbs-ll.h>
#include <util-lib/fast_hash.h>

typedef int                *raw_data_t;
typedef int                *state_data_t;
static const state_data_t   state_data_dummy;
static const size_t         SLOT_SIZE = sizeof(*state_data_dummy);

typedef struct state_info_s {
    state_data_t        data;
    tree_t              tree;
    ref_t               ref;
    lattice_t           lattice;
    hash64_t            hash64;
    lm_loc_t            loc;
} state_info_t;

extern size_t state_info_size ();
extern size_t state_info_int_size ();
extern void state_info_create_empty (state_info_t *state);
extern void state_info_create (state_info_t *state, state_data_t data,
                               tree_t tree, ref_t ref);
extern void state_info_serialize (state_info_t *state, raw_data_t data);
extern void state_info_deserialize (state_info_t *state, raw_data_t data,
                                    raw_data_t store);
extern int state_info_initialize (state_info_t *state, state_data_t data,
                                  transition_info_t *ti, state_info_t *src,
                                  state_data_t store);
extern void state_info_deserialize_cheap (state_info_t *state, raw_data_t data);

#endif // STATE_INFO_H
