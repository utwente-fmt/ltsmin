#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <assert.h>

#include "runtime.h"
#include "treedbs-ll.h"
#include "fast_hash.h"

static const int        TABLE_SIZE = 26;
static const uint64_t   EMPTY = -1UL;
static const size_t     CACHE_LINE_64 = (1 << CACHE_LINE) / sizeof (uint64_t);
static const size_t     CL_MASK = -((1 << CACHE_LINE) / sizeof (uint64_t));

struct treedbs_ll_s {
    int                 nNodes;
    uint32_t            sat_bits;
    uint32_t            sat_mask;
    pthread_key_t       local_key;
    uint64_t           *root;
    uint64_t           *data;
    int               **todo;
    int                 k;
    size_t              size;
    size_t              ratio;
    size_t              thres;
    uint32_t            mask;
    size_t              dsize;
    size_t              dthresh;
    uint32_t            dmask;
};

typedef struct loc_s {
    int                *storage;
    stats_t             stat;
    size_t             *node_count;
} loc_t;

static inline loc_t *
get_local (treedbs_ll_t dbs)
{
    loc_t              *loc = pthread_getspecific (dbs->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (loc_t));
        memset (loc, 0, sizeof (loc_t));
        loc->storage = RTalign (CACHE_LINE_SIZE, sizeof (int[dbs->nNodes * 2]));
        loc->node_count = RTalignZero (CACHE_LINE_SIZE, sizeof (size_t[dbs->nNodes]));
        memset (loc->storage, -1, sizeof (loc->storage));
        pthread_setspecific (dbs->local_key, loc);
    }
    return loc;
}

uint32_t
TreeDBSLLget_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    return atomic32_read (dbs->root+ref) & dbs->sat_mask;
}

void
TreeDBSLLset_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref, uint16_t value)
{
    uint32_t        hash = dbs->root[ref] & ~dbs->sat_mask;
    atomic32_write (dbs->root+ref, hash | (value & dbs->sat_mask));
}

int
TreeDBSLLget_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic32_read (dbs->root+ref);
    uint32_t        val = hash_and_sat & bit;
    return val >> index;
}

int
TreeDBSLLtry_set_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic32_read (dbs->root+ref);
    uint32_t        val = hash_and_sat & bit;
    if (val)
        return 0; //bit was already set
    return cas (dbs->root+ref, hash_and_sat, hash_and_sat | bit);
}

uint32_t
TreeDBSLLinc_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint32_t        val, newval;
    do {
        val = atomic32_read (dbs->root+ref);
        assert ((val & dbs->sat_mask) != dbs->sat_mask);
        newval = val + 1;
    } while ( ! cas (dbs->root+ref, val, newval) );
    return newval;
}

uint32_t
TreeDBSLLdec_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint32_t        val, newval;
    do {
        val = atomic32_read (dbs->root+ref);
        assert ((val & dbs->sat_mask) != 0);
        newval = val - 1;
    } while ( ! cas (dbs->root+ref, val, newval) );
    return newval;
}

static inline uint64_t
prime_rehash (uint64_t h, uint64_t v)
{
    uint64_t            n = (h + 1) & (~CL_MASK);
    uint64_t            p = primes[v & ((1<<9)-1)]; // 1<<9 < 1000
    return (h & CL_MASK) + (p << (CACHE_LINE-3)) + n;
}

static inline int
lookup (uint64_t *table, uint64_t mask, uint64_t threshold,
        const uint64_t data, int index, uint64_t *res, loc_t *loc)
{
    stats_t            *stat = &loc->stat;
    uint64_t            mem, hash, a;
    mem = hash = MurmurHash64 (&data, sizeof(uint64_t), 0);
    for (size_t probes = 0; probes < threshold; probes++) {
        size_t              ref = hash & mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_64;
        for (size_t i = 0; i < CACHE_LINE_64; i++) {
            uint64_t           *bucket = &table[ref];
            if (EMPTY == atomic_read(bucket) && cas(bucket, EMPTY, data)) {
                *res = ref;
                loc->node_count[index]++;
                return 0;
            }
            if (data == atomic_read(bucket)) {
                *res = ref;
                return 1;
            }
            stat->misses++;
            ref += 1;
            ref = ref == line_end ? line_end - CACHE_LINE_64 : ref;
        }
        a = hash;
        hash = prime_rehash (hash, mem);
        assert ((hash & CL_MASK) != (a & CL_MASK));
//            Warning (info, "h=%zu m=%zu p=%zu", hash, mem, primes[mem & ((1<<9)-1)]);
        stat->rehashes++;
    }
    return -1;
}

