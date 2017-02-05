/**
 *
 */

#ifndef STATE_INFO_H
#define STATE_INFO_H

#include <stdlib.h>

#include <mc-lib/lmap.h>
#include <pins-lib/pins.h>
#include <pins2lts-mc/parallel/stream-serializer.h>

typedef int                *state_data_t;
static const state_data_t   state_data_dummy;
static const size_t         SLOT_SIZE = sizeof(*state_data_dummy);

typedef struct si_internal_s si_internal_t;

typedef struct state_info_s {
    ref_t               ref;
    lattice_t           lattice;
    si_internal_t      *in;
} state_info_t;

extern size_t   state_info_serialize_size     (state_info_t *si);

extern size_t   state_info_serialize_int_size (state_info_t *si);

extern state_info_t *state_info_create       ();

extern void     state_info_add (state_info_t *si, action_f ser, action_f des,
                                size_t size, void *ptr);

extern void     state_info_add_simple (state_info_t *si, size_t size, void *ptr);

extern void     state_info_serialize    (state_info_t *state, raw_data_t data);

extern void     state_info_deserialize  (state_info_t *state, raw_data_t data);

extern int      state_info_new_state    (state_info_t *state, state_data_t data,
                                         transition_info_t *ti, state_info_t *src);

extern int      state_info_find_state   (state_info_t *state, state_data_t data,
                                         transition_info_t *ti, state_info_t *src);

extern int      state_info_first (state_info_t *si, state_data_t data);

extern state_data_t state_info_state    (state_info_t *si);

extern state_data_t state_info_pins_state (state_info_t *si);

extern void     state_info_set (state_info_t *si, ref_t ref, lattice_t lat);

#endif // STATE_INFO_H
