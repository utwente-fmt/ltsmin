#include <hre/config.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hre/user.h>
#include <mc-lib/atomics.h>
#include <mc-lib/clt_table.h>
#include <mc-lib/dbs-ll.h>
#include <mc-lib/treedbs-ll.h>
#include <util-lib/fast_hash.h>
#include <util-lib/util.h>


static const int        TABLE_SIZE = 26;
static const uint64_t   EMPTY = 0ULL;
static const uint64_t   EMPTY_1 = -1ULL;
static const size_t     CACHE_LINE_64 = (1 << CACHE_LINE) / sizeof (uint64_t);
static const size_t     CL_MASK = -((1 << CACHE_LINE) / sizeof (uint64_t));

typedef struct node_table_s {
    uint64_t           *table;
    size_t              log_size;
    size_t              size;
    size_t              thres;
    uint64_t            mask;
    uint64_t            sat_bits;
    uint64_t            sat_mask;
    uint64_t            sat_left;
    uint64_t            sat_right;
    uint64_t            sat_nmask;
    int                 error_num;
} node_table_t;

struct treedbs_ll_s {
    size_t              nNodes; // see treedbs_ll_inlined_t
    int                 slim;   // see treedbs_ll_inlined_t
    int                 indexing;// see treedbs_ll_inlined_t
    clt_dbs_t          *clt;
    node_table_t        root;
    node_table_t        data;
    int               **todo;
    int                 k;
    hre_key_t           local_key;
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
    loc_t              *loc = HREgetLocal (dbs->local_key);
    if (loc == NULL) {
        loc = RTalign (CACHE_LINE_SIZE, sizeof (loc_t));
        memset (loc, 0, sizeof (loc_t));
        loc->storage = RTalign (CACHE_LINE_SIZE, sizeof (int[dbs->nNodes * 2]));
        loc->node_count = RTalignZero (CACHE_LINE_SIZE, sizeof (size_t[dbs->nNodes]));
        memset (loc->storage, -1, sizeof (int[dbs->nNodes * 2]));
        HREsetLocal (dbs->local_key, loc);
    }
    return loc;
}

/**
 * Sat bits organization (b1...b5) in node n with children <n1, n2>:
 * (Note that |n1| == |n2|)
 *
 * /------------+------------\
 * |_,b1,b2,n1  | b3,b4,b5,n2|         <-- node n
 * \------------+------------/
 *  <----------> <---------->
 *     32 bit       32 bit
 *
 *      left         right             <-- part
 */
static uint64_t
bit_pos (uint16_t bits, uint16_t index)
{
    uint16_t            half = (bits + 1) >> 1; // ceil (bits / 2)
    uint16_t            part = (index >= half); // 0 for right, 1 for left
    uint64_t            bit = (32 << part) - (half << part) + index; // see function comment
    return 1ULL << bit;
}

static inline uint64_t
b2s (const treedbs_ll_t dbs, uint64_t bits)
{
    uint64_t            sat = bits & dbs->root.sat_mask;
    sat = (sat >> dbs->root.sat_left) | ((sat >> dbs->root.sat_right) & 0xFFFF);
    return sat;
}

static inline uint64_t
s2b (const treedbs_ll_t dbs, uint64_t sat)
{
    uint64_t            bits;
    bits = (sat << dbs->root.sat_left) | ((sat << dbs->root.sat_right));
    return bits & dbs->root.sat_mask;
}

uint32_t
TreeDBSLLget_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    return b2s (dbs, atomic_read(dbs->root.table+ref));
}

int
TreeDBSLLget_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint64_t            bit = bit_pos (dbs->root.sat_bits, index);
    uint64_t            hash_and_sat = atomic_read (dbs->root.table+ref);
    uint64_t            val = hash_and_sat & bit;
    return val != 0;
}

void
TreeDBSLLunset_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint64_t            bit = bit_pos (dbs->root.sat_bits, index);
    uint64_t            hash_and_sat = atomic_read (dbs->root.table+ref);
    if (0 == (hash_and_sat & bit))
        return;
    fetch_and (dbs->root.table+ref, ~bit);
}

int
TreeDBSLLtry_set_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint64_t            bit = bit_pos (dbs->root.sat_bits, index);
    uint64_t            hash_and_sat = atomic_read (dbs->root.table+ref);
    if (0 != (hash_and_sat & bit))
        return 0;
    uint64_t            prev = fetch_or (dbs->root.table+ref, bit);
    return (prev & bit) == 0;
}

