#include <config.h>

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include <hre/user.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/vector_set.h>
#include <mc-lib/atomics.h>
#include <util-lib/fast_hash.h>


#include <sylvan.h>

static int datasize = 22; // 23 = 128 MB
static int maxtablesize = 28;  // 28 = 8196 MB
static int cachesize = 24; // 24 = 576 MB
static int maxcachesize = 28; // 28 = 9216 MB

struct poptOption lddmc_options[]= {
    { "lddmc-tablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &datasize , 0 , "log2 initial size of LDD nodes table", "<tablesize>"},
    { "lddmc-maxtablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxtablesize , 0 , "log2 maximum size of LDD nodes table", "<maxtablesize>"},
    { "lddmc-cachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cachesize , 0 , "log2 size of memoization cache", "<cachesize>"},
    { "lddmc-maxcachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxcachesize , 0 , "log2 maximum size of memoization cache", "<maxcachesize>"},
    POPT_TABLEEND
};

#define DEBUG_MT 0

struct vector_domain {
    struct vector_domain_shared shared;
};

struct vector_set {
    vdom_t dom;

    MDD mdd;
    int size;
    MDD proj; // for set_project

#if DEBUG_MT
    int mtctr;
#endif
};

struct vector_relation {
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;

    MDD mdd;
    int size;
    MDD meta; // for set_next, set_prev

#if DEBUG_MT
    int mtctr;
#endif
};

#if DEBUG_MT
#define entermt(thing) assert(cas(&thing->mtctr, 0, 1))
#define leavemt(thing) assert(cas(&thing->mtctr, 1, 0))
#else
#define entermt(thing) {}
#define leavemt(thing) {}
#endif

static int
calculate_size(MDD meta)
{
    int result = 0;
    uint32_t val = lddmc_getvalue(meta);
    while (val != (uint32_t)-1) {
        if (val != 0) result += 1;
        meta = lddmc_follow(meta, val);
        assert(meta != lddmc_true && meta != lddmc_false);
        val = lddmc_getvalue(meta);
    }
    return result;
}

static vset_t
set_create(vdom_t dom, int k, int *proj)
{
    LACE_ME;

    assert(k <= dom->shared.size);
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));

    set->dom  = dom;
    set->mdd  = lddmc_false;
    set->size = k < 0 ? dom->shared.size : k;
#if DEBUG_MT
    set->mtctr = 0;
#endif

    int _proj[dom->shared.size+1];
    if (k < 0) {
        // set _proj to [-1] (= keep rest)
        _proj[0] = -1;
        set->proj = lddmc_ref(lddmc_cube((uint32_t*)_proj, 1));
    } else if (k == 0) {
        _proj[0] = -2;
        set->proj = lddmc_ref(lddmc_cube((uint32_t*)_proj, 1));
    } else {
        // fill _proj with 0 (= quantify)
        memset(_proj, 0, sizeof(int[dom->shared.size+1]));
        // set _proj to 1 (= keep) for every variable in proj
        for (int i=0; i<k; i++) _proj[proj[i]] = 1;
        // end sequence with -2 (= quantify rest)
        _proj[proj[k-1]+1] = -2;
        set->proj = lddmc_ref(lddmc_cube((uint32_t*)_proj, proj[k-1]+2));
    }

    return set;
}

static vrel_t
rel_create_rw(vdom_t dom, int r_k, int *r_proj, int w_k, int *w_proj)
{
    LACE_ME;

    assert(0 <= r_k && r_k <= dom->shared.size);
    assert(0 <= w_k && w_k <= dom->shared.size);
    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));

    rel->dom  = dom;
    rel->mdd  = lddmc_false;
    rel->size = r_k + w_k;
#if DEBUG_MT
    rel->mtctr = 0;
#endif

