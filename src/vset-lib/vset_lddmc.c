#include <config.h>

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include <hre/user.h>
#include <vset-lib/vdom_object.h>
#include <vset-lib/vector_set.h>
#include <mc-lib/atomics.h>
#include <util-lib/fast_hash.h>

#include <sylvan_cache.h>
#include <sylvan.h>

struct vector_domain {
    struct vector_domain_shared shared;
};

struct vector_set {
    vdom_t dom;

    MDD mdd;          // LDD of the set
    int k;            // projection size or -1 if not projecting
    int *proj;        // if projecting, the variables in the set

    // The following variables are computed based on k and proj

    int size;         // size of state vectors in this set
    MDD meta;         // "meta" for set_project (vs full vector)
};

struct vector_relation {
    vdom_t dom;

    expand_cb expand; // callback for on-the-fly learning in saturation
    void *expand_ctx; // callback parameter

    MDD mdd;          // LDD of the relation

    int r_k, w_k;     // number of read/write in this relation
    int *r_proj;
    int *w_proj;

    // The following variables are computed based on the above variables

    int size;         // depth of the LDD
    MDD meta;         // "meta" for set_next, set_prev

    // The following variables are for the Saturation implementation

    int topvar;       // top variable depth (vs full vector)
    MDD topmeta;      // "meta" for set_next, set_prev (at <topvar>)
    MDD topread;      // "meta" for getting short read vectors (at <topvar>)
    MDD r_meta;       // meta of read vector (relative to full)
};

/**
 * Create a vset holding <k>-sized vectors according to <proj>.
 */
static vset_t
set_create(vdom_t dom, int k, int *proj)
{
    LACE_ME;

    assert(k == -1 || (k >= 0 && k <= dom->shared.size));
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));

    set->dom  = dom;
    set->mdd  = lddmc_false;
    set->k    = k;
    set->proj = k == -1 ? NULL : (int*)RTmalloc(sizeof(int[k]));
    if (k != -1) memcpy(set->proj, proj, sizeof(int[k]));

    // compute size
    set->size = k == -1 ? dom->shared.size : k;

    // compute meta
    if (k == -1) {
        // set meta to [-1] (= keep rest)
        uint32_t meta = (uint32_t)-1;
        set->meta = lddmc_cube(&meta, 1);
    } else if (k == 0) {
        // set meta to [-2] (= quantify rest)
        uint32_t meta = (uint32_t)-2;
        set->meta = lddmc_cube(&meta, 1);
    } else {
        // set meta to 1 (= keep) for every variable in proj
        uint32_t meta[dom->shared.size+1];
        memset(meta, 0, sizeof(int[dom->shared.size+1]));
        for (int i=0; i<k; i++) meta[proj[i]] = 1;
        // end sequence with -2 (= quantify rest)
        meta[proj[k-1]+1] = (uint32_t)-2;
        set->meta = lddmc_cube(meta, proj[k-1]+2);
    }

    lddmc_protect(&set->mdd);
    lddmc_protect(&set->meta);

    return set;
}

/**
 * Destroy a vset
 */
static void
set_destroy(vset_t set)
{
    lddmc_unprotect(&set->mdd);
    lddmc_unprotect(&set->meta);
    if (set->k != -1) RTfree(set->proj);
    RTfree(set);
}

/**
 * Add an element to the set.
 * If the set is a projected vector, then <e> is a projected state.
 */
static void
set_add(vset_t set, const int* e)
{
    LACE_ME;
    set->mdd = lddmc_union_cube(set->mdd, (uint32_t*)e, set->size);
}

/**
 * Returns nonzero if the given set contains no elements.
 */
static int
set_is_empty(vset_t set)
{
    return set->mdd == lddmc_false;
}

/**
 * Returns nonzero if the given sets are identical (same proj and dd)
 */
static int
set_equal(vset_t set1, vset_t set2)
{
    assert(set1->meta == set2->meta);
    return set1->mdd == set2->mdd;
}

/**
 * Empties the given set.
 */
static void
set_clear(vset_t set)
{
    set->mdd = lddmc_false;
}

/**
 * Copies the set <src> to <dst>.
 */
static void
set_copy(vset_t dst, vset_t src)
{
    assert(dst->meta == src->meta);
    dst->mdd = src->mdd;
}