int
TreeDBSLLtry_unset_sat_bit (const treedbs_ll_t dbs, const tree_ref_t ref, int index)
{
    uint64_t            bit = bit_pos (dbs->root.sat_bits, index);
    uint64_t            hash_and_sat = atomic_read (dbs->root.table+ref);
    if (0 == (hash_and_sat & bit))
        return 0;
    uint64_t            prev = fetch_and (dbs->root.table+ref, ~bit);
    return (prev & bit) != 0;
}

static inline int
cas_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref,
              uint64_t read, uint64_t value)
{
    value = s2b (dbs, value) | (read & dbs->root.sat_nmask);
    return cas (dbs->root.table+ref, read, value);
}

int
TreeDBSLLtry_set_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref,
                           size_t bits, size_t offs,
                           uint64_t exp, uint64_t new_val)
{
    uint64_t            old_val, old_bits, new_v;
    uint64_t            mask = (1ULL << bits) - 1;
    HREassert (new_val < (1ULL << dbs->root.sat_bits), "new_val too high");
    HREassert ((new_val & mask) == new_val, "new_val too high w.r.t. bits");

    mask <<= offs;
    exp <<= offs;
    old_bits = atomic_read (dbs->root.table+ref);
    old_val = b2s (dbs, old_bits);
    if ((old_val & mask) != exp) return false;

    new_val <<= offs;
    new_v = (old_val & ~mask) | new_val;
    return cas_sat_bits(dbs, ref, old_bits, new_v);
}

uint32_t
TreeDBSLLinc_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint64_t            new_val, bits;
    do {
        bits = atomic_read (dbs->root.table+ref);
        new_val = b2s (dbs, bits);
        HREassert (new_val < (1ULL << dbs->root.sat_bits), "Too many sat bit incs");
        new_val += 1;
    } while ( !cas_sat_bits(dbs, ref, bits, new_val) );
    return new_val;
}

uint32_t
TreeDBSLLdec_sat_bits (const treedbs_ll_t dbs, const tree_ref_t ref)
{
    uint64_t            new_val, bits;
    do {
        bits = atomic_read (dbs->root.table+ref);
        new_val = b2s (dbs, bits);
        HREassert (new_val > 0, "Too many sat bit decs");
        new_val -= 1;
    } while ( !cas_sat_bits(dbs, ref, bits, new_val) );
    return new_val;
}

static inline uint64_t
prime_rehash (uint64_t h, uint64_t v)
{
    uint64_t            n = (h + 1) & (~CL_MASK);
    uint64_t            p = odd_primes[v & PRIME_MASK];
    return (h & CL_MASK) + (p << (CACHE_LINE-3)) + n;
}

static inline int
lookup (node_table_t *nodes,
        uint64_t data, int index, uint64_t *res, loc_t *loc, bool insert)
{
    stats_t            *stat = &loc->stat;
    uint64_t            mem, hash, a;
    mem = hash = mix64 (data);
    HREassert (data != EMPTY_1, "Value (%zu) out of table range.\n"
            "Avoid -1 value or modify EMPTY_1 reserved value to unused value and recompile LTSmin.", (size_t) data);
            // (re-wire data == EMPTY to data := EMPTY_1 after the next loc)
    data += 1; // avoid EMPTY
    // data = data == EMPTY ? EMPTY_1 : data;
    for (size_t probes = 0; probes < nodes->thres; probes++) {
        size_t              ref = hash & nodes->mask;
        size_t              line_end = (ref & CL_MASK) + CACHE_LINE_64;
        for (size_t i = 0; i < CACHE_LINE_64; i++) {
            uint64_t           *bucket = &nodes->table[ref];
            if (EMPTY == atomic_read(bucket)) {
                if (!insert) return DB_NOT_FOUND;
                if (cas(bucket, EMPTY, data)) {
                    *res = ref;
                    loc->node_count[index]++;
                    return 0;
                }
            }
            if (data == (atomic_read(bucket) & nodes->sat_nmask)) {
                *res = ref;
                return 1;
            }
            stat->misses++;
            ref += 1;
            ref = ref == line_end ? line_end - CACHE_LINE_64 : ref;
        }
        a = hash;
        hash = prime_rehash (hash, mem);
        HREassert ((hash & CL_MASK) != (a & CL_MASK), "loop in hashing function");
        stat->rehashes++;
    }
    return nodes->error_num;
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
int clt_lookup (const treedbs_ll_t dbs, int *next, bool insert)
{
    uint64_t key = concat_n_mix (dbs, next[2], next[3]);
    int seen = clt_find_or_put (dbs->clt, key, insert);
    ((uint64_t*)next)[0] = ((uint64_t*)next)[1];
    return seen;
}

int
TreeDBSLLfop (const treedbs_ll_t dbs, const int *vector, bool insert)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    int                 seen = 0;
    uint64_t            res = 0;
    int                *next = loc->storage;
    memcpy (next + n, vector, sizeof (int[n]));
    for (size_t i = n - 1; seen >= 0 && i > 1; i--) {
        seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc, insert);
        if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
        next[i] = res;
    }
    if (seen >= 0) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next, insert);
            if (seen < 0) return seen;
            loc->node_count[0] += 1 - seen;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc, insert);
            if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
            //((uint64_t*)next)[0] = -1;
        }
    }
    return seen;
}