    uint32_t meta[dom->shared.size*2+1];
    memset(meta, 0, sizeof(uint32_t[dom->shared.size*2+1]));
    int r_i=0, w_i=0, i=0, j=0;
    for (;;) {
        int type = 0;
        if (r_i < r_k && r_proj[r_i] == i) {
            r_i++;
            type += 1; // read
        }
        if (w_i < w_k && w_proj[w_i] == i) {
            w_i++;
            type += 2; // write
        }
        if (type == 0) meta[j++] = 0;
        else if (type == 1) { meta[j++] = 3; }
        else if (type == 2) { meta[j++] = 4; }
        else if (type == 3) { meta[j++] = 1; meta[j++] = 2; }
        if (r_i == r_k && w_i == w_k) {
            meta[j++] = (uint32_t)-1;
            break;
        }
        i++;
    }
        
    rel->meta = lddmc_ref(lddmc_cube((uint32_t*)meta, j));

    return rel;
}

static vrel_t
rel_create(vdom_t dom, int k, int *proj)
{
    return rel_create_rw(dom, k, proj, k, proj);
}

static void
set_destroy(vset_t set)
{
    lddmc_deref(set->mdd);
    lddmc_deref(set->proj);
    RTfree(set);
}

static void
set_add(vset_t set, const int* e)
{
    entermt(set);
    LACE_ME;
    MDD old = set->mdd;
    set->mdd = lddmc_ref(lddmc_union_cube(set->mdd, (uint32_t*)e, set->size));
    lddmc_deref(old);
    leavemt(set);
}

static void
rel_add_cpy(vrel_t rel, const int *src, const int *dst, const int *cpy)
{
    entermt(rel);
    int i=0, j=0, k=0;

    uint32_t vec[rel->size];
    int cpy_vec[rel->size];

    MDD meta = rel->meta;
    for (;;) {
        const uint32_t v = lddmc_getvalue(meta);
        if (v == 1 || v == 3) {
            // read or only-read
            cpy_vec[k] = 0; // not supported yet for read levels
            vec[k++] = src[i++];
        } else if (v == 2 || v == 4) {
            cpy_vec[k] = (v == 4 && cpy && cpy[j]) ? 1 : 0;
            vec[k++] = dst[j++];
        } else if (v == (uint32_t)-1) {
            break;
        }
        meta = lddmc_follow(meta, v);
    }

    assert(k == rel->size);

    LACE_ME;
    MDD old = rel->mdd;
    rel->mdd = lddmc_ref(lddmc_union_cube_copy(rel->mdd, (uint32_t*)vec, cpy_vec, k));
    lddmc_deref(old);
    leavemt(rel);
}

static void
rel_add(vrel_t rel, const int *src, const int *dst)
{
    return rel_add_cpy(rel, src, dst, NULL);
}

static int
set_is_empty(vset_t set)
{
    return (set->mdd == lddmc_false);
}

static int
set_equal(vset_t set1, vset_t set2)
{
    assert(set1->proj == set2->proj);
    assert(set1->size == set2->size);
    return set1->mdd == set2->mdd;
}

static void
set_clear(vset_t set)
{
    entermt(set);
    lddmc_deref(set->mdd);
    set->mdd = lddmc_false;
    leavemt(set);
}

static void
set_copy(vset_t dst, vset_t src)
{
    entermt(dst);
    assert(dst->size == src->size);
    lddmc_deref(dst->mdd);
    dst->mdd = lddmc_ref(src->mdd);
    leavemt(dst);
}

static int
set_member(vset_t set, const int* e)
{
    return lddmc_member_cube(set->mdd, (uint32_t*)e, set->size);
}

static void
set_count(vset_t set, long *nodes, double *elements)
{
    entermt(set);
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount_cached(set->mdd);
    leavemt(set);
}

static void
set_ccount(vset_t set, long *nodes, long double *elements)
{
    entermt(set);
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount(set->mdd);
    leavemt(set);
}