/**
 * Returns nonzero if the given set contains the given element.
 */
static int
set_member(vset_t set, const int* e)
{
    return lddmc_member_cube(set->mdd, (uint32_t*)e, set->size);
}

/**
 * Count the number of elements and nodes of the given set.
 */
static void
set_count(vset_t set, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount_cached(set->mdd);
}

/**
 * Count the number of elements and nodes of the given set.
 */
static void
set_ccount(vset_t set, long *nodes, long double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount(set->mdd);
}

/**
 * Add all elements of <src> to <dst>.
 * (Thread-safe version for if multiple threads add to dst)
 */
static void
set_union(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(src->meta == dst->meta);
    if (dst != src) {
        MDD cur = dst->mdd;
        MDD res = src->mdd;
        lddmc_refs_pushptr(&cur);
        lddmc_refs_pushptr(&res);
        for (;;) {
            res = lddmc_union(cur, res);
            MDD test = __sync_val_compare_and_swap(&dst->mdd, cur, res);
            if (test == cur) break;
            else cur = test;
        }
        lddmc_refs_popptr(2);
    }
}

/**
 * Add all elements of src to dst and remove all elements that were in dst from src
 * i.e.: newDst = dst + src; newSrc = src - dst
 */
static void
set_zip(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(src->meta == dst->meta);
    dst->mdd = lddmc_zip(dst->mdd, src->mdd, &src->mdd);
}

/**
 * Remove all elements in <src> from <dst>.
 */
static void
set_minus(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(src->meta == dst->meta);
    dst->mdd = lddmc_minus(dst->mdd, src->mdd);
}

/**
 * Intersect <dst> with <src>.
 */
static void
set_intersect(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(src->meta == dst->meta);
    dst->mdd = lddmc_intersect(dst->mdd, src->mdd);
}

/**
 * Compute the match of <src> with short vector <match> into <dst>.
 */
static void
set_copy_match(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
    LACE_ME;

    assert(dst->meta == src->meta); // for now, require same meta

    if (p_len == 0) {
        dst->mdd = src->mdd;
    } else {
        const int vector_size = src->dom->shared.size;
        uint32_t meta[vector_size+1];
        int j=0; // current index in src proj
        int k=0; // current index in match proj
        for (int i=0; i<vector_size; i++) {
            if (k == p_len) break; // end of match
            if (src->k == -1 || src->proj[j] == i) {
                if (proj[k] == i) meta[j++] = 1;
                else meta[j++] = 0;
            }
        }
        meta[j++] = -1; // = rest not in match
        MDD meta_mdd = lddmc_refs_push(lddmc_cube(meta, j));
        MDD cube = lddmc_refs_push(lddmc_cube((uint32_t*)match, p_len));
        dst->mdd = lddmc_match(src->mdd, cube, meta_mdd);
        lddmc_refs_pop(2);
    }
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

/**
 * Enumerate (sequentially) all elements in <set> by callback.
 */
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
    memcpy(&dummyset, ctx->set, sizeof(struct vector_set));
    dummyset.mdd = lddmc_false; // start with empty set
    lddmc_refs_pushptr(&dummyset.mdd);
    ctx->cb(&dummyset, ctx->context, (int*)values);
    lddmc_refs_popptr(1);
    return dummyset.mdd;
    (void)count;
}

/**
 * For every element in <set>, call the callback, and
 * collect all results via union into <dst>.
 */
static void
set_update(vset_t dst, vset_t set, vset_update_cb cb, void* context)
{
    LACE_ME;
    struct set_update_context ctx = (struct set_update_context){dst, cb, context};
    MDD result = lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(set_updater), &ctx);
    lddmc_refs_push(result);
    dst->mdd = lddmc_union(dst->mdd, result);
    lddmc_refs_pop(1);
}

/**
 * Enumerate all states matching a certain (short) state
 */
