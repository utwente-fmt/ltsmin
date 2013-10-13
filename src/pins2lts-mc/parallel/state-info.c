/**
 *
 * State info takes care of serialization of state information. Three streams
 * are involved:
 *
 *  /---------------\  deserialization   /-----------------\
 *  |               | -----------------> |                 | ---> .ref
 *  | STACK / QUEUE |                    |   State info    | ---> pins_state()
 *  |               | <----------------- |                 | ---> state()
 *  \---------------/   serialization    \-----------------/
 *                                               |   ^
 *                                        state  |   |
 *                                               |   |
 *                                               v   | new
 *                                         /-----------------\
 *                                         |                 |
 *                                         | State store |
 *                                         |      (PINS)     |
 *                                         \-----------------/
 */


#include <hre/config.h>

#include <inttypes.h>

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/state-store.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/lmap.h>
#include <mc-lib/treedbs-ll.h>
#include <util-lib/fast_hash.h>

struct si_internal_s {
    state_data_t        tmp_init; // temporary state storage for tree
    state_data_t        tmp_pins; // temporary storage for pins state
    streamer_t         *stack_serialize; // stream of (de)serializers to stack
    streamer_t         *pins_serialize; // stream of serializer to PINS
    store_t            *store;
};

static void
ref_ser (void *ctx, void *ptr, raw_data_t data)
{
    ((ref_t *)data)[0] = ((ref_t *) ptr)[0];
    (void) ctx;
}

static void
ref_des (void *ctx, void *ptr, raw_data_t data)
{
    ((ref_t *) ptr)[0]  = ((ref_t *)data)[0];
    (void) ctx;
}

static void
table_ser (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    memcpy (data, store_state(si->in->store), SLOT_SIZE * D);
    (void) ctx;
}

static void
table_des (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    store_set_state (si->in->store, (state_data_t) data);
    (void) ctx;
}

static void
tree_ser (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    memcpy (data, store_tree(si->in->store), 2 * SLOT_SIZE * D);
    (void) ctx;
}

static void
tree_des (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    store_tree_index (si->in->store, (tree_t) data);
    (void) ctx;
}

static inline void
state_info_clear (state_info_t* si)
{
    si->ref = DUMMY_IDX;
    si->lattice = LM_NULL_LATTICE;
    store_clear (si->in->store);
}

streamer_t *
create_serializers (state_info_t *si)
{
    streamer_t         *stream = streamer_create ();
    serializer_t       *s;
    size_t              stab = SLOT_SIZE * D;
    size_t              stree = SLOT_SIZE * D * 2;
    switch (db_type) {
    case HashTable:
        if (refs) {
            s = serializer_create (ref_ser, ref_des, sizeof(ref_t), &si->ref);
            streamer_add (stream, s);
        } else {
            s = serializer_create (ref_ser, ref_des, sizeof(ref_t), &si->ref);
            streamer_add (stream, s);
            s = serializer_create (table_ser, table_des, stab, si);
            streamer_add (stream, s);
        }
        break;
    case ClearyTree:
    case TreeTable:
        if (refs) {
            s = serializer_create (ref_ser, ref_des, sizeof(ref_t), &si->ref);
            streamer_add (stream, s);
        } else {
            s = serializer_create (tree_ser, tree_des, stree, si);
            streamer_add (stream, s);
        }
        break;
    default: Abort ("State store not implemented");
    }
    return stream;
}

/**
 * only the state is required, no additional lattice
 */
static void
pins_ptr (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    state_data_t        state = state_info_state (si);
    ((state_data_t *)data)[0] = state;
    (void) ctx;
}

/**
 * Copy the state to a temp location to be able to add the lattice to it
 */
static void
ta_copy_pins (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    memcpy (si->in->tmp_pins, state_info_state(si), SLOT_SIZE * D);
    ((lattice_t *)si->in->tmp_pins + D)[0] = si->lattice;
    ((state_data_t *)data)[0] = si->in->tmp_pins;
    (void) ctx;
}