/* For counting precisely we use a thread-safe operation cache that allows overwriting entries.
 * Invariants for read and write locks:
 *  - if there is a write lock then the read lock me not be incremented
 *  - the write lock can only be claimed by one thread
 *  - a cache entry is only changed when there are no read locks
 *  - a cache entry is only changed by a thread when that thread has claimed the write lock
 * The implementation will:
 *  - wait for an entry to become write-unlocked when getting an entry from the cache
 *      I think waiting is beneficial, because the entry will probably get the right data.
 *  - not wait for entry to become write-unlocked when putting an entry in the cache
 *
 *  I have left some code (which is commented out) which does not overwrite a cache entry.
 *  Not overwriting requires a cache minimally 2^3 as large to achieve the same performance.
 *  A smaller operation cache for bignums is nice because it saves a lot of memory.
 *  The worst (and probably average) amount of initialized bignums with this implementation is
 *  num_lace_workers * 3 (down, right, me) + cache size. */

#define BN_WRITE_LOCK ((uint64_t) 0x8000000000000000)
#define BN_READ_LOCK  ((uint64_t) 0x7FFFFC0000000000)
#define BN_MDD ((uint64_t) 0x3FFFFFFFFFF)

#define BN_IS_WRITE_UNLOCKED(foo) ((foo & BN_READ_LOCK) | (foo & ~BN_WRITE_LOCK) | (foo & BN_MDD))
#define BN_READ_LOCK_INC(foo) (((((foo & BN_READ_LOCK) >> 42) + 1) << 42) | (foo & BN_MDD))
#define BN_READ_LOCK_DEC(foo) (((((foo & BN_READ_LOCK) >> 42) - 1) << 42) | (foo & BN_MDD) | (foo & BN_WRITE_LOCK))

struct bignum_cache {
    uint64_t node; /* 1 bit for write lock, 21 bits for read-counter, 42 bits for MDD */
    bn_int_t bignum;
};

struct bignum_info {
    bn_int_t*               bignum_false;
    bn_int_t*               bignum_true;
    struct bignum_cache*    bignum_cache;
    uint64_t                bignum_cache_mask;
    uint64_t                slot;
    bn_int_t                bignum;
    struct bignum_info*     down;
    struct bignum_info*     right;
};


TASK_2(int, lddmc_cache_get, MDD, mdd, void*, context)
{
    struct bignum_info* ctx = (struct bignum_info*) context;

    if (mdd == lddmc_false) {
        bn_init_copy(&ctx->bignum, ctx->bignum_false);
        return 0;
    }
    if (mdd == lddmc_true) {
        bn_init_copy(&ctx->bignum, ctx->bignum_true);
        return 0;
    }
    ctx->slot = MurmurHash64(&mdd, sizeof(MDD), 0) & ctx->bignum_cache_mask;

    // uncomment this to not overwrite the cache
    //for (;;) {
    //    uint64_t cache_node = atomic_read(&ctx->bignum_cache[ctx->slot].node);
    //    if ((cache_node & BN_MDD) != mdd) return 0;
    //    if (!(cache_node & BN_WRITE_LOCK)) {
    //        bn_init_copy(&ctx->bignum, &ctx->bignum_cache[ctx->slot].bignum);
    //        return 1;
    //    }
    //}
    //return 0;

    /* acquire a read lock when there is no write lock. Relax bus,
     * because we may need to wait for some read locks and a write lock. */
    volatile uint64_t* ptr = &ctx->bignum_cache[ctx->slot].node;
    uint64_t n = *ptr;
    while (!cas(ptr, BN_IS_WRITE_UNLOCKED(n), BN_READ_LOCK_INC(n))) {
        cpu_relax();
        n = *ptr;
    }

    /* use result if same node */
    int res = 1;
    if ((n & BN_MDD) == mdd) {
        bn_init_copy(&ctx->bignum, &ctx->bignum_cache[ctx->slot].bignum);
        res = 0;
    }

    /* release read lock */
    do n = *ptr;
    while (!cas(ptr, n, BN_READ_LOCK_DEC(n)));

    return res;
}

