#include <hre/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/lmap.h>

static const size_t     LM_FACTOR = 32; // size of the map as a ratio to the locations
#define                 LM_MAX_THREADS (sizeof (size_t) * 8)

/**
 * A store can be: empty, reference, lattice or tombstone (deleted, but probe seq. continues)
 * Orthogonally a store can lie on the end of an allocated block. Hence _END.
 */
typedef enum lm_internal_e {
    LM_STATUS_EMPTY         = 0, //
    LM_STATUS_END           = 2, //
    LM_STATUS_REF           = 1, // implies END
    LM_STATUS_LATTICE       = 3, //
    LM_STATUS_LATTICE_END   = 4, //
    LM_STATUS_TOMBSTONE     = 5, //
    LM_STATUS_TOMBSTONE_END = 6  //
} lm_internal_t;

#ifdef __x86_64__
#define LATTICE_BITS 57
#else
#define LATTICE_BITS 25
#endif

typedef struct lm_store_s {
    lattice_t           lock    : 1;
    lm_internal_t       internal: 3;
    lm_status_t         status  : 3;
    lattice_t           lattice : LATTICE_BITS;
} lm_store_t;


typedef struct local_s {
    size_t              id;
    size_t              next_store;
    lm_store_t         *table;
} __attribute__ ((aligned(1ULL<<CACHE_LINE))) local_t;

struct lm_s {
    size_t              size;
    size_t              workers;
    size_t              wSize;
    lm_store_t         *table;
    lm_store_t         *table_end;
    size_t              block_size;
    hre_key_t           local_key;
    local_t            *locals[LM_MAX_THREADS];
};

static size_t           next_index = 0;

static inline local_t *
get_local (lm_t *map)
{
    local_t            *loc = HREgetLocal (map->local_key);
    if (NULL == loc) {
        loc = RTalignZero (CACHE_LINE_SIZE, sizeof (local_t));
        loc->id = fetch_add (&next_index, 1);
        loc->next_store = 0;
        // force allocation on local node
        size_t table_size = sizeof (lm_store_t[map->wSize]);
        loc->table = RTalignZero (CACHE_LINE_SIZE, table_size);
        HREsetLocal (map->local_key, loc);
        map->locals[loc->id] = loc;
    }
    return loc;
}

static inline lm_store_t
lm_get_store (lm_loc_t location)
{
    return atomic_read((lm_store_t *)location);
}

static inline lm_loc_t
lm_inc_loc (lm_loc_t location)
{
    return location + sizeof(lm_store_t);
}

static inline size_t
stoi (void *p)
{
    return *(size_t*)p;
}

void
lm_unlock (lm_t *map, ref_t ref)
{
    lm_loc_t loc = (lm_loc_t)&map->table[ref];
    lm_store_t store = lm_get_store (loc);
    store.lock = 0;
    atomic_write ((size_t*)loc, stoi(&store));
}

void
lm_lock (lm_t *map, ref_t ref)
{
    lm_loc_t loc = (lm_loc_t)&map->table[ref];
    int result = 0;
    lm_store_t store;
    do {
        store = lm_get_store (loc);
        size_t old = stoi(&store);
        if (1 == store.lock) continue;
        store.lock = 1;
        result = cas ((size_t*)loc, old, stoi(&store));
    } while (!result);
}

int
lm_try_lock (lm_t *map, ref_t ref)
{
    lm_loc_t loc = (lm_loc_t)&map->table[ref];
    lm_store_t store = lm_get_store (loc);
    if (1 == store.lock)
        return false;
    lm_lock (map, ref);
    return true;
}

/**
 * An item may be moved from an end position to a new store.
 * In this case it will be at the location of the reference.
 */
static inline lm_loc_t
follow (lm_loc_t location)
{
    lm_store_t store = lm_get_store (location);
    if (LM_STATUS_REF == store.internal)
        return (lm_loc_t)store.lattice;
    return location;
}

static inline void
lm_set_all (lm_loc_t location, lm_store_t store, lattice_t l,
            lm_status_t status, lm_internal_t internal)
{
    store.lattice = l;
    store.status = status;
    store.internal = internal;
    atomic_write ((size_t *)location, stoi(&store));
}

