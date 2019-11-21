#include <hre/config.h>
#include <assert.h>

// gmp doesn't deal well with extern "C"
#if defined(HAVE_GMPXX_H)
#include <gmpxx.h>
#endif

#include <SDD.h>
#include <DDD.h>
#include <Hom.h>
#include <MemoryManager.h>
#include <Hom_Basic.hh>

static const int DEFAULT_VAR = 1;

class _projectVar:public StrongShom {
    int var;

public:
    _projectVar(int vr):var(vr) {};

    GSDD phiOne() const {
        return GSDD::top;
    }

    bool skip_variable (int vr) const {
        return var != vr;
    }

    GShom phi(int, const DataSet &) const {
        return GShom::id;
    }

    size_t hash() const {
        return 17 * var;
    }

    bool operator==(const StrongShom &s) const {
        const _projectVar & ps = (const _projectVar&)s;
        return (var == ps.var);
    }

    _GShom * clone () const { return new _projectVar(*this); }

    void mark() {
        return;
    }
};

GShom projectVar(int vr) {
    return _projectVar(vr);
};

GShom selectVarVal (int vr, int vl) {
    return localApply(varEqState(DEFAULT_VAR, vl), vr);
}

GShom setVarVal (int vr, int vl) {
  return localApply(setVarConst(DEFAULT_VAR, vl), vr);
}

extern "C" {
#include <hre/user.h>
#include <vset-lib/vdom_object.h>

struct vector_domain {
    struct vector_domain_shared shared;
};

struct vector_set {
    vdom_t dom;
    SDD *ddd;
    Shom *projection;
    int p_len;
    int proj[];
};

struct vector_relation {
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;
    SDD *ddd;
    Shom *next;
    Shom *prev;
    int p_len;
    int proj[];
};

vset_t
set_create_ddd(vdom_t dom, int k, int *proj)
{
    int l = (k < 0) ? 0 : k;
    vset_t set = reinterpret_cast<vset_t>(RTmalloc(sizeof(struct vector_set)
                                                   + sizeof(int[l])));

    set->dom = dom;
    set->ddd = new SDD;
    set->p_len = k;

    Shom projection = Shom::id;
    bool skip[dom->shared.size];

    for (int i = 0; i < dom->shared.size; i++)
        skip[i] = false;

    for (int i = 0; i < k; i++) {
        skip[proj[i]] = true;
        set->proj[i] = proj[i];
    }

    for (int i = 0; i < dom->shared.size; i++) {
        if (!skip[i])
            projection = projectVar(i) & projection;
    }

    set->projection = new Shom(projection);
    return set;
}

void
set_add_ddd(vset_t set, const int *e)
{
    SDD element = SDD::one;

    if (set->p_len >= 0) {
        int len = set->p_len;

        for (int i = len - 1; i >= 0; i--)
            element = SDD(set->proj[i], DDD(DEFAULT_VAR, e[i]), element);
    } else {
        int len = set->dom->shared.size;

        for (int i = len - 1; i >= 0; i--)
            element = SDD(i, DDD(DEFAULT_VAR, e[i]), element);
    }

    *set->ddd = *set->ddd + element;
    assert(*set->ddd != SDD::top);
}

int
set_member_ddd(vset_t set, const int *e)
{
    Shom h = Shom::id;

    if (set->p_len >= 0) {
        int len = set->p_len;

        for (int i = len - 1; i >= 0; i--)
            h = selectVarVal(set->proj[i], e[i]) & h;
    } else {
        int len = set->dom->shared.size;

        for (int i = len - 1; i >= 0; i--)
            h = selectVarVal(i, e[i]) & h;
    }

    SDD tmp = h(*set->ddd);
    return !tmp.empty();
}

int
set_equal_ddd(vset_t set1, vset_t set2)
{
    return (*set1->ddd == *set2->ddd);
}

int
set_is_empty_ddd(vset_t set)
{
    return set->ddd->empty();
}

void
set_clear_ddd(vset_t set)
{
    *set->ddd = SDD::null;
}

void
enumerate(const GSDD *ddd, int idx, int *e, vset_element_cb cb, void *context)
{
    assert(*ddd != SDD::top && *ddd != SDD::null);

    if (*ddd == SDD::one) {
        cb(context, e);
        return;
    }

    for (SDD::const_iterator vi = (*ddd).begin(); vi != (*ddd).end(); vi++) {
        DDD * vals = (DDD*)vi->first;
        for (DDD::const_iterator it = vals->begin() ; it != vals->end() ; it++){
            e[idx] = it->first;
            enumerate(&vi->second, idx + 1, e, cb, context);
        }
    }
}

void
set_enum_ddd(vset_t set, vset_element_cb cb, void *context)
{
    int len = (set->p_len < 0) ? set->dom->shared.size : set->p_len;
    int e[len];

    if (*set->ddd == SDD::null)
        return;

    enumerate(set->ddd, 0, e, cb, context);
}

void
set_enum_match_ddd(vset_t set, int p_len, int *proj, int *match,
                       vset_element_cb cb, void *context)
{
    assert(p_len >= 0);
    Shom h = Shom::id;

    if (*set->ddd == SDD::null)
        return;

    for (int i = p_len - 1; i >= 0; i--)
        h = selectVarVal(proj[i], match[i]) & h;

    int len = (set->p_len < 0) ? set->dom->shared.size : set->p_len;
    int e[len];
    SDD tmp = h(*set->ddd);

    if (tmp == SDD::null)
        return;

    enumerate(&tmp, 0, e, cb, context);
}

void
set_copy_match_ddd(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
    assert(p_len >= 0);
    Shom h = Shom::id;

    for (int i = p_len - 1; i >= 0; i--)
        h = selectVarVal(proj[i], match[i]) & h;

    *dst->ddd = h(*src->ddd);
}

void
set_example_ddd(vset_t set, int *e)
{
    int len = (set->p_len < 0) ? set->dom->shared.size : set->p_len;
    const GSDD *ddd = set->ddd;

    assert(*ddd != SDD::top && *ddd != SDD::null);

    for (int i = 0; i < len; i++) {
        SDD::const_iterator vi = (*ddd).begin();
        DDD* vals = (DDD*)vi->first;
        DDD::const_iterator it = vals->begin();
        e[i] = it->first;
        ddd = &vi->second;
    }
}

void
set_copy_ddd(vset_t dst, vset_t src)
{
    assert(dst->p_len == src->p_len);
    *dst->ddd = *src->ddd;
    assert(*dst->ddd != SDD::top);
}

void
set_project_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = (*dst->projection)(*src->ddd);
    assert(*dst->ddd != SDD::top);
}