static void
set_enum_match(vset_t set, int p_len, int *proj, int *match, vset_element_cb cb, void *context)
{
    assert(p_len >= 0); // sanity check

    // compute the match projection relative to the set projection
    const int vector_size = set->dom->shared.size;
    uint32_t meta[vector_size+1];
    int j=0; // current index in set proj
    int k=0; // current index in match proj
    for (int i=0; i<vector_size; i++) {
        if (k == p_len) break; // end of match
        if (set->k == -1 || set->proj[j] == i) {
            if (proj[k] == i) meta[j++] = 1;
            else meta[j++] = 0;
        }
    }
    meta[j++] = -1; // = rest not in match

    LACE_ME;
    MDD meta_mdd = lddmc_refs_push(lddmc_cube(meta, j));

    MDD cube = lddmc_refs_push(lddmc_cube((uint32_t*)match, p_len));
    struct enum_context ctx = (struct enum_context){.cb=cb, .context=context};
    lddmc_match_sat_par(set->mdd, cube, meta_mdd, (lddmc_enum_cb)TASK(enumer), &ctx);
    lddmc_refs_pop(2);
}

/**
 * Project vector <src> into vector <dst>
 * The projection of src must be >= the projection of dst.
 */
static void
set_project(vset_t dst, vset_t src)
{
    if (dst->meta == src->meta) {
        dst->mdd = src->mdd;
    } else if (src->k == -1) {
        LACE_ME;
        dst->mdd = lddmc_project(src->mdd, dst->meta);
    } else {
        // compute a custom meta
        assert(src->k >= dst->k);
        uint32_t meta[src->dom->shared.size+1];
        int i=0; // index of src->proj
        int j=0; // index of dst->proj
        while (i<src->k && j<dst->k) {
            if (src->proj[i] < dst->proj[j]) {
                meta[i++] = 0;
            } else if (src->proj[i] == dst->proj[j]) {
                meta[i++] = 1;
                j++;
            } else {
                assert(src->proj[i] <= dst->proj[j]);
            }
        }
        meta[i++] = -2; // = quantify rest
        LACE_ME;
        MDD mdd_meta = lddmc_cube(meta, i);
        lddmc_refs_push(mdd_meta);
        dst->mdd = lddmc_project(src->mdd, mdd_meta);
        lddmc_refs_pop(1);
    }
}

/**
 * Project full vector <src> into short vector <dst> except (short) <minus>.
 */
static void
set_project_minus(vset_t dst, vset_t src, vset_t minus)
{
    if (dst->meta == src->meta) {
        set_minus(dst, minus);
    } else if (src->k == -1) {
        LACE_ME;
        assert(dst->meta == minus->meta);
        dst->mdd = lddmc_project_minus(src->mdd, dst->meta, minus->mdd);
    } else {
        // compute a custom meta
        assert(src->k >= dst->k);
        assert(dst->meta == minus->meta);
        uint32_t meta[src->dom->shared.size+1];
        int i=0, j=0;
        while (i<src->k && j<dst->k) {
            if (src->proj[i] < dst->proj[j]) {
                meta[i++] = 0;
            } else if (src->proj[i] == dst->proj[j]) {
                meta[i++] = 1;
                j++;
            } else {
                assert(src->proj[i] <= dst->proj[j]);
            }
        }
        meta[i++] = -2; // = quantify rest
        LACE_ME;
        MDD mdd_meta = lddmc_cube(meta, i);
        lddmc_refs_push(mdd_meta);
        dst->mdd = lddmc_project_minus(src->mdd, mdd_meta, minus->mdd);
        lddmc_refs_pop(1);
    }
}

static void
set_example(vset_t set, int *e)
{
    if (set->mdd == lddmc_false) Abort("set_example: empty set");
    lddmc_sat_one(set->mdd, (uint32_t*)e, set->size);
}

/**
 * Compute the intersection of two short vectors into <dst>.
 * The vectors can be different projections.
 */
static void
set_join(vset_t dst, vset_t left, vset_t right)
{
    LACE_ME;
    dst->mdd = lddmc_join(left->mdd, right->mdd, left->meta, right->meta);
}