static inline void
lm_set_int (lm_loc_t location, lm_internal_t internal)
{
    lm_store_t store = lm_get_store (location);
    store.internal = internal;
    atomic_write ((size_t *)location, stoi(&store));
}

static inline void
lm_set_int2 (lm_loc_t location, lm_store_t *store, lm_internal_t internal)
{
    store->internal = internal;
    atomic_write ((size_t *)location, stoi(store));
}

/**
 * Begin thread-local !sequential! code
 */
static void
deallocate_last_block (lm_t *map)
{
    local_t *local = get_local (map);
    local->next_store -= map->block_size;
    lm_loc_t loc = (lm_loc_t)&local->table[local->next_store];
    for (size_t i = 0; i< map->block_size; i++) {
        lm_set_int (loc, LM_STATUS_EMPTY);
        loc = lm_inc_loc(loc);
    }
}

static lm_loc_t
allocate_block (lm_t *map)
{
    local_t *local = get_local (map);
    lm_loc_t next = local->next_store;
    local->next_store += map->block_size;
    size_t end = local->next_store - 1;
    HREassert (end < map->wSize, "Lattice map allocator overflow, enlarge LM_FACTOR.");
    lm_loc_t end_loc = (lm_loc_t)&local->table[end];
    lm_store_t block_end = lm_get_store (end_loc);
    HREassert (block_end.internal == LM_STATUS_EMPTY, "Block not initialized to LM_STATUS_EMPTY");
    lm_set_int (end_loc, LM_STATUS_END);
    return (lm_loc_t)&local->table[next];
}
/**
 * End thread-local !sequential! code
 */

lm_loc_t
lm_iterate_from (lm_t *map, ref_t k, lm_loc_t *start,
                 lm_iterate_f cb, void *ctx)
{
    lm_loc_t loc = NULL == start ?  (lm_loc_t)&map->table[k] : *start;
    lm_cb_t res;
    while (true) {
        lm_store_t store = lm_get_store (loc);
        switch (store.internal) {
        case LM_STATUS_TOMBSTONE_END:
        case LM_STATUS_EMPTY:
        case LM_STATUS_END:
            return loc;
        case LM_STATUS_TOMBSTONE:
            loc = lm_inc_loc (loc);
            break;
        case LM_STATUS_REF:
            loc = follow (loc);
            break;
        case LM_STATUS_LATTICE:
            res = cb (ctx, store.lattice, store.status, loc);
            if (LM_CB_STOP == res)
                return loc;
            loc = lm_inc_loc (loc);
            break;
        case LM_STATUS_LATTICE_END:
            cb (ctx, store.lattice, store.status, loc);
            return loc;
        default:
            Abort ("Unknown status in lattice map iterate.");
        }
    }
}

static inline int
lm_cas_all (lm_loc_t location, lm_store_t old, lattice_t l,
            lm_status_t status, lm_internal_t internal)
{
    lm_store_t store_new = old;
    store_new.lattice = l;
    store_new.status = status;
    store_new.internal = internal;
    return cas ((size_t *)location, stoi(&old), stoi(&store_new));
}