void
set_union_ddd(vset_t dst, vset_t src)
{
    assert(dst->p_len == src->p_len);
    *dst->ddd = *dst->ddd + *src->ddd;
    assert(*dst->ddd != SDD::top);
}

void
set_intersect_ddd(vset_t dst, vset_t src)
{
    assert(dst->p_len == src->p_len);
    *dst->ddd = *dst->ddd * *src->ddd;
    assert(*dst->ddd != SDD::top);
}


void
set_minus_ddd(vset_t dst, vset_t src)
{
    assert(dst->p_len == src->p_len);
    *dst->ddd = *dst->ddd - *src->ddd;
    assert(*dst->ddd != SDD::top);
}

void
set_count_ddd(vset_t set, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = set->ddd->size();
    if (elements != NULL) *elements = set->ddd->nbStates();
}

vrel_t
rel_create_ddd(vdom_t dom, int k, int *proj)
{
    assert(k >= 0);
    vrel_t rel = reinterpret_cast<vrel_t>(RTmalloc(sizeof(struct vector_relation)
                                                   + sizeof(int[k])));

    rel->dom = dom;
    rel->ddd = new SDD;
    rel->next = new Shom(SDD::null);
    rel->prev = new Shom(SDD::null);
    rel->p_len = k;

    for (int i = 0; i < k; i++)
        rel->proj[i] = proj[i];

    return rel;
}

