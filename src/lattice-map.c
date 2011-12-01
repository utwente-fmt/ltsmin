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


static const size_t     CL_MASK = -(1UL << CACHE_LINE);

struct lmap_ll_s {
    size_t              size;
    size_t              length;
    size_t              key_size;
    size_t              data_size;
    uint32_t            mask;
    char               *table;
    hash32_f            hash32;
    pthread_key_t       local_key;
};

typedef struct local_s {
    stats_t             stat;
} local_t;


typedef struct lmap_loc_s {
    ref_t               loop : 16;
    ref_t               ref  : 48;
} lmap_loc_int_t;

static inline local_t *
get_local (lmap_t map)
{
    local_t            *loc = pthread_getspecific (map->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (local_t));
        memset (loc, 0, sizeof (local_t));
        pthread_setspecific (map->local_key, loc);
    }
    return loc;
}

lmap_loc_t
lmap_iterate_hash (const lmap_t map, const ref_t k, lmap_loc_t *start,
                   lmap_iterate_f cb, void *ctx)
{
    //local_t            *loc = get_local (map);
    //stats_t            *stat = &loc->stat;
    //size_t              seed = 0;
    lmap_loc_int_t              loc;
    if (start)
        loc = *(lmap_loc_int_t*)start;
    else
        loc.ref = map->hash32 ((char *)&k, sizeof(ref_t), 0) & map->mask;
    while (1) {
        size_t              line_end = (loc.ref & CL_MASK) + CACHE_LINE_SIZE;
        for (; loc.loop < CACHE_LINE_SIZE; loc.loop++) {
            lmap_store_t        *bucket = (lmap_store_t*)map->table + loc.ref * map->length;
            if (LMAP_STATUS_EMPTY == bucket->status)
                return *(lmap_loc_t*)&loc;
            loc.ref += 1;
            loc.ref = loc.ref == line_end ? line_end - CACHE_LINE_SIZE : loc.ref;
            if ((LMAP_STATUS_OCCUPIED1 == bucket->status || LMAP_STATUS_OCCUPIED2 == bucket->status) &&
                    k == bucket->ref &&
                    LMAP_CB_STOP == cb (ctx, bucket, *(lmap_loc_t*)&loc)) {
                return *(lmap_loc_t*)&loc;
            }
        }
        loc.ref = map->hash32 ((char *)&k, map->length, loc.ref);
        loc.loop = 0;
    }
}

lmap_status_t
lmap_lookup (const lmap_t map, const ref_t k, lattice_t l)
{
    uint64_t            h = map->hash32 ((char *)&k, sizeof(ref_t), 0);
    while (1) {
        size_t              ref = h & map->mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_SIZE;
        for (size_t i = 0; i < CACHE_LINE_SIZE; i++) {
            lmap_store_t        *bucket = (lmap_store_t*)&map->table[ref * map->length];
            if ((LMAP_STATUS_OCCUPIED1 == bucket->status || LMAP_STATUS_OCCUPIED2 == bucket->status)
                    && k == bucket->ref && l == bucket->lattice)
                return bucket->status;
            ref += 1;
            ref = ref == line_end ? line_end - CACHE_LINE_SIZE : ref;
        }
        h = map->hash32 ((char *)&k, map->length, line_end);
    }
}

lmap_loc_t
lmap_insert_hash (const lmap_t map, const ref_t k, lattice_t l,
                lmap_status_t status, lmap_loc_t *start)
{
    lmap_loc_int_t              loc;
    if (start)
        loc = *(lmap_loc_int_t*)start;
    else
        loc.ref = map->hash32 ((char *)&k, sizeof(ref_t), 0) & map->mask;
    while (1) {
        size_t              line_end = (loc.ref & CL_MASK) + CACHE_LINE_SIZE;
        for (; loc.loop < CACHE_LINE_SIZE; loc.loop++) {
            lmap_store_t        *bucket = (lmap_store_t*)&map->table[loc.ref * map->length];
            loc.ref += 1;
            loc.ref = loc.ref == line_end ? line_end - CACHE_LINE_SIZE : loc.ref;
            if (LMAP_STATUS_EMPTY == bucket->status || LMAP_STATUS_TOMBSTONE == bucket->status) {
                bucket->ref = k;
                bucket->status = status;
                bucket->lattice = l;
                return *(lmap_loc_t*)&loc;
            }
        }
        loc.ref = map->hash32 ((char *)&k, map->length, loc.ref);
        loc.loop = 0;
    }
}

lmap_status_t
lmap_get (const lmap_t map, lmap_loc_t start)
{
    lmap_loc_int_t              loc = *(lmap_loc_int_t*)&start;
    lmap_store_t        *bucket = (lmap_store_t*)&map->table[loc.ref * map->length];
    return bucket->status;
}

void
lmap_set (const lmap_t map, lmap_status_t status, lmap_loc_t start)
{
    lmap_loc_int_t              loc = *(lmap_loc_int_t*)&start;
    lmap_store_t        *bucket = (lmap_store_t*)&map->table[loc.ref * map->length];
    bucket->status = status;
}

lmap_loc_t
lmap_insert (const lmap_t map, ref_t k, lattice_t l, lmap_status_t status)
{
    return lmap_insert_hash (map, k, l, status, NULL);
}

lmap_loc_t
lmap_iterate (const lmap_t map, ref_t k, lmap_iterate_f cb, void *ctx)
{
    return lmap_iterate_hash (map, k, NULL, cb, ctx);
}

lmap_t
lmap_create (size_t key_size, size_t data_size, int size)
{
    assert (63 == key_size && 64 == data_size);
    lmap_t            map = RTalign (CACHE_LINE_SIZE, sizeof (struct lmap_ll_s));
    map->key_size = sizeof (uint64_t);
    map->data_size = sizeof (uint64_t);
    map->length = map->key_size + map->data_size;
    map->size = 1UL << size;
    map->mask = (map->size * map->length) - 1;
    map->hash32 = (hash32_f)SuperFastHash;
    map->table = RTalignZero (CACHE_LINE_SIZE, (map->length) * map->size);
    pthread_key_create (&map->local_key, RTfree);
    return map;
}

void
lmap_free (lmap_t map)
{
    RTfree (map->table);
    RTfree (map);
}

stats_t *
lmap_stats (lmap_t map)
{
    stats_t            *res = RTmallocZero (sizeof (*res));
    stats_t            *stat = &get_local (map)->stat;
    memcpy (res, stat, sizeof (*res));
    return res;
}