VOID_TASK_3(lddmc_copy_context, void*, context, void*, parent, int, succ)
{
    struct bignum_info* ctx = (struct bignum_info*) context;

    struct bignum_info* p = (struct bignum_info*) parent;

    ctx->bignum_false = p->bignum_false;
    ctx->bignum_true = p->bignum_true;
    ctx->bignum_cache = p->bignum_cache;
    ctx->bignum_cache_mask = p->bignum_cache_mask;

    if (succ == 1) p->down = ctx;
    if (succ == 0) p->right = ctx;
}

VOID_TASK_2(lddmc_cache_put, MDD, mdd, void*, context)
{
    struct bignum_info* ctx = (struct bignum_info*) context;

    bn_init(&ctx->bignum);
    bn_add(&ctx->down->bignum, &ctx->right->bignum, &ctx->bignum);
    bn_clear(&ctx->down->bignum);
    bn_clear(&ctx->right->bignum);

    // uncomment this to not overwrite the cache
    //if (cas(&ctx->bignum_cache[ctx->slot].node, 0, mdd | BN_WRITE_LOCK)) {
    //    // major victory
    //    bn_init_copy(&ctx->bignum_cache[ctx->slot].bignum, &ctx->bignum);
    //    // release lock
    //    atomic_write(&ctx->bignum_cache[ctx->slot].node, mdd & BN_MDD);
    //}

    /* acquire the write lock, but don't wait for unlock */
    volatile uint64_t* ptr = &ctx->bignum_cache[ctx->slot].node;
    uint64_t n;
    do {
        n = *ptr;
        if (n & BN_WRITE_LOCK) return;
    } while (!cas(ptr, n & ~BN_WRITE_LOCK, n | BN_WRITE_LOCK));

    /* Wait for all read locks to be released.
     * Also relax bus, because we may need to wait for a number
     * of read locks to be released */
    n = *ptr;
    while (n & BN_READ_LOCK) { cpu_relax(); n = *ptr; }

    /* clear bignum only if there was one previously. */
    if ((n & BN_MDD) != 0) bn_clear(&ctx->bignum_cache[ctx->slot].bignum);

    /* put bignum in cache */
    bn_init_copy(&ctx->bignum_cache[ctx->slot].bignum, &ctx->bignum);

    /* write the node number, note that this will also release the write lock */
    *ptr = mdd;
}

/* compute most significant high bit */
static inline size_t
mshb(long num)
{
    size_t r = 0;
    while (num >>= 1) r++;
    return r;
}

static void
set_count_precise(vset_t set, long *nodes, bn_int_t *elements)
{
    *nodes = lddmc_nodecount(set->mdd);

    struct bignum_info ctx;

    bn_int_t bignum_false;
    bn_init(&bignum_false);
    ctx.bignum_false = &bignum_false;

    bn_int_t bignum_true;
    bn_init(&bignum_true);
    bn_set_digit(&bignum_true, 1);
    ctx.bignum_true = &bignum_true;

    uint64_t cache_size;
    if (_cache_diff() <= 0) {
        cache_size = pow(2, mshb(*nodes >> (_cache_diff() * -1)));
    } else {
        cache_size = pow(2, mshb(*nodes << _cache_diff()));
    }

    Warning(infoLong, "Bignum cache size is %" PRIu64 " entries", cache_size);

    ctx.bignum_cache_mask = cache_size - 1;
    ctx.bignum_cache = RTalignZero(CACHE_LINE_SIZE, cache_size * sizeof(struct bignum_cache));

    lddmc_visit_callbacks_t cbs;
    cbs.lddmc_visit_pre = TASK(lddmc_cache_get);
    cbs.lddmc_visit_init_context = TASK(lddmc_copy_context);
    cbs.lddmc_visit_post = TASK(lddmc_cache_put);

    LACE_ME;
    lddmc_visit_par(set->mdd, &cbs, sizeof(struct bignum_info), &ctx);

    bn_init_copy(elements, &ctx.bignum);

    for (uint64_t i = 0; i < cache_size; i++) {
        if (ctx.bignum_cache[i].node) bn_clear(&ctx.bignum_cache[i].bignum);
    }

    RTfree(ctx.bignum_cache);
}