/**
 * Create a transition relation, with r_k read variables and w_k write variables.
 */
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
    rel->topvar = -1;
    rel->meta = lddmc_false;
    rel->topmeta = lddmc_false;
    rel->topread = lddmc_false;
    rel->r_meta = lddmc_false;

    lddmc_protect(&rel->mdd);
    lddmc_protect(&rel->meta);
    lddmc_protect(&rel->topmeta);
    lddmc_protect(&rel->topread);
    lddmc_protect(&rel->r_meta);

    rel->r_k = r_k;
    rel->w_k = w_k;
    rel->r_proj = (int*)RTmalloc(sizeof(int)*r_k);
    rel->w_proj = (int*)RTmalloc(sizeof(int)*w_k);
    memcpy(rel->r_proj, r_proj, sizeof(int)*r_k);
    memcpy(rel->w_proj, w_proj, sizeof(int)*w_k);

    /* Compute the meta for set_next, set_prev */
    uint32_t meta[dom->shared.size*2+2];
    memset(meta, 0, sizeof(uint32_t[dom->shared.size*2+2]));
    int r_i=0, w_i=0, i=0, j=0;
    for (;;) {
        /* determine type (flags) 1=read, 2=write */
        int type = 0;
        if (r_i < r_k && r_proj[r_i] == i) {
            r_i++;
            type += 1; // read
        }
        if (w_i < w_k && w_proj[w_i] == i) {
            w_i++;
            type += 2; // write
        }
        /* now that we have type, set meta */
        if (type == 0) meta[j++] = 0;
        else if (type == 1) { meta[j++] = 3; }
        else if (type == 2) { meta[j++] = 4; }
        else if (type == 3) { meta[j++] = 1; meta[j++] = 2; }
        /* also set topvar (for saturation variables) */
        if (type != 0 && rel->topvar == -1) rel->topvar = i;
        /* detect end */
        if (r_i == r_k && w_i == w_k) {
            meta[j++] = 5; // action label
            meta[j++] = (uint32_t)-1;
            break;
        }
        i++;
    }

    rel->meta = lddmc_cube((uint32_t*)meta, j);

    if (r_k != 0 || w_k != 0) {
        /* Compute topmeta for saturation */
        assert(rel->topvar != -1);
        rel->topmeta = lddmc_cube((uint32_t*)meta+rel->topvar, j-rel->topvar);

        /* Compute topread for saturation */
        if (r_k == 0) {
            uint32_t readmeta[1];
            readmeta[0] = -2;
            rel->topread = rel->r_meta = lddmc_cube(readmeta, 1);
        } else {
            uint32_t readmeta[dom->shared.size+1];
            memset(readmeta, 0, sizeof(uint32_t[dom->shared.size+1]));
            // set readmeta to 1 (= keep) for every variable in proj
            for (int i=0; i<r_k; i++) readmeta[r_proj[i]] = 1;
            // end sequence with -2 (= quantify rest)
            int last = r_proj[r_k-1]+1;
            readmeta[last] = -2;
            rel->topread = lddmc_cube((uint32_t*)readmeta+rel->topvar, last+1-rel->topvar);
            rel->r_meta = lddmc_cube(readmeta, last+1);
        }
    } else {
        rel->topmeta = lddmc_false;
        rel->topread = lddmc_false;
        rel->r_meta = lddmc_false;
    }

    return rel;
}

/**
 * Destroy a relation.
 */
static void
rel_destroy(vrel_t rel)
{
    lddmc_unprotect(&rel->mdd);
    lddmc_unprotect(&rel->meta);
    lddmc_unprotect(&rel->topmeta);
    lddmc_unprotect(&rel->topread);
    lddmc_unprotect(&rel->r_meta);
    RTfree(rel->r_proj);
    RTfree(rel->w_proj);
    RTfree(rel);
}

/**
 * Add a new transition (with action label and copy vector) to <rel>.
 */
static void
rel_add_act(vrel_t rel, const int *src, const int *dst, const int *cpy, const int act)
{
    int i=0, j=0, k=0;

    uint32_t vec[rel->size + 1];
    int cpy_vec[rel->size + 1];

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
        } else if (v == 5) {
            cpy_vec[k] = 0; // no copy nodes on action labels
            vec[k++] = act;
        }
        meta = lddmc_follow(meta, v);
    }

    assert(k == rel->size + 1); // plus 1, because action label

    LACE_ME;
    rel->mdd = lddmc_union_cube_copy(rel->mdd, (uint32_t*)vec, cpy_vec, k);
}

