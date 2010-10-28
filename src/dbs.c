#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include <runtime.h>
#include <dbs.h>
#include <fast_hash.h>

static const int BLOCKSIZE = 256;
static const int BITS_PER_INT = (int)sizeof(int)*8;

static inline int hashvalue(int left, int right) {
    return SuperFastHash(&left,sizeof left, SuperFastHash(&right,sizeof right,0));
}

typedef struct dbs_node_s *dbs_node_t;
typedef struct dbs_entry_s *dbs_entry_t;
typedef struct dbs_segment_s *dbs_segment_t;

struct dbs_s {
    int length;
    int bytelength;
    int nSegments;
    int deseg_mask;
    int shift;
    int deshift;
    tls_t *stats_tls;
    struct dbs_segment_s {
        int hash_size;
        int *hash;
        int entry_mask;
        int size;
        struct dbs_entry_s {
            int *v;
            int bucket;
        } *entries;
        int next;
        pthread_rwlock_t lock;
    } *segments;
};

int
db_insert (dbs_t dbs, int i, int *v, int *hash)
{
    dbs_segment_t segment = &dbs->segments[i];
    while (segment->next >= segment->size) {
        Warning (info, "Not implemented: resize");
        exit (1);
    }
    int hash1 = *hash & segment->entry_mask;
    assert (hash1 < segment->hash_size);
    int result = segment->next++;
    dbs_entry_t entry = &segment->entries[result];
    memcpy (entry->v, v, dbs->bytelength);
    entry->bucket = segment->hash[hash1];
    segment->hash[hash1] = result;
    stats_t *statistics = TLSgetInstanceRef(dbs->stats_tls);
    statistics->elts++;
    return result;
}

int
db_lookup (dbs_t dbs, int i, const int *v, int hash)
{
    dbs_segment_t segment = &dbs->segments[i];
    hash = hash & segment->entry_mask;
    assert (hash < segment->hash_size);
    int result = segment->hash[hash];
    stats_t *statistics = TLSgetInstanceRef(dbs->stats_tls);
    while (result >= 0) {
        dbs_entry_t entry = &segment->entries[result];
        statistics->tests++;
        if (0==memcmp(entry->v, v, dbs->bytelength))
            break;
        statistics->misses++;
        result = entry->bucket;
    }
    return result;
}

int
DBSlookup (const dbs_t dbs, const int *v)
{
    int *idx = RTmalloc (sizeof *idx);
    DBSlookup_ret (dbs, v, idx);
    return *idx;
}

int
DBSlookup_ret (const dbs_t dbs, const int *v, int *ret)
{
    int hash, i, seen = 1;
    hash = SuperFastHash (v, sizeof *v, 0);
    for (i = 1; i < dbs->length; i++) {
        hash = hashvalue (v[i], hash);
    }
    i = ((unsigned int)hash) >> dbs->shift;
    dbs_segment_t segment = &dbs->segments[i];
    pthread_rwlock_rdlock (&segment->lock);
    *ret = db_lookup (dbs, i, v, hash);
    pthread_rwlock_unlock (&segment->lock);
    if (*ret < 0) {
        pthread_rwlock_wrlock (&segment->lock);
        int resnew = db_lookup (dbs, i, v, hash);
        if (*ret != resnew) {
            stats_t *statistics = TLSgetInstanceRef(dbs->stats_tls);
            statistics->misses++;
        }
        *ret = resnew;
        if (*ret < 0) {
            *ret = db_insert (dbs, i, (int *)v, &hash);
            seen = 0;
        }
        pthread_rwlock_unlock (&segment->lock);
    }
    return seen;
}

dbs_t
DBScreate (int length)
{
    dbs_t dbs = RTmalloc (sizeof *dbs);
    dbs->length = length;
    dbs->bytelength = sizeof (int[length]);
    int hash_shift, seg_shift, hash_size, seg_size;
    hash_shift = 11;
    seg_shift = hash_shift + 1;
    hash_shift = hash_shift;
    seg_size = 1 << seg_shift;
    hash_size = 1 << hash_shift;
    assert (seg_size && ((seg_size - 1) & seg_size) == 0);
    assert (seg_shift < BITS_PER_INT);
    dbs->nSegments = seg_size;
    dbs->shift = BITS_PER_INT - seg_shift;
    dbs->deshift = seg_shift;
    dbs->deseg_mask = ~(-seg_size);
    dbs->segments = RTmalloc (sizeof (struct dbs_segment_s) * seg_size);
    dbs->stats_tls = TLScreate(dbs, sizeof(stats_t));
    for (int i = 0; i < seg_size; i++) {
        dbs_segment_t segment = &dbs->segments[i];
        pthread_rwlock_init (&segment->lock, NULL);
        segment->next = 0;
        segment->size = seg_size;
        segment->entries = RTmalloc (sizeof (struct dbs_entry_s) * seg_size);
        for (int j = 0; j < seg_size; j++)
            segment->entries[j].v = RTmalloc (sizeof (int[length]));
        segment->hash_size = hash_size;
        segment->entry_mask = ~(-hash_size);
        segment->hash = RTmalloc (sizeof (int) * hash_size);
        for (int j = 0; j < hash_size; j++)
            segment->hash[j] = -1;
    }
    return dbs;
}

void
DBSfree (dbs_t dbs)
{
    dbs_segment_t segment;
    for (int j = 0; j < dbs->nSegments; j++) {
        segment = &dbs->segments[j];
        pthread_rwlock_destroy (&segment->lock);
        RTfree (segment->hash);
        RTfree (segment->entries);
    }
    RTfree (dbs->segments);
    RTfree (dbs);
}

stats_t*
DBSstats (dbs_t dbs)
{
    stats_t *statistics = TLSgetInstanceRef(dbs->stats_tls);
    stats_t *stat = RTmalloc (sizeof *stat);
    memcpy (stat, &statistics, sizeof *stat);
    return stat;
    (void)dbs;
}