static inline uint64_t
i64(int *v, size_t ref) {
    return ((int64_t *)v)[ref];
}

static inline int
cmp_i64(int *v1, int *v2, size_t ref) {
    return ((int64_t *)v1)[ref] == ((int64_t *)v2)[ref];
}

int
TreeDBSLLlookup (const treedbs_ll_t dbs, const int *vector)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    int                 seen = 0;
    int                *next = loc->storage;
    memcpy (next + n, vector, sizeof (int[n]));
    uint64_t res = 0;
    for (size_t i = n - 1; seen != -1 && i > 1; i--) {
        seen = lookup (dbs->data, dbs->dmask, dbs->dthresh, i64(next, i), i-1, &res, loc);
        next[i] = res;
    }
    if (seen != -1)
        seen = lookup (dbs->root, dbs->mask, dbs->thres, i64(next, 1), 0, (uint64_t*)next, loc);
    return seen;
}


int
TreeDBSLLlookup_incr (const treedbs_ll_t dbs, const int *v, tree_t prev,
                      tree_t next)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    if ( NULL == prev ) { //first call
        int seen = TreeDBSLLlookup (dbs, v);
        memcpy (next, loc->storage, sizeof(int[n<<1]));
        return seen;
    }
    int                 seen = 1;
    memcpy (next, prev, sizeof (int[n]));
    memcpy (next + n, v, sizeof (int[n]));

    uint64_t res = 0;
    for (size_t i = n - 1; seen != -1 && i > 1; i--) {
        if ( !cmp_i64(prev, next, i) ) {
            seen = lookup (dbs->data, dbs->dmask, dbs->dthresh, i64(next, i), i-1, &res, loc);
            next[i] = res;
        }
    }
    if ( seen != -1 && !cmp_i64(prev, next, 1) )
        seen = lookup (dbs->root, dbs->mask, dbs->thres, i64(next, 1), 0, (uint64_t*)next, loc);
    return seen;
}

int
TreeDBSLLlookup_dm (const treedbs_ll_t dbs, const int *v, tree_t prev,
                    tree_t next, int group)
{
    if ( group == -1 || NULL == prev )
        return TreeDBSLLlookup_incr (dbs, v, prev, next);
    loc_t              *loc = get_local (dbs);
    int                 seen = 1;
    memcpy (next, prev, sizeof (int[dbs->nNodes]));
    memcpy (next + dbs->nNodes, v, sizeof (int[dbs->nNodes]));
    int                 i;

    uint64_t res = 0;
    for (size_t j = 0; seen != -1 && (i = dbs->todo[group][j]) != -1; j++) {
        if ( !cmp_i64(prev, next, i) ) {
            seen = lookup (dbs->data, dbs->dmask, dbs->dthresh, i64(next, i), i-1, &res, loc);
            next[i] = res;
        }
    }
    if ( seen != -1 && !cmp_i64(prev, next, 1) )
        seen = lookup (dbs->root, dbs->mask, dbs->thres, i64(next, 1), 0, (uint64_t*)next, loc);
    return seen;
}

tree_t
TreeDBSLLget (const treedbs_ll_t dbs, const tree_ref_t ref, int *d)
{
    uint32_t           *dst = (uint32_t*)d;
    int64_t            *dst64 = (int64_t *)dst;
    dst64[0] = ref;
    dst64[1] = dbs->root[ref];
    for (int i = 2; i < dbs->nNodes; i++)
        dst64[i] = dbs->data[dst[i]];
    return (tree_t)dst;
}

int *
TreeDBSLLdata (const treedbs_ll_t dbs, tree_t data) {
    return data + dbs->nNodes;
}

tree_ref_t
TreeDBSLLindex (tree_t data) {
    return data[0];
}

void
LOCALfree (void *arg)
{
    loc_t              *loc=  (loc_t*) arg;
    RTfree (loc->node_count);
    RTfree (loc->storage);
    RTfree (loc);
}