static void
rel_count(vrel_t rel, long *nodes, double *elements)
{
    entermt(rel);
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(rel->mdd);
    if (elements != NULL) *elements = lddmc_satcount(rel->mdd);
    leavemt(rel);
}

static void
set_union(vset_t dst, vset_t src)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_union(dst->mdd, src->mdd));
    lddmc_deref(old);
    leavemt(dst);
}

/**
 * Add all elements of src to dst and remove all elements that were in dst already from src
 * in other words: newDst = dst + src
 *                 newSrc = src - dst
 */
static void
set_zip(vset_t dst, vset_t src)
{
    entermt(dst);entermt(src);
    LACE_ME;
    assert(dst->size == src->size);
    MDD old1 = dst->mdd;
    MDD old2 = src->mdd;
    dst->mdd = lddmc_ref(lddmc_zip(dst->mdd, src->mdd, &src->mdd));
    lddmc_ref(src->mdd);
    lddmc_deref(old1);
    lddmc_deref(old2);
    leavemt(dst);leavemt(src);
}

static void
set_minus(vset_t dst, vset_t src)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_minus(dst->mdd, src->mdd));
    lddmc_deref(old);
    leavemt(dst);
}

static void
set_intersect(vset_t dst, vset_t src)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_intersect(dst->mdd, src->mdd));
    lddmc_deref(old);
    leavemt(dst);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->dom->shared.size);
    assert(src->size == src->dom->shared.size);

    lddmc_deref(dst->mdd);

    if (p_len == 0) dst->mdd = lddmc_ref(src->mdd);
    else {
        vdom_t dom = src->dom;
        int _proj[dom->shared.size+1];
        // fill _proj with 0 (= not in match)
        memset(_proj, 0, sizeof(int[dom->shared.size+1]));
        // set _proj to 1 (= match) for every variable in proj
        for (int i=0; i<p_len; i++) _proj[proj[i]] = 1;
        // end sequence with -1 (= rest not in match)
        _proj[proj[p_len-1]+1] = -1;
        MDD mdd_proj = lddmc_ref(lddmc_cube((uint32_t*)_proj, proj[p_len-1]+2));

        MDD cube = lddmc_ref(lddmc_cube((uint32_t*)match, p_len));

        dst->mdd = lddmc_ref(lddmc_match(src->mdd, cube, mdd_proj));
        lddmc_deref(cube);
        lddmc_deref(mdd_proj);
    }
    leavemt(dst);
}

struct enum_context
{
    vset_element_cb cb;
    void* context;
};

VOID_TASK_3(enumer, uint32_t*, values, size_t, count, struct enum_context*, ctx)
{
    ctx->cb(ctx->context, (int*)values);
    (void)count;
}

static void
set_enum(vset_t set, vset_element_cb cb, void* context)
{
    LACE_ME;
    struct enum_context ctx = (struct enum_context){cb, context};
    lddmc_sat_all_nopar(set->mdd, (lddmc_enum_cb)TASK(enumer), &ctx);
}

struct set_update_context
{
    vset_t set;
    vset_update_cb cb;
    void* context;
};

TASK_3(MDD, set_updater, uint32_t*, values, size_t, count, struct set_update_context*, ctx)
{
    struct vector_set dummyset;
    dummyset.dom = ctx->set->dom;
    dummyset.mdd = lddmc_false; // start with empty set
    dummyset.size = ctx->set->size; // same as result
    dummyset.proj = ctx->set->proj;
#if DEBUG_MT
    dummyset.mtctr = 0;
#endif
    ctx->cb(&dummyset, ctx->context, (int*)values);
    lddmc_deref(dummyset.mdd); // return without ref
    return dummyset.mdd;
    (void)count;
}

static void
set_update(vset_t dst, vset_t set, vset_update_cb cb, void* context)
{
    entermt(dst);
    LACE_ME;
    struct set_update_context ctx = (struct set_update_context){dst, cb, context};
    MDD old = dst->mdd;
    MDD result = lddmc_ref(lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(set_updater), &ctx));
    dst->mdd = lddmc_ref(lddmc_union(dst->mdd, result));
    lddmc_deref(old);
    lddmc_deref(result);
    leavemt(dst);
}

