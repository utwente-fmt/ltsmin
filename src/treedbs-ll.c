#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <assert.h>

#include <atomics.h>
#include <clt_table.h>
#include <fast_hash.h>
#include <runtime.h>
#include <treedbs-ll.h>


static const int        TABLE_SIZE = 26;
static const uint64_t   EMPTY = 0UL;
static const uint64_t   EMPTY_1 = -1UL;
static const size_t     CACHE_LINE_64 = (1 << CACHE_LINE) / sizeof (uint64_t);
static const size_t     CL_MASK = -((1 << CACHE_LINE) / sizeof (uint64_t));

typedef struct node_table_s {
    uint64_t           *table;
    size_t              log_size;
    size_t              size;
    size_t              thres;
    uint64_t            mask;
    int                 error_num;
} node_table_t;

struct treedbs_ll_s {
    int                 nNodes; // see treedbs_ll_inlined_t
    int                 slim;   // see treedbs_ll_inlined_t
    uint32_t            sat_bits;
    uint32_t            sat_mask;
    pthread_key_t       local_key;
    clt_dbs_t          *clt;
    node_table_t        root;
    node_table_t        data;
    int               **todo;
    int                 k;
    size_t              ratio;
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
    return atomic_read (dbs->root.table+ref) & dbs->sat_mask;
}

void
TreeDBSLLset_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref, uint16_t value)
{
    uint32_t        hash = dbs->root.table[ref] & ~dbs->sat_mask;
    atomic_write (dbs->root.table+ref, hash | (value & dbs->sat_mask));
}

int
TreeDBSLLget_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic_read (dbs->root.table+ref);
    uint32_t        val = hash_and_sat & bit;
    return val >> index;
}

void
TreeDBSLLunset_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic_read (dbs->root.table+ref);
    uint32_t        val = hash_and_sat & ~bit;
    atomic_write (dbs->root.table+ref, val);
}

int
TreeDBSLLtry_set_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1U << index;
    do {
        uint32_t        hash_and_sat = atomic_read (dbs->root.table+ref);
        uint32_t        val = hash_and_sat & bit;
        if (val)
            return 0; // bit was already set
        if (cas(dbs->root.table+ref, hash_and_sat, hash_and_sat | bit))
            return 1; // success
    } while ( 1 ); // another bit was set
}

int
TreeDBSLLtry_unset_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = (1U << index);
    do {
        uint32_t        hash_and_sat = atomic_read (dbs->root.table+ref);
        uint32_t        val = hash_and_sat & bit;
        if (!val)
            return 0; // bit was already set
        if (cas(dbs->root.table+ref, hash_and_sat, hash_and_sat & ~bit))
            return 1; // success
    } while ( 1 ); // another bit was set

}

uint32_t
TreeDBSLLinc_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint32_t        val, newval;
    do {
        val = atomic_read (dbs->root.table+ref);
        assert ((val & dbs->sat_mask) != dbs->sat_mask);
        newval = val + 1;
    } while ( ! cas (dbs->root.table+ref, val, newval) );
    return newval;
}

uint32_t
TreeDBSLLdec_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint32_t        val, newval;
    do {
        val = atomic_read (dbs->root.table+ref);
        assert ((val & dbs->sat_mask) != 0);
        newval = val - 1;
    } while ( ! cas (dbs->root.table+ref, val, newval) );
    return newval;
}

static inline uint64_t
prime_rehash (uint64_t h, uint64_t v)
{
    uint64_t            n = (h + 1) & (~CL_MASK);
    uint64_t            p = primes[v & PRIME_MASK];
    return (h & CL_MASK) + (p << (CACHE_LINE-3)) + n;
}

static inline int
lookup (node_table_t *table,
        uint64_t data, int index, uint64_t *res, loc_t *loc)
{
    stats_t            *stat = &loc->stat;
    uint64_t            mem, hash, a;
    mem = hash = MurmurHash64 (&data, sizeof(uint64_t), 0);
    assert (data != EMPTY_1 && "Value out of table range.");
    data += 1; // avoid EMPTY
    for (size_t probes = 0; probes < table->thres; probes++) {
        size_t              ref = hash & table->mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_64;
        for (size_t i = 0; i < CACHE_LINE_64; i++) {
            uint64_t           *bucket = &table->table[ref];
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
        stat->rehashes++;
    }
    return table->error_num;
}

static inline uint64_t
i64(int *v, size_t ref) {
    return ((int64_t *)v)[ref];
}

static inline int
cmp_i64(int *v1, int *v2, size_t ref) {
    return ((int64_t *)v1)[ref] == ((int64_t *)v2)[ref];
}

static inline uint64_t
concat_n_mix (const treedbs_ll_t dbs, uint64_t a, uint64_t b)
{
    // append the two numbers while doing some coarse-grained mixing
    uint64_t key = (b + a) & dbs->data.mask;
    key |= (b ^ key) << dbs->data.log_size;
    return nbit_mix (key, dbs->data.log_size << 1); // fine-grained mixing
}

static inline
int clt_lookup (const treedbs_ll_t dbs, int *next)
{
     uint64_t key = concat_n_mix (dbs, next[2], next[3]);
    int seen = clt_find_or_put (dbs->clt, key);
    ((uint64_t*)next)[0] = ((uint64_t*)next)[1];
    return seen;
}

int
TreeDBSLLlookup (const treedbs_ll_t dbs, const int *vector)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    int                 seen = 0;
    uint64_t            res = 0;
    int                *next = loc->storage;
    memcpy (next + n, vector, sizeof (int[n]));
    for (size_t i = n - 1; seen >= 0 && i > 1; i--) {
        seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc);
        next[i] = res;
    }
    if (seen >= 0) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next);
            loc->node_count[0] += 1 - seen;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc);
            ((uint64_t*)next)[0] = -1;
        }
    }
    return seen;
}