lm_loc_t
lm_insert_from_cas (lm_t *map, ref_t k, lattice_t l,
                    lm_status_t status, lm_loc_t *start)
{
    HREassert (l < 1UL << LATTICE_BITS, "Lattice pointer does not fit in store!");
    HREassert (k < map->size, "Lattice map size does not match |ref_t|.");
    lm_loc_t loc = NULL == start ?  (lm_loc_t)&map->table[k] : *start;
    lm_store_t store = lm_get_store (loc);
    lm_store_t *s_loc = (lm_store_t *)loc;
    if (map->table <= s_loc && s_loc < map->table_end && LM_STATUS_EMPTY == store.internal)
        lm_set_int2 (loc, &store, LM_STATUS_END); //this table part is a map!
        // can be done with write, since all workers will do this first on this location
    lm_loc_t next;
    while (true) {
        switch (store.internal) {
        case LM_STATUS_TOMBSTONE:
        case LM_STATUS_EMPTY:           // insert
            if (lm_cas_all(loc, store, l, status, LM_STATUS_LATTICE))
                return loc;
            break; // do reread
        case LM_STATUS_TOMBSTONE_END:
        case LM_STATUS_END:             // insert
            if (lm_cas_all(loc, store, l, status, LM_STATUS_LATTICE_END))
                return loc;
            break; // do reread
        case LM_STATUS_REF:             // follow
            loc = follow (loc); // never changes!
            break;
        case LM_STATUS_LATTICE:         // next
            loc = lm_inc_loc (loc);
            break;
        case LM_STATUS_LATTICE_END:     // next block, move store and set ref
            next = allocate_block (map);
            lm_set_all (next, store, store.lattice, store.status, LM_STATUS_LATTICE);
            if (!lm_cas_all(loc, store, (lattice_t)next, 0, LM_STATUS_REF)) {
                deallocate_last_block (map);
                break; // do reread
            }
            loc = next;
            break;
            // this is the only place where we allow the replacement of an element.
        default:
            Abort ("Unknown status in lattice map insert.");
        }
        store = lm_get_store (loc); //reread
    }
}

lm_loc_t
lm_insert_from (lm_t *map, ref_t k, lattice_t l,
                lm_status_t status, lm_loc_t *start)
{
    HREassert (l < 1UL << LATTICE_BITS, "Lattice pointer does not fit in store!");
    HREassert (k < map->size, "Lattice map size does not match |ref_t|.");
    lm_loc_t loc = NULL == start ?  (lm_loc_t)&map->table[k] : *start;
    lm_store_t store = lm_get_store (loc);
    lm_store_t *s_loc = (lm_store_t *)loc;
    if (map->table <= s_loc && s_loc < map->table_end && LM_STATUS_EMPTY == store.internal)
        lm_set_int2 (loc, &store, LM_STATUS_END); //this table part is a map!
    lm_loc_t next;
    while (true) {
        switch (store.internal) {
        case LM_STATUS_TOMBSTONE:
        case LM_STATUS_EMPTY:           // insert
            lm_set_all (loc, store, l, status, LM_STATUS_LATTICE);
            return loc;
        case LM_STATUS_TOMBSTONE_END:
        case LM_STATUS_END:             // insert
            lm_set_all (loc, store, l, status, LM_STATUS_LATTICE_END);
            return loc;
        case LM_STATUS_REF:             // follow
            loc = follow (loc);
            break;
        case LM_STATUS_LATTICE:         // next
            loc = lm_inc_loc (loc);
            break;
        case LM_STATUS_LATTICE_END:     // next block, move store and set ref
            next = allocate_block (map);
            next = lm_insert_from (map, k, store.lattice, store.status, &next); // k is ignored here!
            lm_set_all (loc, store, (lattice_t)next, 0, LM_STATUS_REF); // ref!
            return lm_insert_from (map, k, l, status, &next);
            // this is the only place where we allow the replacement of an element.
        default:
            Abort ("Unknown status in lattice map insert.");
        }
        store = lm_get_store (loc);
    }
}

lattice_t
lm_get (lm_t *map, lm_loc_t location)
{
    location = follow (location);
    lm_store_t store = lm_get_store (location);
    switch (store.internal) {
    case LM_STATUS_LATTICE:
    case LM_STATUS_LATTICE_END:
        return store.lattice;
    case LM_STATUS_TOMBSTONE:
    case LM_STATUS_TOMBSTONE_END:
        return LM_NULL_LATTICE;
    default:
        Abort ("Lattice map get on empty store!.");
    }
    (void) map;
}

lm_status_t
lm_get_status (lm_t *map, lm_loc_t location)
{
    location = follow (location);
    lm_store_t store = lm_get_store (location);
    switch (store.internal) {
    case LM_STATUS_LATTICE:
    case LM_STATUS_LATTICE_END:
        return store.status;
    default:
        Abort ("Lattice map get_status on empty store!.");
    }
    (void) map;
}