static void
rel_add_cpy(vrel_t rel, const int *src, const int *dst, const int *cpy)
{
    return rel_add_act(rel, src, dst, cpy, 0);
}

static void
rel_add(vrel_t rel, const int *src, const int *dst)
{
    return rel_add_cpy(rel, src, dst, NULL);
}

/**
 * Compute the number of transitions and nodes in the relation <rel>.
 */
static void
rel_count(vrel_t rel, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(rel->mdd);
    if (elements != NULL) *elements = lddmc_satcount(rel->mdd);
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
    lddmc_refs_pushptr(&dummyrel.mdd);
    ctx->cb(&dummyrel, ctx->context, (int*)values);
    lddmc_refs_popptr(1);
    return dummyrel.mdd;
    (void)count;
}

static void
rel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void* context)
{
    LACE_ME;
    struct rel_update_context ctx = (struct rel_update_context){rel, cb, context};
    MDD result = lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(rel_updater), &ctx);

    MDD cur = rel->mdd;
    lddmc_refs_pushptr(&cur);
    lddmc_refs_pushptr(&result);
    for (;;) {
        result = lddmc_union(cur, result);
        MDD test = __sync_val_compare_and_swap(&rel->mdd, cur, result);
        if (test == cur) break;
        else cur = test;
    }
    lddmc_refs_popptr(2);
}

/**
 * Compute the successors of <src> and transitions <rel> into <dst>.
 */
static void
set_next(vset_t dst, vset_t src, vrel_t rel)
{
    LACE_ME;
    assert(dst->meta == src->meta);
    dst->mdd = lddmc_relprod(src->mdd, rel->mdd, rel->meta);
}

/**
 * Compute as rel_next but also take the union with <uni>.
 */
static void
set_next_union(vset_t dst, vset_t src, vrel_t rel, vset_t uni)
{
    LACE_ME;
    assert(dst->meta == src->meta);
    assert(dst->meta == uni->meta);
    dst->mdd = lddmc_relprod_union(src->mdd, rel->mdd, rel->meta, uni->mdd);
}

/**
 * Compute the predecessors of <src> and transitions <rel> into <dst>.
 */
static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t universe)
{
    LACE_ME;
    assert(dst->meta == src->meta);
    assert(dst->meta == universe->meta);
    dst->mdd = lddmc_relprev(src->mdd, rel->mdd, rel->meta, universe->mdd);
}

/**
 * Write a .dot file of the LDD of <src> to <fp>.
 */
static void
set_dot(FILE* fp, vset_t src)
{
    lddmc_fprintdot(fp, src->mdd);
}

/**
 * Write a .dot file of the LDD of <src> to <fp>.
 */
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
    fwrite(&set->k, sizeof(int), 1, f);
    if (set->k != -1) fwrite(set->proj, sizeof(int), set->k, f);
    size_t mdd = lddmc_serialize_add(set->mdd);
    lddmc_serialize_tofile(f);
    fwrite(&mdd, sizeof(size_t), 1, f);
}

static void
rel_save_proj(FILE* f, vrel_t rel)
{
    fwrite(&rel->r_k, sizeof(int), 1, f);
    fwrite(&rel->w_k, sizeof(int), 1, f);
    fwrite(rel->r_proj, sizeof(int), rel->r_k, f);
    fwrite(rel->w_proj, sizeof(int), rel->w_k, f);
}

static void
rel_save(FILE* f, vrel_t rel)
{
    size_t mdd = lddmc_serialize_add(rel->mdd);
    lddmc_serialize_tofile(f);
    fwrite(&mdd, sizeof(size_t), 1, f);
}

static vset_t
set_load(FILE* f, vdom_t dom)
{
    int k;
    if (fread(&k, sizeof(int), 1, f) != 1) Abort("Invalid file format.");

    vset_t set;
    if (k == -1) {
        set = set_create(dom, -1, NULL);
    } else {
        int proj[k];
        if (fread(proj, sizeof(int), k, f) != (size_t)k) Abort("Invalid file format.");
        set = set_create(dom, k, proj);
    }

    lddmc_serialize_fromfile(f);

    size_t mdd;
    if (fread(&mdd, sizeof(size_t), 1, f) < 1) Abort("Invalid file format.");
    set->mdd = lddmc_serialize_get_reversed(mdd);

    return set;
}