treedbs_ll_t
TreeDBSLLcreate (int nNodes, int ratio, int satellite_bits)
{
    return TreeDBSLLcreate_dm (nNodes, TABLE_SIZE, ratio, NULL, satellite_bits);
}

treedbs_ll_t
TreeDBSLLcreate_sized (int nNodes, int size, int ratio, int satellite_bits)
{
    return TreeDBSLLcreate_dm (nNodes, size, ratio, NULL, satellite_bits);
}

/**
* The dependency matrix is projected in the tree:
*
* Binary tree:
*           +                  1
*
*       +       +          2       3
*     -   +   +   -      4   5   6   7
*    / \ / \ / \ / \
* DM row:                   1 1 1 1 1 1
*    - - - + + - - -    8 9 0 1 2 3 4 5
*/
void
project_matrix_to_tree (treedbs_ll_t dbs, matrix_t *m)
{
    int                 nNodes = dbs->nNodes;
    int                 tmp[nNodes * 2];
    dbs->k = dm_nrows(m);
    dbs->todo = RTalign(CACHE_LINE_SIZE, dbs->k * sizeof (dbs->todo[0]));
    for(int row = 0;row < dbs->k;++row){
        dbs->todo[row] = RTalign(CACHE_LINE_SIZE, sizeof (int[nNodes]));
        for(int i = 0; i < nNodes; i++)
            tmp[i + nNodes] = dm_is_set(m, row, i);
        int j = 0;
        for(int i = nNodes - 1; i > 0; i--) {
            int l = (i << 1);
            int r = l + 1;
            tmp[i] = tmp[l] || tmp[r];
            if(tmp[i]){
                dbs->todo[row][j++] = i;
            }
        }
        dbs->todo[row][j] = -1;
    }
}

/**
 * Memorized hash bucket bits org.: <mem_hash> <lock_bit> <node_idx> <sat_bits>
 */
treedbs_ll_t
TreeDBSLLcreate_dm (int nNodes, int size, int ratio, matrix_t * m, int satellite_bits)
{
    assert (sizeof(uint64_t) == 8); //see hardcoded 3
    assert (size <= DB_SIZE_MAX);
    treedbs_ll_t        dbs = RTalign (CACHE_LINE_SIZE, sizeof(struct treedbs_ll_s));
    assert (satellite_bits < 4);
    dbs->sat_bits = satellite_bits;
    dbs->sat_mask  = 3<<30;
    dbs->nNodes = nNodes;
    dbs->size = 1L << size;
    dbs->ratio = ratio;
    dbs->thres = dbs->size / 64;
    dbs->mask = dbs->size - 1;

    dbs->dsize = dbs->size >> dbs->ratio;
    dbs->dthresh = dbs->dsize / 64;
    dbs->dmask = dbs->dsize - 1;

    pthread_key_create(&dbs->local_key, LOCALfree);
    dbs->root = RTalign (CACHE_LINE_SIZE, sizeof (uint64_t) * dbs->size);
    dbs->data = RTalign (CACHE_LINE_SIZE, sizeof (uint64_t) * dbs->dsize);
    if (!dbs->data || !dbs->root)
        Fatal (1, error, "Too large hash table allocated: %zu", dbs->size);
    for (size_t i = 0; i < dbs->size; i++)
        dbs->root[i] = EMPTY;
    for (size_t i = 0; i < dbs->dsize; i++)
        dbs->data[i] = EMPTY;
    dbs->todo = NULL;
    if(m != NULL)
        project_matrix_to_tree(dbs, m);
    return dbs;
}

void
TreeDBSLLfree (treedbs_ll_t dbs)
{
    RTfree (dbs->data);
    RTfree (dbs->root);
    if (NULL != dbs->todo) {
        for(int row = 0;row < dbs->k;++row)
            RTfree (dbs->todo[row]);
        RTfree (dbs->todo);
    }
    RTfree (dbs);
}

stats_t *
TreeDBSLLstats (treedbs_ll_t dbs)
{

    stats_t            *res = RTmalloc (sizeof (*res));
    loc_t              *loc = get_local (dbs);
    for(int i = 0; i < dbs->nNodes; i++)
        loc->stat.nodes += loc->node_count[i];
    loc->stat.elts = loc->node_count[0];
    memcpy (res, &loc->stat, sizeof (*res));
    return res;
}
