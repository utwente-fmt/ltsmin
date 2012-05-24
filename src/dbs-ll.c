#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <atomics.h>
#include <dbs-ll.h>
#include <runtime.h>


static const int        TABLE_SIZE = 24;
static const uint16_t   EMPTY = 0;
static uint16_t         WRITE_BIT = 1;
static uint16_t         WRITE_BIT_R = ~((uint32_t)1);
static const size_t     CL_MASK = -(1UL << CACHE_LINE);

struct dbs_ll_s {
    size_t              length;
    size_t              sat_bits;
    size_t              bytes;
    size_t              size;
    size_t              threshold;
    uint32_t            mask;
    int                *data;
    uint16_t           *table;
    uint16_t            sat_mask;
    hash32_f            hash32;
    int                 full;
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

uint16_t
DBSLLget_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref)
{
    return atomic_read (dbs->table+ref) & dbs->sat_mask;
}

int
DBSLLget_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref, int index)
{
    uint16_t        bit = 1U << index;
    uint16_t        hash_and_sat = atomic_read (dbs->table+ref);
    uint16_t        val = hash_and_sat & bit;
    return val >> index;
}

void
DBSLLunset_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref, int index)
{
    uint16_t        bit = 1U << index;
    uint16_t        hash_and_sat = atomic_read (dbs->table+ref);
    uint16_t        val = hash_and_sat & ~bit;
    atomic_write (dbs->table+ref, val);
}

int
DBSLLtry_set_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref, int index)
{
    uint16_t        bit = 1U << index;
    do {
        uint16_t        hash_and_sat = atomic_read (dbs->table+ref);
        uint16_t        val = hash_and_sat & bit;
        if (val)
            return 0; // bit was already set
        if (cas(dbs->table+ref, hash_and_sat, hash_and_sat | bit))
            return 1; // success
    } while ( 1 ); // another bit was set
}

int
DBSLLtry_unset_sat_bit (const dbs_ll_t dbs, const dbs_ref_t ref, int index)
{
    uint16_t        bit = (1U << index);
    do {
        uint16_t        hash_and_sat = atomic_read (dbs->table+ref);
        uint16_t        val = hash_and_sat & bit;
        if (!val)
            return 0; // bit was already set
        if (cas(dbs->table+ref, hash_and_sat, hash_and_sat & ~bit))
            return 1; // success
    } while ( 1 ); // another bit was set
}

uint16_t
DBSLLinc_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref)
{
    uint16_t        val, newval;
    do {
        val = atomic_read (dbs->table+ref);
        assert ((val & dbs->sat_mask) != dbs->sat_mask);
        newval = val + 1;
    } while ( ! cas (dbs->table+ref, val, newval) );
    return newval;
}

uint16_t
DBSLLdec_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref)
{
    uint16_t        val, newval;
    do {
        val = atomic_read (dbs->table+ref);
        assert ((val & dbs->sat_mask) != 0);
        newval = val - 1;
    } while ( ! cas (dbs->table+ref, val, newval) );
    return newval;
}

void
DBSLLset_sat_bits (const dbs_ll_t dbs, const dbs_ref_t ref, uint16_t value)
{
    uint16_t        hash = dbs->table[ref] & ~dbs->sat_mask;
    atomic_write (dbs->table+ref, hash | (value & dbs->sat_mask));
}

uint16_t
DBSLLmemoized_hash (const dbs_ll_t dbs, const dbs_ref_t ref)
{
    return dbs->table[ref] & ~dbs->sat_mask;
}