static vrel_t
rel_load_proj(FILE* f, vdom_t dom)
{
    int r_k, w_k;
    if (fread(&r_k, sizeof(int), 1, f) != 1) Abort("Invalid file format.");
    if (fread(&w_k, sizeof(int), 1, f) != 1) Abort("Invalid file format.");
    int r_proj[r_k], w_proj[w_k];
    if (fread(r_proj, sizeof(int), r_k, f) != (size_t)r_k) Abort("Invalid file format.");
    if (fread(w_proj, sizeof(int), w_k, f) != (size_t)w_k) Abort("Invalid file format.");
    return rel_create_rw(dom, r_k, r_proj, w_k, w_proj);
}

static void
rel_load(FILE* f, vrel_t rel)
{
    lddmc_serialize_fromfile(f);

    size_t mdd;
    if (fread(&mdd, sizeof(size_t), 1, f) < 1) Abort("Invalid file format.");
    rel->mdd = lddmc_serialize_get_reversed(mdd);
}

static void
dom_save(FILE* f, vdom_t dom)
{
    int vector_size = dom->shared.size;
    fwrite(&vector_size, sizeof(int), 1, f);
}

static int
separates_rw()
{
    return 1;
}

typedef struct lddmc_visit_info_global {
    uint64_t op;
    vset_visit_callbacks_t* cbs;
    size_t user_ctx_size;
} lddmc_visit_info_global_t;

typedef struct lddmc_visit_info {
    MDD proj;
    void* user_context;
    lddmc_visit_info_global_t* global;
} lddmc_visit_info_t;

TASK_2(int, lddmc_visit_pre, MDD, mdd, void*, context)
{
    lddmc_visit_info_t* ctx = (lddmc_visit_info_t*) context;

    if (ctx->global->cbs->vset_visit_pre == NULL) return 1;

    if (mdd == lddmc_false) {
        ctx->global->cbs->vset_visit_pre(1, 0, 0, NULL, ctx->user_context);
        return 0;
    }
    if (mdd == lddmc_true) {
        ctx->global->cbs->vset_visit_pre(1, 1, 0, NULL, ctx->user_context);
        return 0;
    }

    void* result = NULL;
    if (cache_get(mdd | ctx->global->op, ctx->proj, 0, (uint64_t*) &result)) {
        ctx->global->cbs->vset_visit_pre(0, lddmc_getvalue(mdd), 1, result, ctx->user_context);
        return 0;
    }

    ctx->global->cbs->vset_visit_pre(0, lddmc_getvalue(mdd), 0, NULL, ctx->user_context);

    return 1;
}

VOID_TASK_3(lddmc_visit_init_context, void*, context, void*, parent, int, succ)
{
    lddmc_visit_info_t* ctx = (lddmc_visit_info_t*) context;

    lddmc_visit_info_t* p = (lddmc_visit_info_t*) parent;

    ctx->global = p->global;
    if (succ == 1) ctx->proj = lddmc_getdown(p->proj);
    else ctx->proj = p->proj;
    ctx->user_context = (&ctx->global) + 1;

    if (ctx->global->cbs->vset_visit_init_context != NULL) {
        ctx->global->cbs->vset_visit_init_context(ctx->user_context, p->user_context, succ);
    }
}

VOID_TASK_2(lddmc_visit_post, MDD, mdd, void*, context)
{
    lddmc_visit_info_t* ctx = (lddmc_visit_info_t*) context;

    if (ctx->global->cbs->vset_visit_post == NULL) return;

    int cache = 0;
    void* result = NULL;
    ctx->global->cbs->vset_visit_post(lddmc_getvalue(mdd), ctx->user_context, &cache, &result);

    if (cache) {
        if (cache_put(mdd | ctx->global->op, ctx->proj, 0, (uint64_t) result)) {
            if (ctx->global->cbs->vset_visit_cache_success != NULL) {
                ctx->global->cbs->vset_visit_cache_success(ctx->user_context, result);
            }
        }
    }
}

