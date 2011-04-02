#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <runtime.h>
#include <dbs-ll.h>


static const int        TABLE_SIZE = 24;
static const uint32_t   EMPTY = 0;
static const uint32_t   WRITE_BIT = 1 << 31;
static const uint32_t   WRITE_BIT_R = ~(1 << 31);
static const uint32_t   BITS_PER_INT = sizeof (int) * 8;
static const size_t     CL_MASK = -(1 << CACHE_LINE);

struct dbs_ll_s {
    size_t              length;
    size_t              bytes;
    size_t              size;
    size_t              threshold;
    uint32_t            mask;
    int                *data;
    uint32_t           *table;
    hash32_f            hash32;
    pthread_key_t       local_key;
};

typedef struct local_s {
    stats_t             stat;
} local_t;

local_t *
get_local (dbs_ll_t dbs)
{
    local_t            *loc = pthread_getspecific (dbs->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (local_t));
        memset (loc, 0, sizeof (local_t));
        pthread_setspecific (dbs->local_key, loc);
    }
    return loc;
}

uint32_t
DBSLLmemoized_hash (const dbs_ll_t dbs, const int idx)
{
    return dbs->table[idx];
}

int
DBSLLlookup_hash (const dbs_ll_t dbs, const int *v, int *ret, uint32_t *hash)
{
    local_t            *loc = get_local (dbs);
    stats_t            *stat = &loc->stat;
    size_t              seed = 0;
    size_t              l = dbs->length;
    size_t              b = dbs->bytes;
    uint32_t            hash_rehash = hash ? *hash : dbs->hash32 ((char *)v, b, 0);
    uint32_t            hash_memo = hash_rehash;
    //avoid collision of memoized hash with reserved values EMPTY and WRITE_BIT
    while (EMPTY == hash_memo || WRITE_BIT == hash_memo)
        hash_memo = dbs->hash32 ((char *)v, b, ++seed);
    uint32_t            WAIT = hash_memo & WRITE_BIT_R;
    uint32_t            DONE = hash_memo | WRITE_BIT;
    while (seed < dbs->threshold) {
        size_t              idx = hash_rehash & dbs->mask;
        size_t              line_end = (idx & CL_MASK) + CACHE_LINE_SIZE;
        for (size_t i = 0; i < CACHE_LINE_SIZE; i++) {
            uint32_t           *bucket = &dbs->table[idx];
            if (EMPTY == *bucket) {
                if (cas (bucket, EMPTY, WAIT)) {
                    memcpy (&dbs->data[idx * l], v, b);
                    atomic_write (bucket, DONE);
                    stat->elts++;
                    *ret = idx;
                    return 0;
                }
            }
            if (DONE == (atomic_read (bucket) | WRITE_BIT)) {
                while (WAIT == atomic_read (bucket)) {}
                if (0 == memcmp (&dbs->data[idx * l], v, b)) {
                    *ret = idx;
                    return 1;
                }
                stat->misses++;
            }
            idx += 1;
            idx = idx == line_end ? line_end - CACHE_LINE_SIZE : idx;
        }
        hash_rehash = dbs->hash32 ((char *)v, b, hash_rehash + (seed++));
        stat->rehashes++;
    }
    Fatal(1, error, "Hash table full"); 
}

int *
DBSLLget (const dbs_ll_t dbs, const int idx, int *dst)
{
    *dst = dbs->table[idx];
    return &dbs->data[idx * dbs->length];
}

int
DBSLLlookup_ret (const dbs_ll_t dbs, const int *v, int *ret)
{
    return DBSLLlookup_hash (dbs, v, ret, NULL);
}

int
DBSLLlookup (const dbs_ll_t dbs, const int *vector)
{
    int                *ret = RTmalloc (sizeof (*ret));
    DBSLLlookup_hash (dbs, vector, ret, NULL);
    return *ret;
}

dbs_ll_t
DBSLLcreate (int length)
{
    return DBSLLcreate_sized (length, TABLE_SIZE, (hash32_f)SuperFastHash);
}

dbs_ll_t
DBSLLcreate_sized (int length, int size, hash32_f hash32)
{
    dbs_ll_t            dbs = RTalign (CACHE_LINE_SIZE, sizeof (struct dbs_ll_s));
    dbs->length = length;
    dbs->hash32 = hash32;
    dbs->bytes = length * sizeof (int);
    dbs->size = 1 << size;
    dbs->threshold = dbs->size / 100;
    dbs->mask = dbs->size - 1;
    dbs->table = RTalign (CACHE_LINE_SIZE, sizeof (uint32_t[dbs->size]));
    dbs->data = RTalign (CACHE_LINE_SIZE, sizeof (int[dbs->size * length]));
    memset (dbs->table, 0, sizeof (uint32_t[dbs->size]));
    pthread_key_create (&dbs->local_key, RTfree);
    return dbs;
}

void
DBSLLfree (dbs_ll_t dbs)
{
    RTfree (dbs->data);
    RTfree (dbs->table);
    RTfree (dbs);
}

stats_t *
DBSLLstats (dbs_ll_t dbs)
{
    stats_t            *res = RTmalloc (sizeof (*res));
    stats_t            *stat = &get_local (dbs)->stat;
    memcpy (res, stat, sizeof (*res));
    return res;
}