int
DBSLLlookup_hash (const dbs_ll_t dbs, const int *v, dbs_ref_t *ret, uint32_t *hash)
{
    local_t            *loc = get_local (dbs);
    stats_t            *stat = &loc->stat;
    size_t              seed = 0;
    size_t              l = dbs->length;
    size_t              b = dbs->bytes;
    uint32_t            hash_rehash = hash ? *hash : dbs->hash32 ((char *)v, b, 0);
    uint16_t            hash_memo = (hash_rehash >> 16) & ~dbs->sat_mask;
    //avoid collision of memoized hash with reserved values EMPTY and WRITE_BIT
    uint32_t            prime = primes[hash_rehash & PRIME_MASK];
    while (EMPTY == hash_memo || WRITE_BIT == hash_memo)
        hash_memo = (hash_memo + (prime << dbs->sat_bits)) & ~dbs->sat_mask;
    uint16_t            WAIT = hash_memo & WRITE_BIT_R;
    uint16_t            DONE = hash_memo | WRITE_BIT;
    while (seed < dbs->threshold && !atomic_read(&dbs->full)) {
        size_t              ref = hash_rehash & dbs->mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_SIZE;
        for (size_t i = 0; i < CACHE_LINE_SIZE; i++) {
            uint16_t           *bucket = &dbs->table[ref];
            if (EMPTY == *bucket) {
                if (cas (bucket, EMPTY, WAIT)) {
                    memcpy (&dbs->data[ref * l], v, b);
                    atomic_write (bucket, DONE);
                    stat->elts++;
                    *ret = ref;
                    return 0;
                }
            }
            if (DONE == ((atomic_read (bucket) | WRITE_BIT) & ~dbs->sat_mask)) {
                while (WAIT == (atomic_read (bucket) & ~dbs->sat_mask)) {}
                if (0 == memcmp (&dbs->data[ref * l], v, b)) {
                    *ret = ref;
                    return 1;
                }
                stat->misses++;
            }
            ref += 1;
            ref = ref == line_end ? line_end - CACHE_LINE_SIZE : ref;
        }
        hash_rehash += prime << CACHE_LINE;
        stat->rehashes++;
    }
    if ( cas (&dbs->full, 0, 1) ) {
        kill(0, SIGINT);
        Warning(info, "ERROR: Hash table full (size: %zu el)", dbs->size);
    }
    *ret = 0; //incorrect, but does not matter anymore
    return 1;
}

int *
DBSLLget (const dbs_ll_t dbs, const dbs_ref_t ref, int *dst)
{
    return &dbs->data[ref * dbs->length];
    (void) dst;
}

int
DBSLLlookup_ret (const dbs_ll_t dbs, const int *v, dbs_ref_t *ret)
{
    return DBSLLlookup_hash (dbs, v, ret, NULL);
}

dbs_ref_t
DBSLLlookup (const dbs_ll_t dbs, const int *vector)
{
    dbs_ref_t             ret;
    DBSLLlookup_hash (dbs, vector, &ret, NULL);
    return ret;
}

dbs_ll_t
DBSLLcreate (int length)
{
    return DBSLLcreate_sized (length, TABLE_SIZE, (hash32_f)SuperFastHash, 0);
}

dbs_ll_t
DBSLLcreate_sized (int length, int size, hash32_f hash32, int satellite_bits)
{
    dbs_ll_t            dbs = RTalign (CACHE_LINE_SIZE, sizeof (struct dbs_ll_s));
    dbs->length = length;
    dbs->hash32 = hash32;
    dbs->full = 0;
    assert(satellite_bits < 32);
    dbs->sat_bits = satellite_bits;
    dbs->sat_mask = (1UL<<satellite_bits) - 1;
    WRITE_BIT <<= satellite_bits;
    WRITE_BIT_R <<= satellite_bits;
    dbs->bytes = length * sizeof (int);
    dbs->size = 1UL << size;
    dbs->threshold = dbs->size / 100;
    dbs->mask = dbs->size - 1;
    dbs->table = RTalignZero (CACHE_LINE_SIZE, sizeof (uint16_t[dbs->size]));
    dbs->data = RTalign (CACHE_LINE_SIZE, sizeof (int[dbs->size * length]));
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
    stats_t            *res = RTmallocZero (sizeof (*res));
    stats_t            *stat = &get_local (dbs)->stat;
    memcpy (res, stat, sizeof (*res));
    return res;
}
