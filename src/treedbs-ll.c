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
static const uint32_t   EMPTY = 0;
static uint32_t         WRITE_BIT = 1;
static uint32_t         WRITE_BIT_R = ~((uint32_t)1);
static const size_t     BITS_PER_INT = sizeof (int) * 8;
static const size_t     CL_MASK = -((1 << CACHE_LINE) / sizeof (int));

typedef struct int_s {
    uint32_t            left;
    uint32_t            right;
} int_t;

typedef struct i64_s {
    int64_t             lr;
} i64_t;

typedef union node_u {
    int_t               i;
    i64_t               l;
} node_u_t;

struct treedbs_ll_s {
    int                 nNodes;
    uint32_t            sat_bits;
    uint32_t            sat_mask;
    uint32_t            idx_bits;
    uint32_t            idx_mask;
    uint32_t            hash_bits;
    uint32_t            hash_mask;
    pthread_key_t       local_key;
    uint32_t           *table;
    int64_t            *data;
    int               **todo;
    int                 k;
    size_t              size;
    size_t              threshold;
    int                 full;
    uint32_t            mask;
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

uint16_t
TreeDBSLLget_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    return atomic32_read (dbs->table+ref) & dbs->sat_mask;
}

void
TreeDBSLLset_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref, uint16_t value)
{
    uint32_t        hash = dbs->table[ref] & ~dbs->sat_mask;
    atomic32_write (dbs->table+ref, hash | (value & dbs->sat_mask));
}

int
TreeDBSLLget_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic32_read (dbs->table+ref);
    uint32_t        val = hash_and_sat & bit;
    return val >> index;
}

int
TreeDBSLLtry_set_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint32_t        bit = 1 << index;
    uint32_t        hash_and_sat = atomic32_read (dbs->table+ref);
    uint32_t        val = hash_and_sat & bit;
    if (val)
        return 0; //bit was already set
    return cas (dbs->table+ref, hash_and_sat, hash_and_sat | bit);
}

static inline int
table_lookup (const treedbs_ll_t dbs, const node_u_t *data, int index, int *res,
              loc_t *loc)
{
    stats_t            *stat = &loc->stat;
    uint32_t            hash,
                        h = index;
    do {
        h = hash = mix (data->i.left, data->i.right, h);
        hash &= dbs->hash_mask;
        hash ^= (h & ~dbs->hash_mask) << (BITS_PER_INT - dbs->hash_bits); // increases entropy
        hash |= index << dbs->sat_bits;
    } while (EMPTY == hash || WRITE_BIT == hash);
    
    uint32_t            WAIT = hash & WRITE_BIT_R;
    uint32_t            DONE = hash | WRITE_BIT;
    for (size_t probes = 0; probes < dbs->threshold && !dbs->full; probes++) {
        size_t              ref = h & dbs->mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_INT;
        for (size_t i = 0; i < CACHE_LINE_INT; i++) {
            uint32_t           *bucket = &dbs->table[ref];
            if (EMPTY == atomic32_read(bucket)) {
                if (cas (bucket, EMPTY, WAIT)) {
                    write_data_fenced (&dbs->data[ref], data->l.lr);
                    atomic32_write (bucket, DONE);
                    *res = ref;
                    loc->node_count[index]++;
                    return 0;
                }
            }
            if (DONE == ((atomic32_read (bucket) & ~dbs->sat_mask) | WRITE_BIT)) {
                while (WAIT == (atomic32_read (bucket) & ~dbs->sat_mask)) {}
                if (dbs->data[ref] == data->l.lr) {
                    *res = ref;
                    return 1;
                }
                stat->misses++;
            }
            ref += 1;
            ref = ref == line_end ? line_end - CACHE_LINE_INT : ref;
        }
        h = mix (data->i.left, data->i.right, h);
        stat->rehashes++;
    }
    if ( cas (&dbs->full, 0, 1) ) {
        kill(0, SIGINT);
        Warning(info, "ERROR: Hash table full (size: %zu nodes)", dbs->size);
    }
    *res = 0; //incorrect, does not matter anymore
    return 1;
}