int
TreeDBSLLfop_incr (const treedbs_ll_t dbs, const int *v, tree_t prev,
                   tree_t next, bool insert)
{
    loc_t              *loc = get_local (dbs);
    size_t              n = dbs->nNodes;
    uint64_t            res = 0;
    int                 seen = 1;
    if ( NULL == prev ) { //first call
        int result = TreeDBSLLfop (dbs, v, insert);
        memcpy (next, loc->storage, sizeof(int[n<<1]));
        return result;
    }
    memcpy (next, prev, sizeof (int[n]));
    memcpy (next + n, v, sizeof (int[n]));
    for (size_t i = n - 1; seen >= 0 && i > 1; i--) {
        if ( !cmp_i64(prev, next, i) ) {
            seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc, insert);
            if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
            next[i] = res;
        }
    }
    if ( seen >= 0 && !cmp_i64(prev, next, 1) ) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next, insert);
            if (seen < 0) return seen;
            loc->node_count[0] += 1 - seen;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc, insert);
            if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
            //((uint64_t*)next)[0] = -1;
        }
    }
    return seen;
}

int
TreeDBSLLfop_dm (const treedbs_ll_t dbs, const int *v, tree_t prev,
                 tree_t next, int group, bool insert)
{
    if ( group == -1 || NULL == prev )
        return TreeDBSLLfop_incr (dbs, v, prev, next, insert);
    loc_t              *loc = get_local (dbs);
    int                 seen = 1;
    int                 i;
    uint64_t            res = 0;
    memcpy (next, prev, sizeof (int[dbs->nNodes]));
    memcpy (next + dbs->nNodes, v, sizeof (int[dbs->nNodes]));
    for (size_t j = 0; seen >= 0 && (i = dbs->todo[group][j]) != -1; j++) {
        if ( i != 1 && !cmp_i64(prev, next, i) ) {
            seen = lookup (&dbs->data, i64(next, i), i-1, &res, loc, insert);
            if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
            next[i] = res;
        }
    }
    if ( seen >= 0 && !cmp_i64(prev, next, 1) ) {
        if (dbs->slim) {
            seen = clt_lookup (dbs, next, insert);
            if (seen < 0) return seen;
            loc->node_count[0] += 1 - seen;
            ((uint64_t*)next)[0] = -1;
        } else {
            seen = lookup (&dbs->root, i64(next, 1), 0, (uint64_t*)next, loc, insert);
            if (!insert && seen == DB_NOT_FOUND) return DB_NOT_FOUND;
            //((uint64_t*)next)[0] = -1;
        }
    }
    return seen;
}

tree_t
TreeDBSLLget (const treedbs_ll_t dbs, const tree_ref_t ref, int *d)
{
    uint32_t           *dst     = (uint32_t*)d;
    int64_t            *dst64   = (int64_t *)dst;
    if (!dbs->indexing) { // skip the root leaf for cleary and for normal tree!
        dst64[0] = -1;
        dst64[1] = ref;
    } else {
        dst64[0] = ref;
        dst64[1] = (dbs->root.table[ref] - 1) & dbs->root.sat_nmask;
    }
    for (size_t i = 2; i < dbs->nNodes; i++)
        dst64[i] = (dbs->data.table[dst[i]] - 1) & dbs->data.sat_nmask; // for "- 1", see lookup() --> avoid EMPTY
    return (tree_t)dst;
}

void
LOCALfree (void *arg)
{
    loc_t              *loc=  (loc_t*) arg;
    RTalignedFree (loc->node_count);
    RTalignedFree (loc->storage);
    RTalignedFree (loc);
}

treedbs_ll_t
TreeDBSLLcreate (int nNodes, int ratio, int satellite_bits, int slim, int indexing)
{
    return TreeDBSLLcreate_dm (nNodes, TABLE_SIZE, ratio, NULL, satellite_bits, slim, indexing);
}