void
rel_add_ddd(vrel_t rel, const int *src, const int *dst)
{
    SDD element = SDD::one;
    Shom next = Shom::id;
    Shom prev = Shom::id;

    if (rel->p_len >= 0) {
        int len = rel->p_len;

        for (int i = len - 1; i >= 0; i--) {
            element = SDD(rel->proj[i], DDD(DEFAULT_VAR, dst[i]), element);
            element = SDD(rel->proj[i], DDD(DEFAULT_VAR, src[i]), element);
            next = selectVarVal(rel->proj[i], src[i]) & next;
            if (src[i] != dst[i])
                next = setVarVal(rel->proj[i], dst[i]) & next;
            prev = selectVarVal(rel->proj[i], dst[i]) & prev;
            if (src[i] != dst[i])
                prev = setVarVal(rel->proj[i], src[i]) & prev;
        }
    } else {
        int len = rel->dom->shared.size;

        for (int i = len - 1; i >= 0; i--) {
            element = SDD(i, DDD(DEFAULT_VAR, dst[i]), element);
            element = SDD(i, DDD(DEFAULT_VAR, src[i]), element);
            next = selectVarVal(i, src[i]) & next;
            if (src[i] != dst[i])
                next = setVarVal(i, dst[i]) & next;
            prev = selectVarVal(i, dst[i]) & prev;
            if (src[i] != dst[i])
                prev = setVarVal(i, src[i]) & prev;
        }
    }

    SDD tmp = *rel->ddd;
    *rel->ddd = *rel->ddd + element;
    assert(*rel->ddd != SDD::top);

    if (tmp != *rel->ddd) {
        *rel->next = next + *rel->next;
        *rel->prev = prev + *rel->prev;
    }
}

void
rel_count_ddd(vrel_t rel, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = rel->ddd->size();
    if (elements != NULL) *elements = rel->ddd->nbStates();
}

void
set_next_ddd(vset_t dst, vset_t src, vrel_t rel)
{
    *dst->ddd = (*rel->next)(*src->ddd);
    assert(*dst->ddd != SDD::top);
}

void
set_prev_ddd(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    *dst->ddd = (*rel->prev)(*src->ddd);
    assert(*dst->ddd != SDD::top);
    set_intersect_ddd(dst,univ);
}

void
set_reorder_ddd() {}

void
set_destroy_ddd(vset_t set)
{
    delete set->ddd;
    delete set->projection;
    RTfree(set);
    MemoryManager::garbage();
}

void set_least_fixpoint_ddd (vset_t dst, vset_t src, vrel_t rels[],
                                 int rel_count)
{
    Shom relation = Shom::id;

    for (int i = 0; i < rel_count; i++) {
        if (rels[i]->expand != NULL)
            Abort("DDD does not support least fixpoint with expansion");

        relation = relation + *rels[i]->next;
    }

    Shom fix = fixpoint(relation);

    *dst->ddd = fix(*src->ddd);
    MemoryManager::garbage();
}

vdom_t
vdom_create_ddd(int n)
{
    Warning(info, "Creating a DDD domain.");
    vdom_t dom = reinterpret_cast<vdom_t>(RTmalloc(sizeof(struct vector_domain)));
    vdom_init_shared(dom, n);

    dom->shared.set_create = set_create_ddd;
    dom->shared.set_add = set_add_ddd;
    dom->shared.set_member = set_member_ddd;
    dom->shared.set_equal = set_equal_ddd;
    dom->shared.set_is_empty = set_is_empty_ddd;
    dom->shared.set_clear = set_clear_ddd;
    dom->shared.set_enum = set_enum_ddd;
    dom->shared.set_enum_match = set_enum_match_ddd;
    dom->shared.set_copy_match = set_copy_match_ddd;
    dom->shared.set_example = set_example_ddd;
    dom->shared.set_copy = set_copy_ddd;
    dom->shared.set_project = set_project_ddd;
    dom->shared.set_union = set_union_ddd;
    dom->shared.set_intersect = set_intersect_ddd;
    dom->shared.set_minus = set_minus_ddd;
    dom->shared.set_count = set_count_ddd;
    dom->shared.rel_create = rel_create_ddd;
    dom->shared.rel_add = rel_add_ddd;
    dom->shared.rel_count = rel_count_ddd;
    dom->shared.set_next = set_next_ddd;
    dom->shared.set_prev = set_prev_ddd;
    dom->shared.reorder = set_reorder_ddd;
    dom->shared.set_destroy = set_destroy_ddd;
    dom->shared.set_least_fixpoint = set_least_fixpoint_ddd;

    return dom;
}

}