struct rel_update_context
{
    vrel_t rel;
    vrel_update_cb cb;
    void* context;
};

TASK_3(MDD, rel_updater, uint32_t*, values, size_t, count, struct rel_update_context*, ctx)
{
    struct vector_relation dummyrel;
    dummyrel.dom = ctx->rel->dom;
    dummyrel.mdd = lddmc_false; // start with empty set
    dummyrel.size = ctx->rel->size; // same as result
    dummyrel.meta = ctx->rel->meta;
#if DEBUG_MT
    dummyrel.mtctr = 0;
#endif
    ctx->cb(&dummyrel, ctx->context, (int*)values);
    lddmc_deref(dummyrel.mdd); // return without ref
    return dummyrel.mdd;
    (void)count;
}

static void
rel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void* context)
{
    entermt(rel);
    LACE_ME;
    struct rel_update_context ctx = (struct rel_update_context){rel, cb, context};
    MDD old = rel->mdd;
    MDD result = lddmc_ref(lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(rel_updater), &ctx));
    rel->mdd = lddmc_ref(lddmc_union(rel->mdd, result));
    lddmc_deref(old);
    lddmc_deref(result);
    leavemt(rel);
}

static void
set_enum_match(vset_t set, int p_len, int *proj, int *match, vset_element_cb cb, void *context)
{
    LACE_ME;
    assert(set->size == set->dom->shared.size);
    assert(p_len > 0);

    vdom_t dom = set->dom;
    int _proj[dom->shared.size+1];
    // fill _proj with 0 (= not in match)
    memset(_proj, 0, sizeof(int[dom->shared.size+1]));
    // set _proj to 1 (= match) for every variable in proj
    for (int i=0; i<p_len; i++) _proj[proj[i]] = 1;
    // end sequence with -1 (= rest not in match)
    _proj[proj[p_len-1]+1] = -1;

    MDD mdd_proj = lddmc_ref(lddmc_cube((uint32_t*)_proj, proj[p_len-1]+2));
    MDD cube = lddmc_ref(lddmc_cube((uint32_t*)match, p_len));

    struct enum_context ctx = (struct enum_context){cb, context};
    lddmc_match_sat_par(set->mdd, cube, mdd_proj, (lddmc_enum_cb)TASK(enumer), &ctx);
    lddmc_deref(cube);
    lddmc_deref(mdd_proj);
}

static void
set_project(vset_t dst, vset_t src)
{
    if (dst->proj == src->proj) {
        lddmc_deref(dst->mdd);
        dst->mdd = src->mdd;
    } else {
        entermt(dst);
        LACE_ME;
        assert(src->size == dst->dom->shared.size);
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_project(src->mdd, dst->proj));
        leavemt(dst);
    }
}

static void
set_project_minus(vset_t dst, vset_t src, vset_t minus)
{
    if (dst->proj == src->proj) {
        set_minus(dst, minus);
    } else {
        entermt(dst);
        LACE_ME;
        assert(src->size == dst->dom->shared.size);
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_project_minus(src->mdd, dst->proj, minus->mdd));
        leavemt(dst);
    }
}

static void
set_next(vset_t dst, vset_t src, vrel_t rel)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size);
    if (dst == src) {
        MDD old = dst->mdd;
        dst->mdd = lddmc_ref(lddmc_relprod(src->mdd, rel->mdd, rel->meta));
        lddmc_deref(old);
    } else {
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_relprod(src->mdd, rel->mdd, rel->meta));
    }
    leavemt(dst);
}

static void
set_next_union(vset_t dst, vset_t src, vrel_t rel, vset_t uni)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size && uni->size == dst->size);
    if (dst == src) {
        MDD old = dst->mdd;
        dst->mdd = lddmc_ref(lddmc_relprod_union(src->mdd, rel->mdd, rel->meta, uni->mdd));
        lddmc_deref(old);
    } else {
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_relprod_union(src->mdd, rel->mdd, rel->meta, uni->mdd));
    }
    leavemt(dst);
}

