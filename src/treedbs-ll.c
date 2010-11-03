#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#include "runtime.h"
#include "treedbs-ll.h"
#include "fast_hash.h"

static const int        TABLE_SIZE = 26;
static const uint32_t   EMPTY = 0;
static const uint32_t   WRITE_BIT = 1 << 31;
static const uint32_t   WRITE_BIT_R = ~(1 << 31);
static const uint32_t   BITS_PER_INT = sizeof (int) * 8;
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
    int                 len_bits;
    int                 nbits;         // hash bitsize
    int                 nbit_mask;
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
} loc_t;

static inline loc_t *
get_local (treedbs_ll_t dbs)
{
    loc_t              *loc = pthread_getspecific (dbs->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (loc_t));
        memset (loc, 0, sizeof (loc_t));
        loc->storage = RTalign (CACHE_LINE_SIZE, sizeof (int[dbs->nNodes * 2]));
        memset (loc->storage, -1, sizeof (loc->storage));
        pthread_setspecific (dbs->local_key, loc);
    }
    return loc;
}

static inline int
table_lookup (const treedbs_ll_t dbs, const node_u_t *data, int index, int *res,
              stats_t *stat)
{
    uint32_t            hash,
                        h;
    h = hash = mix (data->i.left, data->i.right, index);
    hash <<= dbs->len_bits;
    hash ^= (h & dbs->nbit_mask) >> 1; // increases entropy
    hash |= index;
    while (EMPTY == hash || WRITE_BIT == hash)
        hash = mix (data->i.left, data->i.right, hash);
    uint32_t            WAIT = hash & WRITE_BIT_R;
    uint32_t            DONE = hash | WRITE_BIT;
    for (size_t probes = 0; probes < dbs->threshold && !dbs->full; probes++) {
        uint32_t            idx = h & dbs->mask;
        size_t              line_end = (idx & CL_MASK) + CACHE_LINE_INT;
        for (size_t i = 0; i < CACHE_LINE_INT; i++) {
            uint32_t           *bucket = &dbs->table[idx];
            if (EMPTY == *bucket) {
                if (cas (bucket, EMPTY, WAIT)) {
                    write_data_fenced (&dbs->data[idx], data->l.lr);
                    atomic_write (bucket, DONE);
                    *res = idx;
                    stat->elts++;
                    return 0;
                }
            }
            if (DONE == (atomic_read (bucket) | WRITE_BIT)) {
                while (WAIT == atomic_read (bucket)) {}
                if (dbs->data[idx] == data->l.lr) {
                    *res = idx;
                    return 1;
                }
                stat->misses++;
            }
            idx += 1;
            idx = idx == line_end ? line_end - CACHE_LINE_INT : idx;
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
i64(int *v, size_t idx) {
    return (node_u_t *) (v + (idx<<1));
}

static inline int
cmp_i64(int *v1, int *v2, size_t idx) {
    return ((int64_t *)v1)[idx] == ((int64_t *)v2)[idx];
}

int
TreeDBSLLlookup (const treedbs_ll_t dbs, const int *vector)
{
    loc_t              *loc = get_local (dbs);
    stats_t            *stat = &loc->stat;
    size_t              n = dbs->nNodes;
    int                 seen = 0;
    int                *next = loc->storage;
    memcpy (next + n, vector, sizeof (int[n]));
    for (size_t i = n - 1; i > 0; i--)
        seen = table_lookup (dbs, i64(next, i), i-1, next+i, stat);
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
    stats_t            *stat = &loc->stat;
    int                 seen = 1;
    memcpy (next, prev, sizeof (int[n]));
    memcpy (next + n, v, sizeof (int[n]));
    for (size_t i = n - 1; i > 0; i--)
    if ( !cmp_i64(prev, next, i) )
        seen = table_lookup (dbs, i64(next, i), i-1, next+i, stat);
    return seen;
}

int
TreeDBSLLlookup_dm (const treedbs_ll_t dbs, const int *v, tree_t prev, 
                    tree_t next, int group)
{
    if ( group == -1 || NULL == prev )
        return TreeDBSLLlookup_incr (dbs, v, prev, next);
    loc_t              *loc = get_local (dbs);
    stats_t            *stat = &loc->stat;
    int                 seen = 1;
    memcpy (next, prev, sizeof (int[dbs->nNodes]));
    memcpy (next + dbs->nNodes, v, sizeof (int[dbs->nNodes]));
    int                 idx;
    for (size_t i = 0; (idx = dbs->todo[group][i]) != -1; i++)
    if ( !cmp_i64(prev, next, idx) )
        seen = table_lookup (dbs, i64(next, idx), idx-1, next+idx, stat);
    return seen;
}

tree_t
TreeDBSLLget (const treedbs_ll_t dbs, const int idx, int *d)
{
    uint32_t           *dst = (uint32_t*)d;
    int64_t            *dst64 = (int64_t *)dst;
    dst[1] = (uint32_t)idx;
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
LOCALfree (void *loc)
{
    RTfree (loc);
}

treedbs_ll_t
TreeDBSLLcreate (int nNodes)
{
    return TreeDBSLLcreate_dm (nNodes, TABLE_SIZE, NULL);
}

treedbs_ll_t
TreeDBSLLcreate_sized (int nNodes, int size)
{
    return TreeDBSLLcreate_dm (nNodes, size, NULL);
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
TreeDBSLLcreate_dm (int nNodes, int size, matrix_t * m)
{
    treedbs_ll_t        dbs = RTalign (CACHE_LINE_SIZE, sizeof(struct treedbs_ll_s));
    int                 pow = 0;
    while ((1 << (++pow)) < nNodes) {}
    dbs->len_bits = pow;
    dbs->nbits = BITS_PER_INT - pow - 1;
    dbs->nbit_mask = -(1 << dbs->nbits);
    dbs->nNodes = nNodes;
    pthread_key_create(&dbs->local_key, LOCALfree);
    dbs->full = 0;
    dbs->size = 1L << size;
    dbs->threshold = dbs->size / 50;
    dbs->mask = dbs->size - 1;
    dbs->table = RTalign(CACHE_LINE_SIZE, sizeof (uint32_t) * dbs->size);
    dbs->data = RTalign(CACHE_LINE_SIZE, sizeof (node_u_t) * dbs->size);
    if (!dbs->data)
        Fatal (1, error, "Too large hash table allocated: %zu", dbs->size);
    memset(dbs->table, 0, sizeof (uint32_t[dbs->size]));
    if(m != NULL)
        project_matrix_to_tree(dbs, m);
    return dbs;
}

void
TreeDBSLLfree (treedbs_ll_t dbs)
{
    RTfree (dbs->data);
    RTfree (dbs->table);
    RTfree (dbs);
}

stats_t *
TreeDBSLLstats (treedbs_ll_t dbs)
{
    loc_t              *loc = get_local (dbs);
    return &loc->stat;
}