static inline node_u_t *
i64(int *v, size_t ref) {
    return (node_u_t *) (v + (ref<<1));
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
    for (size_t i = n - 1; i > 0; i--)
        seen = table_lookup (dbs, i64(next, i), i-1, next+i, loc);
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
    for (size_t i = n - 1; i > 0; i--)
    if ( !cmp_i64(prev, next, i) )
        seen = table_lookup (dbs, i64(next, i), i-1, next+i, loc);
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
    int                 idx;
    for (size_t i = 0; (idx = dbs->todo[group][i]) != -1; i++)
    if ( !cmp_i64(prev, next, idx) )
        seen = table_lookup (dbs, i64(next, idx), idx-1, next+idx, loc);
    return seen;
}

tree_t
TreeDBSLLget (const treedbs_ll_t dbs, const tree_ref_t ref, int *d)
{
    uint32_t           *dst = (uint32_t*)d;
    int64_t            *dst64 = (int64_t *)dst;
    dst[1] = (uint32_t)ref;
    for (int i = 1; i < dbs->nNodes; i++)
        dst64[i] = dbs->data[dst[i]];
    return (tree_t)dst;
}

int *
TreeDBSLLdata (const treedbs_ll_t dbs, tree_t data) {
    return data + dbs->nNodes;
}

uint32_t
TreeDBSLLindex (tree_t data) {
    return data[1];
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
TreeDBSLLcreate (int nNodes, int satellite_bits)
{
    return TreeDBSLLcreate_dm (nNodes, TABLE_SIZE, NULL, satellite_bits);
}

treedbs_ll_t
TreeDBSLLcreate_sized (int nNodes, int size, int satellite_bits)
{
    return TreeDBSLLcreate_dm (nNodes, size, NULL, satellite_bits);
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
TreeDBSLLcreate_dm (int nNodes, int size, matrix_t * m, int satellite_bits)
{
    assert (size <= DB_SIZE_MAX);
    treedbs_ll_t        dbs = RTalign (CACHE_LINE_SIZE, sizeof(struct treedbs_ll_s));
    uint32_t            pow = 0;
    while ((1 << (++pow)) < nNodes) {}
    assert (satellite_bits < 16);
    assert (satellite_bits + pow + 1 <= BITS_PER_INT);
    dbs->idx_bits = pow;
    dbs->sat_bits = satellite_bits;
    dbs->hash_bits = BITS_PER_INT - dbs->idx_bits - dbs->sat_bits  - 1;
    dbs->sat_mask  = ((1<<dbs->sat_bits) -1);
    dbs->idx_mask  = ((1<<dbs->idx_bits) -1) << dbs->sat_bits;
    dbs->hash_mask = ((1<<dbs->hash_bits)-1) << (dbs->idx_bits+dbs->sat_bits+1);
    assert ( dbs->hash_mask==0 || (dbs->hash_mask & 1<<(BITS_PER_INT-1)) );
    /* Lock bits are stored in the memoized hash.
     *
     * The lower the lock bit is stored, the better. Since the lower bits are
     * used for indexing, they carry less information content than the higher
     * bits, when applied for comparing hashes.
     */
    WRITE_BIT <<= satellite_bits;
    WRITE_BIT_R <<= satellite_bits;
    dbs->nNodes = nNodes;
    dbs->full = 0;
    dbs->size = 1L << size;
    dbs->threshold = dbs->size / 50;
    dbs->mask = dbs->size - 1;
    pthread_key_create(&dbs->local_key, LOCALfree);
    dbs->table = RTalignZero (CACHE_LINE_SIZE, sizeof (uint32_t) * dbs->size);
    dbs->data = RTalign (CACHE_LINE_SIZE, sizeof (node_u_t) * dbs->size);
    if (!dbs->data)
        Fatal (1, error, "Too large hash table allocated: %zu", dbs->size);
    dbs->todo = NULL;
    if(m != NULL)
        project_matrix_to_tree(dbs, m);
    return dbs;
}

void
TreeDBSLLfree (treedbs_ll_t dbs)
{
    RTfree (dbs->data);
    RTfree (dbs->table);
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