static void
set_visit_prepare(vset_t set, vset_visit_callbacks_t* cbs, size_t user_ctx_size, void* user_ctx,
    int cache_op, lddmc_visit_info_t* context, lddmc_visit_callbacks_t* lddmc_cbs)
{
    context->global->op = ((uint64_t) cache_op) << 40;
    context->global->cbs = cbs;
    context->global->user_ctx_size = user_ctx_size;

    context->proj = set->meta;
    context->user_context = user_ctx;

    lddmc_cbs->lddmc_visit_pre = TASK(lddmc_visit_pre);
    lddmc_cbs->lddmc_visit_init_context = TASK(lddmc_visit_init_context);
    lddmc_cbs->lddmc_visit_post = TASK(lddmc_visit_post);
}

static void
set_visit_par(vset_t set, vset_visit_callbacks_t* cbs, size_t user_ctx_size, void* user_ctx, int cache_op)
{
    lddmc_visit_info_t context;
    lddmc_visit_info_global_t glob;
    context.global = &glob;
    lddmc_visit_callbacks_t lddmc_cbs;
    set_visit_prepare(set, cbs, user_ctx_size, user_ctx, cache_op, &context, &lddmc_cbs);

    LACE_ME;
    lddmc_visit_par(set->mdd, &lddmc_cbs, sizeof(lddmc_visit_info_t) + user_ctx_size, &context);
}

static void
set_visit_seq(vset_t set, vset_visit_callbacks_t* cbs, size_t user_ctx_size, void* user_ctx, int cache_op)
{
    lddmc_visit_info_t context;
    lddmc_visit_info_global_t glob;
    context.global = &glob;
    lddmc_visit_callbacks_t lddmc_cbs;
    set_visit_prepare(set, cbs, user_ctx_size, user_ctx, cache_op, &context, &lddmc_cbs);

    LACE_ME;
    lddmc_visit_seq(set->mdd, &lddmc_cbs, sizeof(lddmc_visit_info_t) + user_ctx_size, &context);
}

static void
dom_clear_cache(vdom_t dom, const int cache_op)
{
    (void) cache_op; (void) dom;
    cache_clear();
}

static int
dom_next_cache_op(vdom_t dom)
{
    (void) dom;
    const uint64_t op = cache_next_opid();
    if (op >> 40 > INT_MAX) Abort("Too many user cache operations");

    return (int) op;
}

/**
 * Implementation of (parallel) saturation
 * (assumes relations are ordered on first variable)
 */
TASK_5(MDD, lddmc_go_sat, MDD, set, vrel_t*, rels, int, depth, int, count, int, id)
{
    /* Terminal cases */
    if (set == lddmc_false) return lddmc_false;
    if (count == 0) return set;
    assert(set != lddmc_true);

    /* Consult the cache */
    MDD result;
    MDD _set = set;
    if (cache_get3(201LL<<40, _set, (uint64_t)rels, id, &result)) return result;
    lddmc_refs_pushptr(&_set);

    /* Check if the relation should be applied */
    int var = rels[0]->topvar;
    assert(depth <= var);
    if (depth == var) {
        /* Count the number of relations starting here */
        int n = 1;
        while (n < count && var == rels[n]->topvar) n++;
        /*
         * Compute until fixpoint:
         * - SAT deeper
         * - learn and chain-apply all current level once
         */
        MDD prev = lddmc_false;
        struct vector_set dummy;
        lddmc_refs_pushptr(&set);
        lddmc_refs_pushptr(&prev);
        while (prev != set) {
            prev = set;
            // SAT deeper
            set = CALL(lddmc_go_sat, set, rels+n, depth, count-n, id);
            // learn and chain-apply all current level once
            for (int i=0;i<n;i++) {
                if (rels[i]->expand != NULL) {
                    // project set
                    dummy.dom = rels[i]->dom;
                    dummy.size = rels[i]->r_k;
                    dummy.meta = rels[i]->r_meta;
                    dummy.k = rels[i]->r_k;
                    dummy.proj = rels[i]->r_proj;
                    dummy.mdd = lddmc_project(set, rels[i]->topread);
                    // call expand callback
                    lddmc_refs_pushptr(&dummy.mdd);
                    rels[i]->expand(rels[i], &dummy, rels[i]->expand_ctx);
                    lddmc_refs_popptr(1);
                }
                // and then step
                set = lddmc_relprod_union(set, rels[i]->mdd, rels[i]->topmeta, set);
            }
        }
        lddmc_refs_popptr(2);
        result = set;
    } else {
        /* Recursive computation */
        lddmc_refs_spawn(SPAWN(lddmc_go_sat, lddmc_getright(set), rels, depth, count, id));
        MDD down = lddmc_refs_push(CALL(lddmc_go_sat, lddmc_getdown(set), rels, depth+1, count, id));
        MDD right = lddmc_refs_sync(SYNC(lddmc_go_sat));
        lddmc_refs_pop(1);
        result = lddmc_makenode(lddmc_getvalue(set), down, right);
    }
    // Store in cache
    cache_put3(201LL<<40, _set, (uint64_t)rels, id, result);
    lddmc_refs_popptr(1);
    return result;
}

