#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <runtime.h>
#include <lattice-map.h>

/**
 * Internally this multimap does sequential probing within a certain block
 * (cyclic). After that it rehashes to another block.
 */
static const size_t     BLOCK_SIZE = 16; // nr of stores in a block
static const size_t     BLOCK_LOG2 = 4; // log2 BLOCK_SIZE
static const size_t     BLOCK_MASK = -16; // Mask to get index in block

struct lmap_ll_s {
    size_t              size;
    size_t              length;
    size_t              key_size;
    size_t              data_size;
    size_t              mask;
    lmap_store_t       *table;
    hash32_f            hash32;
    pthread_key_t       local_key;
};

typedef struct local_s {
    stats_t             stat;
} local_t;

/**
 *
 */
typedef struct lmap_loc_s {
    ref_t               ref  : 48;
    ref_t               loop : 16;
} lmap_loc_int_t;

static inline lmap_loc_t
i2e (lmap_loc_int_t loc)
{
    return *(lmap_loc_t*)&loc;
}

static inline lmap_loc_int_t
e2i (lmap_loc_t loc)
{
    return *(lmap_loc_int_t*)&loc;
}


static inline local_t *
get_local (lmap_t *map)
{
    local_t            *loc = pthread_getspecific (map->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (local_t));
        memset (loc, 0, sizeof (local_t));
        pthread_setspecific (map->local_key, loc);
    }
    return loc;
}

static inline lmap_loc_int_t
get_location (const lmap_t *map, lmap_loc_t *start, ref_t k)
{
    lmap_loc_int_t loc = {.loop = 0, .ref = 0};
    if (start)
        loc = e2i(*start);
    else
        loc.ref = map->hash32 ((char*)&k, sizeof (ref_t), 0) & map->mask;
    return loc;
}

static inline ref_t
rehash (ref_t h, ref_t v)
{
    return h + (primes[v & 511]<<BLOCK_LOG2);
}

lmap_loc_t
lmap_iterate_from (const lmap_t *map, ref_t k, lmap_loc_t *start,
                   lmap_iterate_f cb, void *ctx)
{
    lmap_loc_int_t          loc = get_location (map, start, k);
    while (1) {
        size_t              line_begin = ((uint64_t)loc.ref) & BLOCK_MASK;
        for (; loc.loop < BLOCK_SIZE; loc.loop++) {
            lmap_store_t        *bucket = &map->table[loc.ref];
            if (LMAP_STATUS_EMPTY == bucket->status)
                return i2e(loc);
            loc.ref = ((loc.ref + 1) & ~BLOCK_MASK) | line_begin;
            if (    LMAP_STATUS_TOMBSTONE != bucket->status &&
                    k == bucket->ref) {
                if (LMAP_CB_STOP == cb (ctx, bucket, i2e(loc))) {
                    return i2e (loc);
                }
            }
        }
        loc.ref = rehash (loc.ref, k) & map->mask;
        loc.loop = 0;
    }
}

lmap_loc_t
lmap_lookup (const lmap_t *map, ref_t k, lattice_t l)
{
    lmap_loc_int_t          loc = get_location (map, NULL, k);
    while (1) {
        size_t              line_begin = ((uint64_t)loc.ref) & BLOCK_MASK;
        for (; loc.loop < BLOCK_SIZE; loc.loop++) {
            lmap_store_t        *bucket = &map->table[loc.ref];
            if (LMAP_STATUS_EMPTY == bucket->status)
                return i2e (loc);
            if (    LMAP_STATUS_TOMBSTONE != bucket->status &&
                    k == bucket->ref && l == bucket->lattice)
                return i2e (loc);
            loc.ref = ((loc.ref + 1) & ~BLOCK_MASK) | line_begin;
        }
        loc.ref = rehash (loc.ref, k) & map->mask;
        loc.loop = 0;
    }
}

lmap_loc_t
lmap_insert_from (const lmap_t *map, ref_t k, lattice_t l,
                  lmap_status_t status, lmap_loc_t *start)
{
    lmap_loc_int_t          loc = get_location (map, start, k);
    while (1) {
        size_t              block = ((uint64_t)loc.ref) & BLOCK_MASK;
        for (; loc.loop < BLOCK_SIZE; loc.loop++) {
            lmap_store_t        *bucket = &map->table[loc.ref];
            if (    LMAP_STATUS_EMPTY == bucket->status ||
                    LMAP_STATUS_TOMBSTONE == bucket->status) {
                bucket->ref = k;
                bucket->status = status;
                bucket->lattice = l;
                return i2e (loc);
            }
            loc.ref = ((loc.ref + 1) & ~BLOCK_MASK) | block;
        }
        loc.ref = rehash (loc.ref, k) & map->mask;
        loc.loop = 0;
    }
}

lmap_store_t *
lmap_get (const lmap_t *map, lmap_loc_t location)
{
    lmap_loc_int_t       loc = e2i (location);
    return &map->table[loc.ref];
}

void
lmap_set (const lmap_t *map, lmap_loc_t location, lmap_status_t status)
{
    lmap_loc_int_t       loc = e2i (location);
    map->table[loc.ref].status = status;
}

void
lmap_delete (const lmap_t *map, lmap_loc_t location)
{
    lmap_loc_int_t       loc = e2i (location);
    map->table[loc.ref].status = LMAP_STATUS_TOMBSTONE;
}

lmap_loc_t
lmap_insert (const lmap_t *map, ref_t k, lattice_t l, lmap_status_t status)
{
    return lmap_insert_from (map, k, l, status, NULL);
}

lmap_loc_t
lmap_iterate (const lmap_t *map, ref_t k, lmap_iterate_f cb, void *ctx)
{
    return lmap_iterate_from (map, k, NULL, cb, ctx);
}

lmap_t *
lmap_create (size_t key_size, size_t data_size, int size)
{
    lmap_loc_int_t t1 = { .loop =0, .ref = 1 };
    ref_t t2 = 1;
    assert ( t2 == *(ref_t*)&t1 );
    assert (63 == key_size && 64 == data_size);
    lmap_t           *map = RTalign (CACHE_LINE_SIZE, sizeof (struct lmap_ll_s));
    map->key_size = sizeof (uint64_t);
    map->data_size = sizeof (uint64_t);
    map->length = map->key_size + map->data_size;
    map->size = 1UL << size;
    map->mask = map->size - 1;
    map->hash32 = (hash32_f)SuperFastHash;
    map->table = RTalignZero (CACHE_LINE_SIZE, sizeof(lmap_store_t[map->size]));
    pthread_key_create (&map->local_key, RTfree);
    return map;
}

void
lmap_free (lmap_t *map)
{
    RTfree (map->table);
    RTfree (map);
}

stats_t *
lmap_stats (lmap_t *map)
{
    stats_t            *res = RTmallocZero (sizeof (*res));
    stats_t            *stat = &get_local (map)->stat;
    memcpy (res, stat, sizeof (*res));
    return res;
}
