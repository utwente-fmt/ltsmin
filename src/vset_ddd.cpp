#include <config.h>
#include <assert.h>

// gmp doesn't deal well with extern "C"
#if defined(HAVE_GMPXX_H)
#include <gmpxx.h>
#endif

#include <DDD.h>
#include <Hom.h>
#include <MemoryManager.h>

class _projectVar:public StrongHom {
    int var;

public:
    _projectVar(int vr):var(vr) {};

    GDDD phiOne() const {
        return GDDD::top;
    }

    bool skip_variable (int vr) const {
        return var != vr;
    }

    GHom phi(int, int) const {
        return GHom::id;
    }

    size_t hash() const {
        return 17 * var;
    }

    bool operator==(const StrongHom &s) const {
        const _projectVar & ps = (const _projectVar&)s;
        return (var == ps.var);
    }

    _GHom * clone () const { return new _projectVar(*this); }

    void mark() {
        return;
    }
};

GHom projectVar(int vr) { return _projectVar(vr); };

class _selectVarVal:public StrongHom {
    int var, val;

public:
    _selectVarVal(int vr, int vl):var(vr),val(vl) {};

    GDDD phiOne() const {
        return GDDD::top;
    }

    bool skip_variable (int vr) const {
        return var != vr;
    }

    GHom phi(int vr, int vl) const {
        if (vl == val)
            return GHom(vr, vl, GHom::id);
        else
            return GHom(GDDD::null);
    }

    size_t hash() const {
        return 23 * var + 29 * val;
    }

    bool operator==(const StrongHom &s) const {
        const _selectVarVal & ps = (const _selectVarVal&)s;
        return (var == ps.var) && (val == ps.val);
    }

    _GHom * clone () const { return new _selectVarVal(*this); }

    void mark() {
        return;
    }
};

GHom selectVarVal(int vr, int vl) { return _selectVarVal(vr, vl); };

class _setVarVal:public StrongHom {
    int var, val;

public:
    _setVarVal(int vr, int vl):var(vr),val(vl) {};

    GDDD phiOne() const {
        return GDDD::top;
    }

    bool skip_variable (int vr) const {
        return var != vr;
    }

    GHom phi(int, int) const {
        return GHom(var, val, GHom::id);
    }

    size_t hash() const {
        return 31 * var + 37 * val;
    }

    bool operator==(const StrongHom &s) const {
        const _setVarVal & ps = (const _setVarVal&)s;
        return (var == ps.var) && (val == ps.val);
    }

    _GHom * clone () const { return new _setVarVal(*this); }

    void mark() {
        return;
    }
};

GHom setVarVal(int vr, int vl) { return _setVarVal(vr, vl); };

extern "C" {
#include <vdom_object.h>
#include <runtime.h>

struct vector_domain {
    struct vector_domain_shared shared;
};

struct vector_set {
    vdom_t dom;
    DDD *ddd;
    Hom *projection;
    int p_len;
    int proj[];
};

struct vector_relation {
    vdom_t dom;
    DDD *ddd;
    Hom *next;
    Hom *prev;
    int p_len;
    int proj[];
};

vset_t
set_create_ddd(vdom_t dom, int k, int *proj)
{
    vset_t set = reinterpret_cast<vset_t>(RTmalloc(sizeof(struct vector_set)
                                                   + k * sizeof(int)));

    set->dom = dom;
    set->ddd = new DDD;
    set->p_len = k;

    Hom projection = Hom::id;
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

    set->projection = new Hom(projection);
    return set;
}

void
set_add_ddd(vset_t set, const int *e)
{
    DDD element = DDD(DDD::one);

    if (set->p_len) {
        int len = set->p_len;

        for (int i = len - 1; i >= 0; i--)
            element = DDD(set->proj[i], e[i], element);
    } else {
        int len = set->dom->shared.size;

        for (int i = len - 1; i >= 0; i--)
            element = DDD(i, e[i], element);
    }

    *set->ddd = *set->ddd + element;
    assert(*set->ddd != DDD::top);
}

int
set_member_ddd(vset_t set, const int *e)
{
    Hom h = Hom::id;

    if (set->p_len) {
        int len = set->p_len;

        for (int i = len - 1; i >= 0; i--)
            h = selectVarVal(set->proj[i], e[i]) & h;
    } else {
        int len = set->dom->shared.size;

        for (int i = len - 1; i >= 0; i--)
            h = selectVarVal(i, e[i]) & h;
    }

    DDD tmp = h(*set->ddd);
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
    *set->ddd = DDD::null;
}

void
enumerate(const GDDD *ddd, int idx, int *e, vset_element_cb cb, void *context)
{
    assert(*ddd != GDDD::top && *ddd != GDDD::null);

    if (*ddd == GDDD::one) {
        cb(context, e);
        return;
    }

    for (GDDD::const_iterator vi = (*ddd).begin(); vi != (*ddd).end(); vi++) {
        e[idx] = vi->first;
        enumerate(&vi->second, idx + 1, e, cb, context);
    }
}

void
set_enum_ddd(vset_t set, vset_element_cb cb, void *context)
{
    int len = (set->p_len) ? set->p_len : set->dom->shared.size;
    int e[len];

    if (*set->ddd == DDD::null)
        return;

    enumerate(set->ddd, 0, e, cb, context);
}

void
set_enum_match_ddd(vset_t set, int p_len, int *proj, int *match,
                       vset_element_cb cb, void *context)
{
    Hom h = Hom::id;

    if (*set->ddd == DDD::null)
        return;

    for (int i = p_len - 1; i >= 0; i--)
        h = selectVarVal(proj[i], match[i]) & h;

    int len = (set->p_len) ? set->p_len : set->dom->shared.size;
    int e[len];
    DDD tmp = h(*set->ddd);

    enumerate(&tmp, 0, e, cb, context);
}

void
set_copy_match_ddd(vset_t dst, vset_t src, int p_len, int *proj, int *match)
{
    Hom h = Hom::id;

    for (int i = p_len - 1; i >= 0; i--)
        h = selectVarVal(proj[i], match[i]) & h;

    *dst->ddd = h(*src->ddd);
}

void
set_example_ddd(vset_t set, int *e)
{
    int len = (set->p_len) ? set->p_len : set->dom->shared.size;
    const GDDD *ddd = set->ddd;

    assert(*ddd != GDDD::top && *ddd != GDDD::null);

    for (int i = 0; i < len; i++) {
        GDDD::const_iterator vi = (*ddd).begin();
        e[i] = vi->first;
        ddd = &vi->second;
    }
}

void
set_copy_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = *src->ddd;
    assert(*dst->ddd != DDD::top);
}

