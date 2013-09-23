/**
 *
 */

#include <hre/config.h>

#include <pins2lts-mc/algorithm/algorithm.h>
#include <pins2lts-mc/parallel/global.h>
#include <pins2lts-mc/parallel/state-info.h>
#include <pins2lts-mc/parallel/state-store.h>

// TODO: distribute

struct state_info_factory_s {
    state_data_t        store;          // temporary state storage1
    state_data_t        store2;         // temporary state storage2
    state_info_t        state;          // currently explored state
    state_info_t        initial;        // initial state
    state_info_t       *successor;      // current successor state
};

/**
 * Algo's state representation and serialization / deserialization
 */

void
state_info_create_empty (state_info_t *state)
{
    state->tree = NULL;
    state->data = NULL;
    state->ref = DUMMY_IDX;
}

void
state_info_create (state_info_t *state, state_data_t data, tree_t tree,
                   ref_t ref)
{
    state->data = data;
    state->tree = tree;
    state->ref = ref;
}

size_t
state_info_int_size ()
{
    return (state_info_size () + 3) / 4;
}

size_t
state_info_size ()
{
    size_t              ref_size = sizeof (ref_t);
    size_t              data_size = SLOT_SIZE * (HashTable & db_type ? D : 2*D);
    size_t              state_info_size = refs ? ref_size : data_size;
    if (!refs && (HashTable & db_type))
        state_info_size += ref_size;
    if (ZOBRIST)
        state_info_size += sizeof (hash64_t);
    if (Strat_OWCTY & strategy[0])
        state_info_size++;
    if (Strat_TA & strategy[0])
        state_info_size += sizeof (lattice_t) + sizeof (lm_loc_t);
    return state_info_size;
}

/**
 * Next-state function output --> algorithm
 */
int
state_info_initialize (state_info_t *state, state_data_t data,
                       transition_info_t *ti, state_info_t *src,
                       state_data_t store)
{
    state->data = data;
    if (Strat_TA & strategy[0]) {
        state->lattice = *(lattice_t*)(data + D);
        state->loc = LM_NULL_LOC;
    }
    return find_or_put (state, ti, src, store);
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_serialize (state_info_t *state, raw_data_t data)
{
    if (ZOBRIST) {
        ((uint64_t*)data)[0] = state->hash64;
        data += 2;
    }
    if (refs) {
        ((ref_t*)data)[0] = state->ref;
        data += 2;
    } else if (HashTable & db_type) {
        ((ref_t*)data)[0] = state->ref;
        data += 2;
        memcpy (data, state->data, (SLOT_SIZE * D));
        data += D;
    } else { // Tree
        memcpy (data, state->tree, (2 * SLOT_SIZE * D));
        data += D<<1;
    }
    if (Strat_TA & strategy[0]) {
        ((lattice_t*)data)[0] = state->lattice;
        HREassert (state->lattice != 0);
        data += 2;
        ((lm_loc_t*)data)[0] = state->loc;
    }
}

/**
 * From stack/queue --> algorithm
 */
void
state_info_deserialize (state_info_t *state, raw_data_t data, state_data_t store)
{
    if (ZOBRIST) {
        state->hash64 = ((hash64_t*)data)[0];
        data += 2;
    }
    if (refs) {
        state->ref  = ((ref_t*)data)[0];
        data += 2;
        state->data = get (global->dbs, state->ref, store);
        if (Tree & db_type) {
            state->tree = state->data;
            state->data = TreeDBSLLdata (global->dbs, state->data);
        }
    } else {
        if (HashTable & db_type) {
            state->ref  = ((ref_t*)data)[0];
            data += 2;
            state->data = data;
            data += D;
        } else { // Tree
            state->tree = data;
            state->data = TreeDBSLLdata (global->dbs, data);
            state->ref  = TreeDBSLLindex (global->dbs, data);
            data += D<<1;
        }
    }
    if (Strat_TA & strategy[0]) {
        state->lattice = ((lattice_t*)data)[0];
        HREassert (state->lattice != 0);
        data += 2;
        state->loc = ((lm_loc_t*)data)[0];
    }
}

void
state_info_deserialize_cheap (state_info_t *state, raw_data_t data)
{
    HREassert (refs);
    if (ZOBRIST) {
        state->hash64 = ((hash64_t*)data)[0];
        data += 2;
    }
    state->ref  = ((ref_t*)data)[0];
    data += 2;
    if (Strat_TA & strategy[0]) {
        state->lattice = ((lattice_t*)data)[0];
        HREassert (state->lattice != 0);
        data += 2;
        state->loc = ((lm_loc_t*)data)[0];
    }
}