static void
set_least_fixpoint(vset_t dst, vset_t src, vrel_t _rels[], int rel_count)
{
    // Create copy of rels
    vrel_t rels[rel_count];
    memcpy(rels, _rels, sizeof(vrel_t[rel_count]));

    // Sort the rels (using gnome sort)
    int i = 1, j = 2;
    vrel_t t;
    while (i < rel_count) {
        vrel_t *p = rels+i, *q = p-1;
        if ((*q)->topvar > (*p)->topvar) {
            t = *q;
            *q = *p;
            *p = t;
            if (--i) continue;
        }
        i = j++;
    }

    // Get next id (for cache)
    static volatile int id = 0;
    int _id = __sync_fetch_and_add(&id, 1);

    // Go!
    LACE_ME;
    dst->mdd = CALL(lddmc_go_sat, src->mdd, rels, 0, rel_count, _id);
}

static void
set_function_pointers(vdom_t dom)
{
    /* Set function pointers */
    dom->shared.dom_clear_cache=dom_clear_cache;
    dom->shared.dom_next_cache_op=dom_next_cache_op;

    dom->shared.set_create=set_create;
    dom->shared.set_destroy=set_destroy;
    dom->shared.set_is_empty=set_is_empty;
    dom->shared.set_equal=set_equal;
    dom->shared.set_clear=set_clear;
    dom->shared.set_copy=set_copy;
    dom->shared.set_count=set_count;
    dom->shared.set_ccount=set_ccount;
    dom->shared.set_project=set_project;
    dom->shared.set_project_minus=set_project_minus;
    dom->shared.set_union=set_union;
    dom->shared.set_intersect=set_intersect;
    dom->shared.set_minus=set_minus;
    dom->shared.set_zip=set_zip;
    dom->shared.set_add=set_add;
    dom->shared.set_update=set_update;
    dom->shared.set_member=set_member;
    dom->shared.set_example=set_example;
    dom->shared.set_enum=set_enum;
    dom->shared.set_enum_match=set_enum_match;
    dom->shared.set_copy_match=set_copy_match;
    dom->shared.set_join=set_join;
    dom->shared.set_visit_par=set_visit_par;
    dom->shared.set_visit_seq=set_visit_seq;

    dom->shared.set_next=set_next;
    dom->shared.set_next_union=set_next_union;
    dom->shared.set_prev=set_prev;
    dom->shared.set_least_fixpoint=set_least_fixpoint;

    dom->shared.rel_create_rw=rel_create_rw;
    dom->shared.rel_destroy=rel_destroy;
    dom->shared.rel_add=rel_add;
    dom->shared.rel_add_cpy=rel_add_cpy;
    dom->shared.rel_add_act=rel_add_act;
    dom->shared.rel_count=rel_count;
    dom->shared.rel_update=rel_update;

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
}

void ltsmin_initialize_sylvan(); // defined in vset_sylvan.c

vdom_t
vdom_create_lddmc(int n)
{
    Warning(info,"Creating a multi-core ListDD domain.");

    ltsmin_initialize_sylvan();
    sylvan_init_ldd();

    /* Create data structure */
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom, n);
    set_function_pointers(dom);
    return dom;
}

vdom_t
vdom_create_lddmc_from_file(FILE *f)
{
    int vector_size;
    if (fread(&vector_size, sizeof(int), 1, f) < 1) Abort("Invalid file format.");
    return vdom_create_lddmc(vector_size);
}