static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t universe)
{
    entermt(dst);
    LACE_ME;
    assert(dst->size == src->size);
    assert(dst->size == universe->size);
    if (dst == src) {
        MDD old = dst->mdd;
        dst->mdd = lddmc_ref(lddmc_relprev(src->mdd, rel->mdd, rel->meta, universe->mdd));
        lddmc_deref(old);
    } else {
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_relprev(src->mdd, rel->mdd, rel->meta, universe->mdd));
    }
    leavemt(dst);
}

static void
set_example(vset_t set, int *e)
{
    if (set->mdd == lddmc_false) Abort("set_example: empty set");
    lddmc_sat_one(set->mdd, (uint32_t*)e, set->size);
}

static void
set_join(vset_t dst, vset_t left, vset_t right)
{
    entermt(dst);
    LACE_ME;
    if (dst == left || dst == right) {
        MDD old = dst->mdd;
        dst->mdd = lddmc_ref(lddmc_join(left->mdd, right->mdd, left->proj, right->proj));
        lddmc_deref(old);
    } else {
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_join(left->mdd, right->mdd, left->proj, right->proj));
    }
    leavemt(dst);
}

static void
set_dot(FILE* fp, vset_t src)
{
    lddmc_fprintdot(fp, src->mdd);
}

static void
rel_dot(FILE* fp, vrel_t src)
{
    lddmc_fprintdot(fp, src->mdd);
}

static void
set_reorder()
{
    // ignore
}

/* Storing to file */
static void
serialize_reset(FILE* f, vdom_t dom)
{
    lddmc_serialize_reset();
    return;
    (void)f;
    (void)dom;
}

static void
set_save(FILE* f, vset_t set)
{
    size_t mdd = lddmc_serialize_add(set->mdd);
    size_t proj = lddmc_serialize_add(set->proj);
    lddmc_serialize_tofile(f);
    fwrite(&mdd, sizeof(size_t), 1, f);
    fwrite(&proj, sizeof(size_t), 1, f);
    fwrite(&set->size, sizeof(int), 1, f);;
}

static void
rel_save_proj(FILE* f, vrel_t rel)
{
    return; // ignore
    (void)f;
    (void)rel;
}

static void
rel_save(FILE* f, vrel_t rel)
{
    size_t mdd = lddmc_serialize_add(rel->mdd);
    size_t meta = lddmc_serialize_add(rel->meta);
    lddmc_serialize_tofile(f);
    fwrite(&mdd, sizeof(size_t), 1, f);
    fwrite(&meta, sizeof(size_t), 1, f);
}

static vset_t
set_load(FILE* f, vdom_t dom)
{
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));
    set->dom = dom;

    lddmc_serialize_fromfile(f);

    size_t mdd, proj;
    fread(&mdd, sizeof(size_t), 1, f);
    fread(&proj, sizeof(size_t), 1, f);
    fread(&set->size, sizeof(int), 1, f);
    set->mdd = lddmc_ref(lddmc_serialize_get_reversed(mdd));
    set->proj = lddmc_ref(lddmc_serialize_get_reversed(proj));

    return set;
}

static vrel_t
rel_load_proj(FILE* f, vdom_t dom)
{
    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));
    memset(rel, 0, sizeof(struct vector_relation));
    rel->dom = dom;
    return rel;
    (void)f;
}

static void
rel_load(FILE* f, vrel_t rel)
{
    if (rel->mdd) lddmc_deref(rel->mdd);
    if (rel->meta) lddmc_deref(rel->meta);

    lddmc_serialize_fromfile(f);

    size_t mdd, meta;
    fread(&mdd, sizeof(size_t), 1, f);
    fread(&meta, sizeof(size_t), 1, f);
    rel->mdd = lddmc_ref(lddmc_serialize_get_reversed(mdd));
    rel->meta = lddmc_ref(lddmc_serialize_get_reversed(meta));
    rel->size = calculate_size(rel->meta);
}