/**
 * The state was assembled by the tree in the local tmp_init,
 * which contains room for the additional lattice.
 */
static void
ta_tree_ref_pins (void *ctx, void *ptr, raw_data_t data)
{
    state_info_t       *si = (state_info_t *) ptr;
    state_data_t        state = state_info_state (si);
    ((lattice_t *)state + D)[0] = si->lattice;
    ((state_data_t *)data)[0] = state;
    (void) ctx;
}

/**
 * serializes a pointer to the full PINS state (including lattice)
 */
streamer_t *
create_pins (state_info_t *si)
{
    streamer_t         *stream = streamer_create ();
    serializer_t       *s;

    if (strategy[0] & Strat_TA) {
        switch (db_type) {
        case HashTable:
            s = serializer_create (ta_copy_pins, dummy_action, sizeof(void*), si);
            break;
        case ClearyTree:
        case TreeTable:
            if (refs) {
                s = serializer_create (ta_tree_ref_pins, dummy_action, sizeof(void*), si);
            } else {
                s = serializer_create (ta_copy_pins, dummy_action, sizeof(void*), si);
            }
            break;
        default: Abort ("State store not implemented");
        }
    } else {
        // by default on the state data suffices (no lattice needs to be added)
        s = serializer_create (pins_ptr, dummy_action, sizeof(void*), si);
    }
    streamer_add (stream, s);
    return stream;
}

void
state_info_add (state_info_t *si, serializer_t *s)
{
    streamer_add (si->in->stack_serialize, s);
}

state_info_t *
state_info_create ()
{
    state_info_t       *si = RTalignZero (CACHE_LINE_SIZE, sizeof(state_info_t));
    si->in = RTmallocZero (sizeof(si_internal_t));
    si->in->tmp_pins = RTalignZero (CACHE_LINE_SIZE, SLOT_SIZE * N * 2 + sizeof(lattice_t));
    si->in->stack_serialize = create_serializers (si);
    si->in->store = store_create (si);
    si->in->pins_serialize = create_pins (si);
    state_info_clear (si);
    return si;
}

size_t
state_info_serialize_size (state_info_t *si)
{
    return streamer_get_size (si->in->stack_serialize);
}

size_t
state_info_serialize_int_size (state_info_t *si)
{

    return INT_SIZE (state_info_serialize_size (si));
}

state_data_t
state_info_pins_state (state_info_t *si)
{
    state_data_t        state;
    streamer_walk (si->in->pins_serialize, NULL, (raw_data_t) &state, SERIALIZE);
    Debug ("Synthesized state %"PRIu32", %zu", MurmurHash32 (state, D*4, 0), si->ref);
    return state;
}

bool
state_info_new_state (state_info_t *si, state_data_t data,
                      transition_info_t *ti, state_info_t *src)
{
    return store_new_state (si->in->store, data, ti, src->in->store);
}

void
state_info_serialize (state_info_t *si, raw_data_t data)
{
    HREassert (si->ref != DUMMY_IDX);
    streamer_walk (si->in->stack_serialize, NULL, data, SERIALIZE);
    Debug ("Serialized state %"PRIu32", %zu at %p",
           MurmurHash32 (store_state(si->in->store), D*4, 0), si->ref, data);
}

void
state_info_deserialize (state_info_t *si, raw_data_t data)
{
    state_info_clear (si);
    streamer_walk (si->in->stack_serialize, NULL, data, DESERIALIZE);
    Debug ("Deserialized state %"PRIu32", %zu at %p",
           MurmurHash32 (store_state(si->in->store), D*4, 0), si->ref, data);
    HREassert (si->ref != DUMMY_IDX);
}

void
state_info_set (state_info_t *si, ref_t ref, lattice_t lat)
{
    state_info_clear (si);
    si->ref = ref;
    si->lattice = lat;
}

bool
state_info_first (state_info_t *si, state_data_t data)
{
    return store_first (si->in->store, data);
}

state_data_t
state_info_state (state_info_t *si)
{
    return store_state (si->in->store);
}