treedbs_ll_t
TreeDBSLLcreate_sized (int nNodes, int size, int ratio, int satellite_bits, int slim, int indexing)
{
    return TreeDBSLLcreate_dm (nNodes, size, ratio, NULL, satellite_bits, slim, indexing);
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
    size_t              nNodes = dbs->nNodes;
    int                 tmp[nNodes * 2];
    dbs->k = dm_nrows(m);
    dbs->todo = RTalign(CACHE_LINE_SIZE, dbs->k * sizeof (dbs->todo[0]));
    for (int row = 0;row < dbs->k;++row) {
        dbs->todo[row] = RTalign(CACHE_LINE_SIZE, sizeof (int[nNodes]));
        for (size_t i = 0; i < nNodes; i++)
            tmp[i + nNodes] = dm_is_set(m, row, i);
        int j = 0;
        for (int i = nNodes - 1; i > 0; i--) {
            int l = (i << 1);
            int r = l + 1;
            tmp[i] = tmp[l] || tmp[r];
            if (tmp[i]) {
                dbs->todo[row][j++] = i;
            }
        }
        dbs->todo[row][j] = -1;
    }
}

static void
create_nodes (node_table_t *nodes, size_t log_size, size_t sat_bits, int alloc,
              int error_num)
{
    nodes->size = 1ULL << log_size;
    nodes->log_size = log_size;
    nodes->thres = nodes->size / 64;
    nodes->thres = min (nodes->thres, 1ULL << 18);
    nodes->mask = nodes->size - 1;
    nodes->error_num = error_num;
    nodes->sat_bits = sat_bits;
    nodes->sat_mask = 0;
    nodes->sat_right = 32 - (sat_bits + 1) / 2;
    nodes->sat_left = nodes->sat_right * 2;
    for (uint64_t i = 0; i < sat_bits; i++)
        nodes->sat_mask |= bit_pos (sat_bits, i);
    nodes->sat_nmask = ~nodes->sat_mask;
    if (alloc) {
        nodes->table = RTalignZero (CACHE_LINE_SIZE, sizeof (uint64_t) * nodes->size);
        if (!nodes->table) {
            Abort ("Too large hash table allocated: %.1f GB", ((float)sizeof (uint64_t) * nodes->size/1024/1024/1024));
        }
    }
}

treedbs_ll_t
TreeDBSLLcreate_dm (int nNodes, int size, int ratio, matrix_t * m,
                    int satellite_bits, int slim, int indexing)
{
    HREassert (size <= DB_SIZE_MAX, "Tree too large: %d", size);
    HREassert (nNodes >= 2, "Tree vectors too small: %d", nNodes);
    HREassert ((slim == 0 || slim == 1), "Wrong value for slim: %d", slim);
    HREassert (satellite_bits + 2 * (size-ratio) <= 64,
               "Tree table size and satellite bits (%d) too large or ratio too "
               "low (%d).", satellite_bits, ratio);
    treedbs_ll_t        dbs = RTalign (CACHE_LINE_SIZE, sizeof(struct treedbs_ll_s));
    dbs->nNodes = nNodes;
    dbs->ratio = ratio;
    dbs->slim = slim;
    dbs->indexing = indexing || !slim || satellite_bits;
    dbs->todo = NULL;
    create_nodes (&dbs->root, size, satellite_bits, !dbs->slim, DB_ROOTS_FULL);
    create_nodes (&dbs->data, size - dbs->ratio, 0, 1, DB_LEAFS_FULL);
    if (dbs->slim)
        dbs->clt = clt_create (dbs->data.log_size*2, dbs->root.log_size);
    HREcreateLocal (&dbs->local_key, LOCALfree);
    if (m != NULL)
        project_matrix_to_tree(dbs, m);
    return dbs;
}

void
TreeDBSLLfree (treedbs_ll_t dbs)
{
    RTalignedFree (dbs->data.table);
    if (dbs->slim) {
        clt_free (dbs->clt);
    } else {
        RTalignedFree (dbs->root.table);
    }
    if (NULL != dbs->todo) {
        for(int row = 0;row < dbs->k;++row)
            RTalignedFree (dbs->todo[row]);
        RTalignedFree (dbs->todo);
    }
    RTalignedFree (dbs);
}

stats_t *
TreeDBSLLstats (treedbs_ll_t dbs)
{

    stats_t            *res = RTmalloc (sizeof (*res));
    loc_t              *loc = get_local (dbs);
    for (size_t i = 0; i < dbs->nNodes; i++)
        loc->stat.nodes += loc->node_count[i];
    loc->stat.elts = loc->node_count[0];
    memcpy (res, &loc->stat, sizeof (*res));
    return res;
}