int
TreeDBSLLlookup_incr (const treedbs_ll_t dbs, const int *v, tree_t prev,
                      tree_t next)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    uint64_t            res = 0;
    int                 seen = 1;
    if ( NULL == prev ) { //first call
        int result = TreeDBSLLlookup (dbs, v);
        memcpy (next, loc->storage, sizeof(int[n<<1]));
        return result;
    }
    memcpy (next, prev, sizeof (int[n]));
    memcpy (next + n, v, sizeof (int[n]));
    for (size_t i = n - 1; seen >= 0 && i > 1; i--) {
        if ( !cmp_i64(prev, next, i) ) {
            seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc);
            next[i] = res;
        }
    }
    if ( seen >= 0 && !cmp_i64(prev, next, 1) ) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next);
            loc->node_count[0] += 1 - seen;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc);
            ((uint64_t*)next)[0] = -1;
        }
    }
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
    int                 i;
    uint64_t            res = 0;
    memcpy (next, prev, sizeof (int[dbs->nNodes]));
    memcpy (next + dbs->nNodes, v, sizeof (int[dbs->nNodes]));
    for (size_t j = 0; seen >= 0 && (i = dbs->todo[group][j]) != -1; j++) {
        if ( i != 1 && !cmp_i64(prev, next, i) ) {
            seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc);
            next[i] = res;
        }
    }
    if ( seen >= 0 && !cmp_i64(prev, next, 1) ) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next);
            loc->node_count[0] += 1 - seen;
            ((uint64_t*)next)[0] = -1;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc);
            ((uint64_t*)next)[0] = -1;
        }
    }
    return seen;
}

tree_t
TreeDBSLLget (const treedbs_ll_t dbs, const tree_ref_t ref, int *d)
{
    uint32_t           *dst     = (uint32_t*)d;
    int64_t            *dst64   = (int64_t *)dst;
    //if (dbs->slim) { // skip the root leaf for cleary and for normal tree!
        dst64[0] = -1;
        dst64[1] = ref;
    //} else {
    //    dst64[0] = ref;
    //    dst64[1] = dbs->root.table[ref] - 1;
    //}
    for (int i = 2; i < dbs->nNodes; i++)
        dst64[i] = dbs->data.table[dst[i]] - 1; // see lookup() --> avoid EMPTY
    return (tree_t)dst;
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
TreeDBSLLcreate (int nNodes, int ratio, int satellite_bits, int slim)
{
    return TreeDBSLLcreate_dm (nNodes, TABLE_SIZE, ratio, NULL, satellite_bits, slim);
}

treedbs_ll_t
TreeDBSLLcreate_sized (int nNodes, int size, int ratio, int satellite_bits, int slim)
{
    return TreeDBSLLcreate_dm (nNodes, size, ratio, NULL, satellite_bits, slim);
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

treedbs_ll_t
TreeDBSLLcreate_dm (int nNodes, int size, int ratio, matrix_t * m, int satellite_bits, int slim)
{
    assert (size <= DB_SIZE_MAX && "Tree too large");
    assert (nNodes >= 2 && "Tree too small");
    assert ((slim == 0 || slim == 1) && "Wrong value for slim");
    treedbs_ll_t        dbs = RTalign (CACHE_LINE_SIZE, sizeof(struct treedbs_ll_s));
    assert (satellite_bits < 4);
    dbs->sat_bits = satellite_bits;
    dbs->sat_mask  = 3<<30;
    dbs->nNodes = nNodes;
    dbs->ratio = ratio;
    dbs->slim = slim;

    dbs->root.size = 1UL << size;
    dbs->root.log_size = size;
    dbs->root.thres = dbs->root.size / 64;
    dbs->root.mask = dbs->root.size - 1;
    dbs->root.error_num = DB_ROOTS_FULL;

    dbs->data.log_size = size - dbs->ratio;
    dbs->data.size = dbs->root.size >> dbs->ratio;
    dbs->data.thres = dbs->data.size / 64;
    dbs->data.mask = dbs->data.size - 1;
    dbs->data.error_num = DB_LEAFS_FULL;

    pthread_key_create(&dbs->local_key, LOCALfree);
    if (dbs->slim) {
        dbs->clt = clt_create (dbs->data.log_size*2, dbs->root.log_size);
    } else {
        dbs->root.table = RTalignZero (CACHE_LINE_SIZE, sizeof (uint64_t) * dbs->root.size);
    }
    dbs->data.table = RTalignZero (CACHE_LINE_SIZE, sizeof (uint64_t) * dbs->root.size);
    if (!dbs->data.table || (!dbs->root.table && !dbs->clt))
        Fatal (1, error, "Too large hash table allocated: %zu", dbs->root.size);
    dbs->todo = NULL;
    if(m != NULL)
        project_matrix_to_tree(dbs, m);
    return dbs;
}

void
TreeDBSLLfree (treedbs_ll_t dbs)
{
    RTfree (dbs->data.table);
    if (dbs->slim) {
        clt_free (dbs->clt);
    } else {
        RTfree (dbs->root.table);
    }
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