void
lm_set_status (lm_t *map, lm_loc_t location, lm_status_t status)
{
    location = follow (location);
    HREassert (status < (1ULL << 3), "Only 3 status bits are reserved.");
    lm_store_t store = lm_get_store (location);
    switch (store.internal) {
    case LM_STATUS_LATTICE:
    case LM_STATUS_LATTICE_END:
        store.status = status;
        atomic_write ((size_t*)location, stoi(&store));
        break;
    default:
        Abort("Lattice map set status on empty store!.");
    }
    (void) map;
}

int
lm_cas_update (lm_t *map, lm_loc_t location,
               lattice_t l_old, lm_status_t status_old,
               lattice_t l, lm_status_t status)
{
    location = follow (location);
    lm_store_t store = lm_get_store (location);
    if (store.lattice != l_old || store.status != status_old)
        return false;
    lm_store_t store_new = store;
    store_new.lattice = l;
    store_new.status = status;
    switch (store.internal) {
    case LM_STATUS_LATTICE:
    case LM_STATUS_LATTICE_END:
        return cas ((size_t*)location, stoi(&store), stoi(&store_new));
    default:
        return false;
    }
    (void) map;
}

void
lm_cas_delete (lm_t *map, lm_loc_t location, lattice_t l, lm_status_t status)
{
    location = follow (location);
    lm_store_t store = lm_get_store (location);
    if (store.lattice != l || store.status != status)
        return;
    lm_store_t store_new = store;
    switch (store.internal) {
    case LM_STATUS_LATTICE:
        store_new.internal = LM_STATUS_TOMBSTONE;
        cas ((size_t*)location, stoi(&store), stoi(&store_new));
        return;
    case LM_STATUS_LATTICE_END:
        store_new.internal = LM_STATUS_TOMBSTONE_END;
        cas ((size_t*)location, stoi(&store), stoi(&store_new));
        return;
    default:
        return;
    }
    (void) map;
}

void
lm_delete (lm_t *map, lm_loc_t location)
{
    location = follow (location);
    lm_store_t store = lm_get_store (location);
    switch (store.internal) {
    case LM_STATUS_LATTICE:
        lm_set_int2 (location, &store, LM_STATUS_TOMBSTONE);
        break;
    case LM_STATUS_LATTICE_END:
        lm_set_int2 (location, &store, LM_STATUS_TOMBSTONE_END);
        break;
    default:
        Abort ("Deleting non-lattice from lattice map.");
    }
    (void) map;
}

lm_loc_t
lm_insert (lm_t *map, ref_t k, lattice_t l, lm_status_t status)
{
    return lm_insert_from (map, k, l, status, NULL);
}

lm_loc_t
lm_iterate (lm_t *map, ref_t k, lm_iterate_f cb, void *ctx)
{
    return lm_iterate_from (map, k, NULL, cb, ctx);
}

lm_t *
lm_create (size_t workers, size_t size, size_t block_size)
{
    HREassert (block_size >= 2 && block_size < size * LM_FACTOR, "Wrong block size");
    lm_t           *map = RTalignZero (CACHE_LINE_SIZE, sizeof (struct lm_s));
    map->workers = workers;
    map->size = size;
    map->block_size = block_size;
    size_t allocator_mem = map->size * LM_FACTOR;
    size_t nblocks_per_worker = allocator_mem / (map->block_size * map->workers);
    map->wSize = nblocks_per_worker * map->block_size;
    allocator_mem = nblocks_per_worker * map->block_size * map->workers;
    size_t table_size = sizeof (lm_store_t[map->size]);
    map->table = RTalignZero (CACHE_LINE_SIZE, table_size);
    map->table_end = map->table + map->size;
    if (NULL == map->table) Abort("Allocation failed for lmap table of %zuGB", table_size>>30);
    HREcreateLocal (&map->local_key, NULL);
    return map;
}

void
lm_free (lm_t *map)
{
    RTalignedFree (map->table);
    for (size_t i = 0; i < map->workers; i++)
        if (NULL != map->locals[i]) RTalignedFree (map->locals[i]);
    RTalignedFree (map);
}

size_t
lm_allocated (lm_t *map)
{
    size_t total = 0;
    for (size_t i = 0; i < map->workers; i++)
        if (NULL != map->locals[i])
            total += map->locals[i]->next_store;
    return total;
}
