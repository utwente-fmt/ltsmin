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

struct poptOption lddmc_options[]= {
    POPT_TABLEEND
};

struct vector_domain {
    struct vector_domain_shared shared;
};

struct vector_set {
    vdom_t dom;

    MDD mdd;
    int size;
    MDD proj; // for set_project
};

struct vector_relation {
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;

    MDD mdd;
    int size;
    MDD meta; // for set_next, set_prev
};

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

    uint32_t meta[dom->shared.size*2+2];
    memset(meta, 0, sizeof(uint32_t[dom->shared.size*2+2]));
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
            meta[j++] = 5; // action label
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
    LACE_ME;
    MDD old = set->mdd;
    set->mdd = lddmc_ref(lddmc_union_cube(set->mdd, (uint32_t*)e, set->size));
    lddmc_deref(old);
}

static void
rel_add_act(vrel_t rel, const int *src, const int *dst, const int *cpy, const int act)
{
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
        } else if (v == 5) {
            cpy_vec[k] = 0; // no copy nodes on action labels
            vec[k++] = act;
        }
        meta = lddmc_follow(meta, v);
    }

    assert(k == rel->size + 1); // plus 1, because action label

    LACE_ME;
    MDD old = rel->mdd;
    rel->mdd = lddmc_ref(lddmc_union_cube_copy(rel->mdd, (uint32_t*)vec, cpy_vec, k));
    lddmc_deref(old);
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
    lddmc_deref(set->mdd);
    set->mdd = lddmc_false;
}

static void
set_copy(vset_t dst, vset_t src)
{
    assert(dst->size == src->size);
    lddmc_deref(dst->mdd);
    dst->mdd = lddmc_ref(src->mdd);
}

static int
set_member(vset_t set, const int* e)
{
    return lddmc_member_cube(set->mdd, (uint32_t*)e, set->size);
}

static void
set_count(vset_t set, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount_cached(set->mdd);
}

static void
set_ccount(vset_t set, long *nodes, long double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(set->mdd);
    if (elements != NULL) *elements = lddmc_satcount(set->mdd);
}

static void
rel_count(vrel_t rel, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = lddmc_nodecount(rel->mdd);
    if (elements != NULL) *elements = lddmc_satcount(rel->mdd);
}

static void
set_union(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_union(dst->mdd, src->mdd));
    lddmc_deref(old);
}

/**
 * Add all elements of src to dst and remove all elements that were in dst already from src
 * in other words: newDst = dst + src
 *                 newSrc = src - dst
 */
static void
set_zip(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(dst->size == src->size);
    MDD old1 = dst->mdd;
    MDD old2 = src->mdd;
    dst->mdd = lddmc_ref(lddmc_zip(dst->mdd, src->mdd, &src->mdd));
    lddmc_ref(src->mdd);
    lddmc_deref(old1);
    lddmc_deref(old2);
}

static void
set_minus(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_minus(dst->mdd, src->mdd));
    lddmc_deref(old);
}

static void
set_intersect(vset_t dst, vset_t src)
{
    LACE_ME;
    assert(dst->size == src->size);
    MDD old = dst->mdd;
    dst->mdd = lddmc_ref(lddmc_intersect(dst->mdd, src->mdd));
    lddmc_deref(old);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
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
    ctx->cb(&dummyset, ctx->context, (int*)values);
    lddmc_deref(dummyset.mdd); // return without ref
    return dummyset.mdd;
    (void)count;
}

static void
set_update(vset_t dst, vset_t set, vset_update_cb cb, void* context)
{
    LACE_ME;
    struct set_update_context ctx = (struct set_update_context){dst, cb, context};
    MDD old = dst->mdd;
    MDD result = lddmc_ref(lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(set_updater), &ctx));
    dst->mdd = lddmc_ref(lddmc_union(dst->mdd, result));
    lddmc_deref(old);
    lddmc_deref(result);
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
    ctx->cb(&dummyrel, ctx->context, (int*)values);
    lddmc_deref(dummyrel.mdd); // return without ref
    return dummyrel.mdd;
    (void)count;
}

static void
rel_update(vrel_t rel, vset_t set, vrel_update_cb cb, void* context)
{
    LACE_ME;
    struct rel_update_context ctx = (struct rel_update_context){rel, cb, context};
    MDD old = rel->mdd;
    MDD result = lddmc_ref(lddmc_collect(set->mdd, (lddmc_collect_cb)TASK(rel_updater), &ctx));
    rel->mdd = lddmc_ref(lddmc_union(rel->mdd, result));
    lddmc_deref(old);
    lddmc_deref(result);
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
        LACE_ME;
        assert(src->size == dst->dom->shared.size);
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_project(src->mdd, dst->proj));
    }
}

static void
set_project_minus(vset_t dst, vset_t src, vset_t minus)
{
    if (dst->proj == src->proj) {
        set_minus(dst, minus);
    } else {
        LACE_ME;
        assert(src->size == dst->dom->shared.size);
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_project_minus(src->mdd, dst->proj, minus->mdd));
    }
}

static void
set_next(vset_t dst, vset_t src, vrel_t rel)
{
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
}

static void
set_next_union(vset_t dst, vset_t src, vrel_t rel, vset_t uni)
{
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
}

static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t universe)
{
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
    LACE_ME;
    if (dst == left || dst == right) {
        MDD old = dst->mdd;
        dst->mdd = lddmc_ref(lddmc_join(left->mdd, right->mdd, left->proj, right->proj));
        lddmc_deref(old);
    } else {
        lddmc_deref(dst->mdd);
        dst->mdd = lddmc_ref(lddmc_join(left->mdd, right->mdd, left->proj, right->proj));
    }
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
    if (fread(&mdd, sizeof(size_t), 1, f) < 1) Abort("invalid read");
    if (fread(&proj, sizeof(size_t), 1, f) < 1) Abort("invalid read");
    if (fread(&set->size, sizeof(int), 1, f) < 1) Abort("invalid read");
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
    if (fread(&mdd, sizeof(size_t), 1, f) < 1) Abort("invalid read");
    if (fread(&meta, sizeof(size_t), 1, f) < 1) Abort("invalid read");
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

    context->proj = set->proj;
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
    dom->shared.rel_add_act=rel_add_act;
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
    dom->shared.set_visit_par=set_visit_par;
    dom->shared.set_visit_seq=set_visit_seq;
    dom->shared.dom_clear_cache=dom_clear_cache;
    dom->shared.dom_next_cache_op=dom_next_cache_op;
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
    size_t vector_size;
    if (fread(&vector_size, sizeof(size_t), 1, f) < 1) Abort("invalid read");
    return vdom_create_lddmc((int)vector_size);
}