static void
dom_save(FILE* f, vdom_t dom)
{
    size_t vector_size = dom->shared.size;
    fwrite(&vector_size, sizeof(size_t), 1, f);
}

static int
separates_rw()
{
    return 1;
}

static int
supports_cpy()
{
    return 1;
}

static void
set_function_pointers(vdom_t dom)
{
    /* Set function pointers */
    dom->shared.set_create=set_create;
    dom->shared.set_destroy=set_destroy;
    dom->shared.set_is_empty=set_is_empty;
    dom->shared.set_equal=set_equal;
    dom->shared.set_clear=set_clear;
    dom->shared.set_copy=set_copy;
    dom->shared.set_count=set_count;
    dom->shared.set_ccount=set_ccount;
    dom->shared.set_count_precise=set_count_precise;
    dom->shared.set_project=set_project;
    dom->shared.set_project_minus=set_project_minus;
    dom->shared.set_union=set_union;
    dom->shared.set_intersect=set_intersect;
    dom->shared.set_minus=set_minus;
    dom->shared.set_zip=set_zip;

    dom->shared.rel_create=rel_create;
    dom->shared.rel_create_rw=rel_create_rw;
    //dom->shared.rel_destroy=rel_destroy;
    dom->shared.rel_add=rel_add;
    dom->shared.rel_add_cpy=rel_add_cpy;
    dom->shared.rel_count=rel_count;
    dom->shared.rel_update=rel_update;

    dom->shared.set_add=set_add;
    dom->shared.set_update=set_update;
    dom->shared.set_member=set_member;
    dom->shared.set_example=set_example;
    dom->shared.set_enum=set_enum;
    dom->shared.set_enum_match=set_enum_match;
    dom->shared.set_copy_match=set_copy_match;

    dom->shared.set_next=set_next;
    dom->shared.set_next_union=set_next_union;
    dom->shared.set_prev=set_prev;
    dom->shared.set_join=set_join;
    //dom->shared.set_least_fixpoint=set_least_fixpoint;
	//void (*set_least_fixpoint)(vset_t dst,vset_t src,vrel_t rels[],int rel_count);

	//void (*set_copy_match_proj)(vset_t src,vset_t dst,int p_len,int* proj,int p_id,int*match);
	//int (*proj_create)(int p_len,int* proj);

    dom->shared.reorder=set_reorder;

    /* DOT output */
    dom->shared.set_dot=set_dot;
    dom->shared.rel_dot=rel_dot;

    /* Synchronization */
    dom->shared.pre_save=serialize_reset;
    dom->shared.post_save=serialize_reset;
    dom->shared.pre_load=serialize_reset;
    dom->shared.post_load=serialize_reset;

    dom->shared.dom_save=dom_save;
    dom->shared.set_save=set_save;
    dom->shared.rel_save_proj=rel_save_proj;
    dom->shared.rel_save=rel_save;
    dom->shared.set_load=set_load;
    dom->shared.rel_load_proj=rel_load_proj;
    dom->shared.rel_load=rel_load;

    dom->shared.separates_rw=separates_rw;
    dom->shared.supports_cpy=supports_cpy;
}

vdom_t
vdom_create_lddmc(int n)
{
    Warning(info,"Creating a multi-core ListDD domain.");

    /* Initialize library if necessary */
    static int initialized = 0;
    if (!initialized) {
        sylvan_init_package(1LL<<datasize, 1LL<<maxtablesize, 1LL<<cachesize, 1LL<<maxcachesize);
        sylvan_init_ldd();
        initialized = 1;
    }

    /* Create data structure */
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom, n);
    set_function_pointers(dom);
    return dom;
}

vdom_t
vdom_create_lddmc_from_file(FILE *f)
{
    size_t vector_size;
    fread(&vector_size, sizeof(size_t), 1, f);
    return vdom_create_lddmc((int)vector_size);
}