void
set_project_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = (*dst->projection)(*src->ddd);
    assert(*dst->ddd != DDD::top);
}

void
set_union_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = *dst->ddd + *src->ddd;
    assert(*dst->ddd != DDD::top);
}

void
set_intersect_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = *dst->ddd * *src->ddd;
    assert(*dst->ddd != DDD::top);
}


void
set_minus_ddd(vset_t dst, vset_t src)
{
    *dst->ddd = *dst->ddd - *src->ddd;
    assert(*dst->ddd != DDD::top);
}

void
set_count_ddd(vset_t set, long *nodes, bn_int_t *elements)
{
    *nodes = set->ddd->size();
    long double count = set->ddd->nbStates();
    bn_double2int(count, elements);
}

vrel_t
rel_create_ddd(vdom_t dom, int k, int *proj)
{
    vrel_t rel = reinterpret_cast<vrel_t>(RTmalloc(sizeof(struct vector_relation)
                                                   + k * sizeof(int)));

    rel->dom = dom;
    rel->ddd = new DDD;
    rel->next = new Hom(DDD::null);
    //rel->next = new Hom(Hom::id);
    rel->prev = new Hom(DDD::null);
    //rel->prev = new Hom(Hom::id);
    rel->p_len = k;

    for (int i = 0; i < k; i++)
        rel->proj[i] = proj[i];

    return rel;
}

void
rel_add_ddd(vrel_t rel, const int *src, const int *dst)
{
    DDD element = DDD(DDD::one);
    Hom next = Hom::id;
    Hom prev = Hom::id;

    if (rel->p_len) {
        int len = rel->p_len;

        for (int i = len - 1; i >= 0; i--) {
            element = DDD(rel->proj[i], dst[i], element);
            element = DDD(rel->proj[i], src[i], element);
            next = selectVarVal(rel->proj[i], src[i]) & next;
            next = setVarVal(rel->proj[i], dst[i]) & next;
            prev = selectVarVal(rel->proj[i], dst[i]) & prev;
            prev = setVarVal(rel->proj[i], src[i]) & prev;
        }
    } else {
        int len = rel->dom->shared.size;

        for (int i = len - 1; i >= 0; i--) {
            element = DDD(i, dst[i], element);
            element = DDD(i, src[i], element);
            next = selectVarVal(i, src[i]) & next;
            next = setVarVal(i, dst[i]) & next;
            prev = selectVarVal(i, dst[i]) & prev;
            prev = setVarVal(i, src[i]) & prev;
        }
    }

    DDD tmp = *rel->ddd;
    *rel->ddd = *rel->ddd + element;
    assert(*rel->ddd != DDD::top);

    if (tmp != *rel->ddd) {
        *rel->next = next + *rel->next;
        *rel->prev = prev + *rel->prev;
    }
}

void
rel_count_ddd(vrel_t rel, long *nodes, bn_int_t *elements)
{
    *nodes = rel->ddd->size();
    long double count = rel->ddd->nbStates();
    bn_double2int(count, elements);
}

void
set_next_ddd(vset_t dst, vset_t src, vrel_t rel)
{
    // Hom fp = fixpoint(*rel->next);
    // *dst->ddd = fp(*src->ddd);
    *dst->ddd = (*rel->next)(*src->ddd);
    assert(*dst->ddd != DDD::top);
}

void
set_prev_ddd(vset_t dst, vset_t src, vrel_t rel)
{
    *dst->ddd = (*rel->prev)(*src->ddd);
    assert(*dst->ddd != DDD::top);
}

void
set_reorder_ddd() {}

void
set_destroy_ddd(vset_t set)
{
    delete set->ddd;
    RTfree(set);
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

    return dom;
}

}
