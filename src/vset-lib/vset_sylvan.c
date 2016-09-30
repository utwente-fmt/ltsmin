#include <hre/config.h>

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <alloca.h>

#include <hre/user.h>
#include <hre-io/user.h>
#include <vset-lib/vdom_object.h>
#include <sylvan.h>

static int statebits = 16;
static int actionbits = 16;
static int tablesize = 24;
static int maxtablesize = 28;
static int cachesize = 23;
static int maxcachesize = 27;
static int granularity = 1;
static char* sizes = NULL;

struct poptOption sylvan_options[] = {
    { "sylvan-bits", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &statebits, 0, "set number of bits per integer in the state vector", "<bits>"},
    { "sylvan-sizes", 0, POPT_ARG_STRING, &sizes, 0, "set nodes table and operation cache sizes (powers of 2)", "<tablesize>,<tablemax>,<cachesize>,<cachemax>"},
    { "sylvan-tablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &tablesize , 0 , "set initial size of BDD table to 1<<tablesize", "<tablesize>"},
    { "sylvan-maxtablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxtablesize , 0 , "set maximum size of BDD table to 1<<maxsize", "<maxtablesize>"},
    { "sylvan-cachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cachesize , 0 , "set initial size of operation cache to 1<<cachesize", "<cachesize>"},
    { "sylvan-maxcachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxcachesize , 0 , "set maximum size of operation cache to 1<<cachesize", "<maxcachesize>"},
    { "sylvan-granularity", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &granularity , 0 , "only use operation cache every <granularity> BDD levels", "<granularity>"},
    POPT_TABLEEND
};

struct vector_domain
{
    struct vector_domain_shared shared;

    BDD state_variables;        // set of all state variables
    BDD prime_variables;        // set of all prime state variables
    BDD action_variables;       // set of action label variables
};

struct vector_set
{
    vdom_t dom;

    BDD bdd;
    size_t vector_size;         // size of state vectors in this set
    BDD state_variables;        // set of state variables in this set

    BDD projection;             // set of state variables not in this set (for set_project)
};

struct vector_relation
{
    vdom_t dom;
    expand_cb expand;           // compatibility with generic vector_relation
    void *expand_ctx;           // idem

    BDD bdd;
    BDD all_variables;          // all state/prime state variables that the relation is defined on
    BDD all_action_variables;   // union of all_variables and dom->action_variables

    /* the following are for rel_add_cpy */
    int r_k, w_k;               // number of read/write in this relation
    int *w_proj;                // easier for rel_add_cpy
    BDD cur_is_next;            // helper BDD for read-only (copy) variables
    BDD state_variables;        // set of READ state variables in this relation
    BDD prime_variables;        // set of WRITE prime state variables in this relation
};

/**
 * This means: create a new BDD set in the domain dom
 * dom = my domain (just copy)
 * k is the number of integers in proj
 * proj is a list of indices of the state vector in the projection
 */
static vset_t
set_create(vdom_t dom, int k, int* proj) 
{
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));

    LACE_ME;

    set->dom = dom;
    set->bdd = sylvan_false; // Initialize with an empty BDD

    sylvan_protect(&set->bdd);

    if (k>=0 && k<dom->shared.size) {
        // We are creating a projection
        set->vector_size = k;

        BDDVAR state_vars[statebits * k];
        for (int i=0; i<k; i++) {
            for (int j=0; j<statebits; j++) {
                state_vars[i*statebits+j] = 2*(proj[i]*statebits+j);
            }
        }

        set->state_variables = sylvan_ref(sylvan_set_fromarray(state_vars, statebits * k));
    } else {
        // Use all variables
        set->vector_size = dom->shared.size;
        set->state_variables = sylvan_ref(dom->state_variables);
    }

    set->projection = sylvan_ref(sylvan_set_removeall(dom->state_variables, set->state_variables));

    return set;
}

/**
 * Destroy a set.
 * The set must be created first with set_create
 */
static void
set_destroy(vset_t set) 
{
    sylvan_unprotect(&set->bdd);
    sylvan_deref(set->projection);
    sylvan_deref(set->state_variables);
    RTfree(set);
}

/**
 * Create a transition relation
 * There are r_k 'read' variables and w_k 'write' variables
 */
static vrel_t
rel_create_rw(vdom_t dom, int r_k, int *r_proj, int w_k, int *w_proj)
{
    LACE_ME;

    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));

    rel->dom = dom;
    rel->bdd = sylvan_false; // Initially, empty.

    rel->r_k = r_k;
    rel->w_k = w_k;
    rel->w_proj = (int*)RTmalloc(sizeof(int)*w_k);
    memcpy(rel->w_proj, w_proj, sizeof(int)*w_k);

    /* Compute a_proj the union of r_proj and w_proj, and a_k the length of a_proj */
    int a_proj[r_k+w_k];
    int r_i = 0, w_i = 0, a_i = 0;
    for (;r_i < r_k || w_i < w_k;) {
        if (r_i < r_k && w_i < w_k) {
            if (r_proj[r_i] < w_proj[w_i]) {
                a_proj[a_i++] = r_proj[r_i++];
            } else if (r_proj[r_i] > w_proj[w_i]) {
                a_proj[a_i++] = w_proj[w_i++];
            } else /* r_proj[r_i] == w_proj[w_i] */ {
                a_proj[a_i++] = w_proj[w_i++];
                r_i++;
            }
        } else if (r_i < r_k) {
            a_proj[a_i++] = r_proj[r_i++];
        } else if (w_i < w_k) {
            a_proj[a_i++] = w_proj[w_i++];
        }
    }
    const int a_k = a_i;

    /* Compute ro_proj: a_proj \ w_proj */
    int ro_proj[r_k];
    int ro_i = 0;
    for (a_i = w_i = 0; a_i < a_k; a_i++) {
        if (w_i < w_k) {
            if (a_proj[a_i] < w_proj[w_i]) {
                ro_proj[ro_i++] = a_proj[a_i];
            } else /* a_proj[a_i] == w_proj[w_i] */ {
                w_i++;
            }
        } else {
            ro_proj[ro_i++] = a_proj[a_i];
        }
    }
    const int ro_k = ro_i;

    /* Create set of (read) state variables for sylvan_cube */
    BDDVAR state_vars[statebits * r_k];
    for (int i=0; i<r_k; i++) {
        for (int j=0; j<statebits; j++) {
            state_vars[i*statebits+j] = 2*(r_proj[i]*statebits+j);
        }
    }
    rel->state_variables = sylvan_ref(sylvan_set_fromarray(state_vars, statebits * r_k));

    /* Create set of (write) prime variables for sylvan_cube */
    BDDVAR prime_vars[statebits * w_k];
    for (int i=0; i<w_k; i++) {
        for (int j=0; j<statebits; j++) {
            prime_vars[i*statebits+j] = 2*(w_proj[i]*statebits+j)+1;
        }
    }
    rel->prime_variables = sylvan_ref(sylvan_set_fromarray(prime_vars, statebits * w_k));

    /* Compute all_variables, which are all variables the transition relation is defined on */
    BDDVAR all_vars[statebits * a_k * 2];
    for (int i=0; i<a_k; i++) {
        for (int j=0; j<statebits; j++) {
            all_vars[2*(i*statebits+j)] = 2*(a_proj[i]*statebits+j);
            all_vars[2*(i*statebits+j)+1] = 2*(a_proj[i]*statebits+j)+1;
        }
    }
    rel->all_variables = sylvan_ref(sylvan_set_fromarray(all_vars, statebits * a_k * 2));
    rel->all_action_variables = sylvan_ref(sylvan_set_addall(rel->all_variables, rel->dom->action_variables));

    /* Compute cur_is_next for variables in ro_proj */
    BDD cur_is_next = sylvan_true;
    bdd_refs_push(cur_is_next);
    for (int i=ro_k-1; i>=0; i--) {
        for (int j=statebits-1; j>=0; j--) {
            BDD low = bdd_refs_push(sylvan_makenode(2*(ro_proj[i]*statebits+j)+1, cur_is_next, sylvan_false));
            BDD high = sylvan_makenode(2*(ro_proj[i]*statebits+j)+1, sylvan_false, cur_is_next);
            bdd_refs_pop(2);
            cur_is_next = bdd_refs_push(sylvan_makenode(2*(ro_proj[i]*statebits+j), low, high));
        }
    }
    bdd_refs_pop(1);
    rel->cur_is_next = sylvan_ref(cur_is_next);

    return rel;
}

/**
 * Destroy a relation.
 */
static void
rel_destroy(vrel_t rel)
{
    sylvan_deref(rel->bdd);
    sylvan_deref(rel->state_variables);
    sylvan_deref(rel->prime_variables);
    sylvan_deref(rel->all_variables);
    sylvan_deref(rel->all_action_variables);
    RTfree(rel);
}

/* Helper function to detect problems with the number of bits per stats  */
static void
check_state(const int *e, unsigned int N)
{
    for (unsigned int i=0; i<N; i++) {
        if (e[i] != 0) {
            register int X = 32 - __builtin_clz(e[i]);
            if (X > statebits) Abort("%d bits are not enough for the state vector (try option --sylvan-bits=%d)!", statebits, X);
        }
    }
}

/**
 * Bit smash state to cube
 * Make sure size(arr) is statebits*state_length
 */
static void
state_to_cube(const int* state, size_t state_length, uint8_t *arr)
{
    for (size_t i=0; i<state_length; i++) {
        for (int j=0; j<statebits; j++) {
            *arr = (*state & (1LL<<(statebits-j-1))) ? 1 : 0;
            arr++;
        }
        state++;
    }
}

/**
 * Adds e to set
 */
static void
set_add(vset_t set, const int* e)
{
    // e is a full state vector

    // check if the state vector fits in <statebits> bits
    check_state(e, set->dom->shared.size);

    // create cube
    uint8_t cube[set->dom->shared.size * statebits];
    state_to_cube(e, set->dom->shared.size, cube);

    // get Lace infrastructure
    LACE_ME;

    // cube it
    BDD bdd = sylvan_cube(set->dom->state_variables, cube);

    // existential quantification
    bdd_refs_push(bdd);
    bdd = sylvan_exists(bdd, set->projection);
    bdd_refs_pop(1);

    // add to set
    bdd_refs_push(bdd);
    set->bdd = sylvan_or(set->bdd, bdd);
    bdd_refs_pop(1);
}

/**
 * Returns 1 if e is a member of set, 0 otherwise
 */
static int
set_member(vset_t set, const int* e)
{
    // e is a full state vector

    // check if the state vector fits in <statebits> bits
    check_state(e, set->dom->shared.size);

    // create cube
    uint8_t cube[set->dom->shared.size * statebits];
    state_to_cube(e, set->dom->shared.size, cube);

    // get Lace infrastructure
    LACE_ME;

    // cube it
    BDD bdd = sylvan_cube(set->dom->state_variables, cube);

    // existential quantification
    bdd_refs_push(bdd);
    bdd = sylvan_exists(bdd, set->projection);
    bdd_refs_pop(1);

    // check if in set
    bdd_refs_push(bdd);
    int res = sylvan_and(set->bdd, bdd) != sylvan_false ? 1 : 0;
    bdd_refs_pop(1);
    return res;
}

/**
 * Returns 1 if the set is empty, 0 otherwise
 */
static int
set_is_empty(vset_t set)
{
    return set->bdd == sylvan_false ? 1 : 0;
}

/**
 * Returns 1 if the two sets are equal, 0 otherwise.
 * Assumption: projections are equal.
 */
static int
set_equal(vset_t set1, vset_t set2)
{
    return set1->bdd == set2->bdd;
}

/** 
 * Clear a set.
 */
static void
set_clear(vset_t set) 
{
    set->bdd = sylvan_false;
}

/**
 * Copy a set from src to dst.
 * Assumption: projections are equal.
 */
static void
set_copy(vset_t dst, vset_t src)
{
    assert(dst->projection == src->projection);
    dst->bdd = src->bdd;
}

/**
 * This is the internal execution of set_enum
 * <levels> contains k BDDLEVEL levels.
 * 0 <= n < k
 */
static void
set_enum_do(BDD root, const BDD variables, int *vec, int n, vset_element_cb cb, void* context)
{
    if (root == sylvan_false) return;
    if (sylvan_set_isempty(variables)) {
        // Make sure that there are no variables left
        assert(root == sylvan_true);
        // We have one satisfying assignment!
        cb(context, vec);
    } else {
        BDDVAR var = sylvan_var(variables);
        BDD variables_next = sylvan_set_next(variables);

        int i = n / statebits;        // which slot in the state vector
        int j = n % statebits;        // which bit?

        uint32_t bitmask = 1 << (statebits-1-j);

        if (root == sylvan_true || var != sylvan_var(root)) {
            // n is skipped, take both
            vec[i] |= bitmask;
            set_enum_do(root, variables_next, vec, n+1, cb, context);
            vec[i] &= ~bitmask;
            set_enum_do(root, variables_next, vec, n+1, cb, context);
        } else {
            vec[i] |= bitmask;
            set_enum_do((sylvan_high(root)), variables_next, vec, n+1, cb, context);
            vec[i] &= ~bitmask;
            set_enum_do((sylvan_low(root)), variables_next, vec, n+1, cb, context);
        }
    }
}

/**
 * Enumerate all elements of the set. Calls cb(context, const int* ELEMENT) 
 * for every found element. Elements are projected, meaning not the full 
 * state vector is returned, but only the selected bytes.
 */
static void
set_enum(vset_t set, vset_element_cb cb, void* context)
{
    int vec[set->vector_size];
    memset(vec, 0, sizeof(int)*set->vector_size);
    set_enum_do(set->bdd, set->state_variables, vec, 0, cb, context);
}

/**
 * Generate one possible state
 */
static void
set_example(vset_t set, int *e)
{
    assert(set->bdd != sylvan_false);

    memset(e, 0, sizeof(int)*set->vector_size);

    uint8_t* cube = (uint8_t*)alloca(set->vector_size*statebits*sizeof(uint8_t));
    sylvan_sat_one(set->bdd, set->state_variables, cube);

    size_t i;
    int j;
    for (i=0;i<set->vector_size;i++) {
        for (j=0;j<statebits;j++) {
            if (cube[i*statebits+j]==1) e[i] |= 1<<(statebits-j-1);
        }
    }
}

/**
 * Enumerate all states that match partial state <match>
 * <match> is p_len long
 * <proj> is an ordered list of integers, containing indices of each match integer
 */
static void
set_enum_match(vset_t set, int p_len, int* proj, int* match, vset_element_cb cb, void* context) 
{
    LACE_ME;

    /* create bdd of 'match' */
    BDD match_bdd = sylvan_true;
    for (int i=p_len-1; i>=0; i--) {
        uint32_t b = match[i];
        for (int j=statebits-1; j>=0; j--) {
            if (b & 1) match_bdd = sylvan_makenode(2*(proj[i]*statebits+j), sylvan_false, match_bdd);
            else match_bdd = sylvan_makenode(2*(proj[i]*statebits+j), match_bdd, sylvan_false);
            b >>= 1;
        }
    }
    bdd_refs_push(match_bdd);
    match_bdd = sylvan_and(match_bdd, set->bdd);
    bdd_refs_pop(1);

    int vec[set->vector_size];
    memset(vec, 0, sizeof(int)*set->vector_size);

    bdd_refs_push(match_bdd);
    set_enum_do(match_bdd, set->state_variables, vec, 0, cb, context);
    bdd_refs_pop(1);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int* proj, int*match)
{
    LACE_ME;

    /* create bdd of 'match' */
    BDD match_bdd = sylvan_true;
    for (int i=p_len-1; i>=0; i--) {
        uint32_t b = match[i];
        for (int j=statebits-1; j>=0; j--) {
            if (b & 1) match_bdd = sylvan_makenode(2*(proj[i]*statebits+j), sylvan_false, match_bdd);
            else match_bdd = sylvan_makenode(2*(proj[i]*statebits+j), match_bdd, sylvan_false);
            b >>= 1;
        }
    }

    bdd_refs_push(match_bdd);
    dst->bdd = sylvan_and(match_bdd, src->bdd);
    bdd_refs_pop(1);
}

static void
set_count(vset_t set, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = sylvan_nodecount(set->bdd);
    if (elements != NULL) *elements = (double) sylvan_satcount(set->bdd, set->state_variables);
}

static void
rel_count(vrel_t rel, long *nodes, double *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = sylvan_nodecount(rel->bdd);
    if (elements != NULL) *elements = (double)sylvan_satcount(rel->bdd, rel->all_action_variables);
}

/**
 * Calculate dst = dst + src
 */
static void
set_union(vset_t dst, vset_t src)
{
    LACE_ME;

    if (dst != src) {
        dst->bdd = sylvan_or(dst->bdd, src->bdd);
    }
}

/**
 * Calculate dst = dst /\ src
 */
static void
set_intersect(vset_t dst, vset_t src)
{
    LACE_ME;

    if (dst != src) {
        dst->bdd = sylvan_and(dst->bdd, src->bdd);
    }
}

/**
 * Calculate dst = dst - src
 */
static void
set_minus(vset_t dst, vset_t src)
{
    LACE_ME;

    if (dst != src) {
        dst->bdd = sylvan_diff(dst->bdd, src->bdd);
    } else {
        dst->bdd = sylvan_false;
    }
}

/**
 * Calculate dst = next(src, rel)
 */
static void
set_next(vset_t dst, vset_t src, vrel_t rel)
{
    LACE_ME;

    // defined on same variables?
    assert(dst->state_variables == src->state_variables);

    dst->bdd = sylvan_relnext(src->bdd, rel->bdd, rel->all_variables);
} 

/**
 * Calculate dst = prev(src, rel) intersected with univ
 */
static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    LACE_ME;

    // defined on same variables?
    assert(dst->state_variables == src->state_variables);

    if (dst == univ) {
        Abort("Do not call set_prev with dst == univ");
    }

    dst->bdd = sylvan_relprev(rel->bdd, src->bdd, rel->all_variables);
    set_intersect(dst, univ);
}

/**
 * Calculate projection of src onto dst
 */
static void
set_project(vset_t dst, vset_t src)
{
    if (dst->projection != src->projection) {
        LACE_ME;
        dst->bdd = sylvan_exists(src->bdd, dst->projection);
    } else {
        dst->bdd = src->bdd;
    }
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

    if (src == dst) {
        Abort("Do not call set_zip with dst == src");
    }

    BDD tmp1 = dst->bdd;
    BDD tmp2 = src->bdd;
    bdd_refs_push(tmp1);
    bdd_refs_push(tmp2);
    dst->bdd = sylvan_or(tmp1, tmp2);
    src->bdd = sylvan_diff(tmp2, tmp1);
    bdd_refs_pop(2);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add_act(vrel_t rel, const int *src, const int *dst, const int *cpy, const int act)
{
    LACE_ME;

    check_state(src, rel->r_k);
    check_state(dst, rel->w_k);

    // make cube of src
    uint8_t src_cube[rel->r_k * statebits];
    state_to_cube(src, (size_t)rel->r_k, src_cube);
    BDD src_bdd = bdd_refs_push(sylvan_cube(rel->state_variables, src_cube));

    // Some custom code to create the BDD representing the dst+cpy structure
    BDD dst_bdd = sylvan_true;
    for (int i=rel->w_k-1; i>=0; i--) {
        int k = rel->w_proj[i];
        if (cpy && cpy[i]) {
            // take copy of read
            bdd_refs_push(dst_bdd);
            for (int j=statebits-1; j>=0; j--) {
                BDD low = bdd_refs_push(sylvan_makenode(2*(k*statebits+j)+1, dst_bdd, sylvan_false));
                BDD high = sylvan_makenode(2*(k*statebits+j)+1, sylvan_false, dst_bdd);
                bdd_refs_pop(2);
                dst_bdd = bdd_refs_push(sylvan_makenode(2*(k*statebits+j), low, high));
            }
            bdd_refs_pop(1);
        } else {
            // actually write
            for (int j=statebits-1; j>=0; j--) {
                if (dst[i] & (1LL<<(statebits-j-1))) dst_bdd = sylvan_makenode(2*(k*statebits+j)+1, sylvan_false, dst_bdd);
                else dst_bdd = sylvan_makenode(2*(k*statebits+j)+1, dst_bdd, sylvan_false);
            }
        }
    }
    bdd_refs_push(dst_bdd);

    // make cube of action
    uint8_t act_cube[actionbits];
    for (int i=0; i<actionbits; i++) {
        act_cube[i] = (act & (1LL<<(actionbits-i-1))) ? 1 : 0;
    }
    BDD act_bdd = bdd_refs_push(sylvan_cube(rel->dom->action_variables, act_cube));

    // concatenate dst and act
    BDD dst_and_act = bdd_refs_push(sylvan_and(dst_bdd, act_bdd));

    // concatenate src and dst and act
    BDD src_and_dst_and_act = bdd_refs_push(sylvan_and(src_bdd, dst_and_act));

    // intersect with cur_is_next
    BDD to_add = bdd_refs_push(sylvan_and(src_and_dst_and_act, rel->cur_is_next));

    // add result to relation
    BDD old = rel->bdd;
    rel->bdd = sylvan_ref(sylvan_or(rel->bdd, to_add));
    sylvan_deref(old);

    bdd_refs_pop(6);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add_cpy(vrel_t rel, const int *src, const int *dst, const int *cpy)
{
    LACE_ME;

    check_state(src, rel->r_k);
    check_state(dst, rel->w_k);

    // make cube of src
    uint8_t src_cube[rel->r_k * statebits];
    state_to_cube(src, (size_t)rel->r_k, src_cube);
    BDD src_bdd = bdd_refs_push(sylvan_cube(rel->state_variables, src_cube));

    // Some custom code to create the BDD representing the dst+cpy structure
    BDD dst_bdd = sylvan_true;
    for (int i=rel->w_k; i>=0; i--) {
        int k = rel->w_proj[i];
        if (cpy && cpy[i]) {
            // take copy of read
            bdd_refs_push(dst_bdd);
            for (int j=statebits-1; j>=0; j--) {
                BDD low = bdd_refs_push(sylvan_makenode(2*(k*statebits+j)+1, dst_bdd, sylvan_false));
                BDD high = sylvan_makenode(2*(k*statebits+j)+1, sylvan_false, dst_bdd);
                bdd_refs_pop(2);
                dst_bdd = bdd_refs_push(sylvan_makenode(2*(k*statebits+j), low, high));
            }
            bdd_refs_pop(1);
        } else {
            // actually write
            for (int j=statebits-1; j>=0; j--) {
                if (dst[i] & (1LL<<(statebits-j-1))) dst_bdd = sylvan_makenode(2*(k*statebits+j)+1, sylvan_false, dst_bdd);
                else dst_bdd = sylvan_makenode(2*(k*statebits+j)+1, dst_bdd, sylvan_false);
            }
        }
    }
    sylvan_test_isbdd(dst_bdd);
    bdd_refs_push(dst_bdd);

    // concatenate src and dst
    BDD src_and_dst = bdd_refs_push(sylvan_and(src_bdd, dst_bdd));

    // intersect with cur_is_next
    BDD to_add = bdd_refs_push(sylvan_and(src_and_dst, rel->cur_is_next));

    // add result to relation
    BDD old = rel->bdd;
    rel->bdd = sylvan_ref(sylvan_or(rel->bdd, to_add));
    sylvan_deref(old);

    bdd_refs_pop(4);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add(vrel_t rel, const int *src, const int *dst)
{
    return rel_add_cpy(rel, src, dst, 0);
}

static void
set_reorder()
{
    // ignore
}

static void
set_dot(FILE* fp, vset_t src)
{
    sylvan_fprintdot(fp, src->bdd);
}

static void
rel_dot(FILE* fp, vrel_t rel)
{
    sylvan_fprintdot(fp, rel->bdd);
}

/* SAVING */
static void
post_save(FILE* f, vdom_t dom)
{
    sylvan_serialize_reset();
    return;
    (void)f;
    (void)dom;
}

static void
set_save(FILE* f, vset_t set)
{
    size_t bdd = sylvan_serialize_add(set->bdd);
    size_t state_vars = sylvan_serialize_add(set->state_variables);
    sylvan_serialize_tofile(f);
    fwrite(&bdd, sizeof(size_t), 1, f);
    fwrite(&set->vector_size, sizeof(size_t), 1, f);
    fwrite(&state_vars, sizeof(size_t), 1, f);
}

static void
rel_save_proj(FILE* f, vrel_t rel)
{
    return; // Do not store anything
    (void)f;
    (void)rel;
}

static void
rel_save(FILE* f, vrel_t rel)
{
    size_t bdd = sylvan_serialize_add(rel->bdd);
    size_t all_vars = sylvan_serialize_add(rel->all_variables);
    sylvan_serialize_tofile(f);
    fwrite(&bdd, sizeof(size_t), 1, f);
    fwrite(&all_vars, sizeof(size_t), 1, f);
}

/* LOADING */
static void
post_load(FILE* f, vdom_t dom)
{
    sylvan_serialize_reset();
    return;
    (void)f;
    (void)dom;
}

static vset_t
set_load(FILE* f, vdom_t dom)
{
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));
    set->dom = dom;

    sylvan_serialize_fromfile(f);

    size_t bdd, state_vars;
    if ((fread(&bdd, sizeof(size_t), 1, f) != 1) ||
        (fread(&set->vector_size, sizeof(size_t), 1, f) != 1) ||
        (fread(&state_vars, sizeof(size_t), 1, f) != 1)) {
        Abort("invalid file format");
    }

    LACE_ME;

    sylvan_protect(&set->bdd);
    set->bdd = sylvan_serialize_get_reversed(bdd);
    set->state_variables = sylvan_ref(sylvan_serialize_get_reversed(state_vars));
    set->projection = sylvan_ref(sylvan_set_removeall(dom->state_variables, set->state_variables));

    return set;
}

static vrel_t 
rel_load_proj(FILE* f, vdom_t dom)
{
    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));
    memset(rel, 0, sizeof(struct vector_relation));
    rel->dom = dom;
    return rel; // Do not actually load anything from file
    (void)f;
}

static void
rel_load(FILE* f, vrel_t rel)
{
    if (rel->bdd) sylvan_deref(rel->bdd);
    if (rel->all_variables) sylvan_deref(rel->all_variables);

    sylvan_serialize_fromfile(f);

    size_t bdd, all_vars;
    if ((fread(&bdd, sizeof(size_t), 1, f) != 1) ||
        (fread(&all_vars, sizeof(size_t), 1, f) != 1)) {
        Abort("invalid file format");
    }

    rel->bdd = sylvan_ref(sylvan_serialize_get_reversed(bdd));
    rel->all_variables = sylvan_ref(sylvan_serialize_get_reversed(all_vars));
}

static void
dom_save(FILE* f, vdom_t dom)
{
    int vector_size = dom->shared.size;
    fwrite(&vector_size, sizeof(int), 1, f);
    fwrite(&statebits, sizeof(int), 1, f);
    fwrite(&actionbits, sizeof(int), 1, f);
}

static int
separates_rw()
{
    return 1;
}

static void
dom_set_function_pointers(vdom_t dom)
{
    // Set function pointers
    dom->shared.set_create=set_create;
    dom->shared.set_destroy=set_destroy;
    dom->shared.set_add=set_add; 
    dom->shared.set_member=set_member; 
    dom->shared.set_is_empty=set_is_empty; 
    dom->shared.set_equal=set_equal;
    dom->shared.set_clear=set_clear;
    dom->shared.set_enum=set_enum;
    dom->shared.set_enum_match=set_enum_match;
    dom->shared.set_copy=set_copy;
    dom->shared.set_copy_match=set_copy_match;
    // copy_match_proj
    // proj_create
    dom->shared.set_example=set_example;
    dom->shared.set_union=set_union;
    dom->shared.set_intersect=set_intersect;
    dom->shared.set_minus=set_minus;
    dom->shared.set_zip=set_zip;
    dom->shared.set_project=set_project;
    dom->shared.set_count=set_count;
    dom->shared.rel_create_rw=rel_create_rw;
    dom->shared.rel_add_act=rel_add_act;
    dom->shared.rel_add_cpy=rel_add_cpy;
    dom->shared.rel_add=rel_add;
    dom->shared.rel_count=rel_count;
    dom->shared.set_next=set_next;
    dom->shared.set_prev=set_prev;
    // set_least_fixpoint

    dom->shared.reorder=set_reorder;
    dom->shared.set_dot=set_dot;
    dom->shared.rel_dot=rel_dot;

    // no pre_load or pre_save
    dom->shared.post_save=post_save;
    dom->shared.post_load=post_load;
    dom->shared.dom_save=dom_save;
    dom->shared.set_save=set_save;
    dom->shared.set_load=set_load;
    dom->shared.rel_save_proj=rel_save_proj;
    dom->shared.rel_save=rel_save;
    dom->shared.rel_load_proj=rel_load_proj;
    dom->shared.rel_load=rel_load;
    dom->shared.rel_destroy=rel_destroy;

    dom->shared.separates_rw=separates_rw;
}

VOID_TASK_0(gc_start)
{
    Warning(info, "vset_sylvan: starting garbage collection");
}

VOID_TASK_0(gc_end)
{
    Warning(info, "vset_sylvan: garbage collection done");
}

/**
 * Small helper function
 */
static char*
to_h(double size, char *buf)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    int i = 0;
    for (;size>1024;size/=1024) i++;
    sprintf(buf, "%.*f %s", i, size, units[i]);
    return buf;
}

/**
 * Function to initialize Sylvan, also used by vset_lddmc
 */
void
ltsmin_initialize_sylvan()
{
    static int initialized=0;
    if (initialized) return;
    initialized=1;

    if (sizes != NULL) {
        // parse it...
        if (sscanf(sizes, "%d,%d,%d,%d", &tablesize, &maxtablesize, &cachesize, &maxcachesize) != 4) {
            Abort("Invalid string for --sylvan-sizes, try e.g. --sylvan-sizes=23,28,22,27");
        }
        if (tablesize < 10 || maxtablesize < 10 || cachesize < 10 || maxcachesize < 10 ||
            tablesize > 40 || maxtablesize > 40 || cachesize > 40 || maxcachesize > 40) {
            Abort("Invalid string for --sylvan-sizes, must be between 10 and 40");
        }
        if (tablesize > maxtablesize) {
            Abort("Invalid string for --sylvan-sizes, tablesize is larger than maxtablesize");
        }
        if (cachesize > maxcachesize) {
            Abort("Invalid string for --sylvan-sizes, cachesize is larger than maxcachesize");
        }
    }

    char buf[32];
    to_h((1ULL<<maxtablesize)*24+(1ULL<<maxcachesize)*36, buf);
    Warning(info, "Sylvan allocates %s virtual memory for nodes table and operation cache.", buf);
    to_h((1ULL<<tablesize)*24+(1ULL<<cachesize)*36, buf);
    Warning(info, "Initial nodes table and operation cache requires %s.", buf);

    // Call initializator of library (if needed)
    sylvan_init_package(1LL<<tablesize, 1LL<<maxtablesize, 1LL<<cachesize, 1LL<<maxcachesize);
    sylvan_set_granularity(granularity);
    sylvan_gc_hook_pregc(TASK(gc_start));
    sylvan_gc_hook_postgc(TASK(gc_end));
}

/**
 * Create a domain with object size n
 */
vdom_t
vdom_create_sylvan(int n)
{
    Warning(info, "Creating a Sylvan domain.");

    ltsmin_initialize_sylvan();
    sylvan_init_mtbdd();

    // Create data structure of domain
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom,n);
    dom_set_function_pointers(dom);

    // Create state_variables and prime_variables
    BDDVAR state_vars[statebits * n];
    BDDVAR prime_vars[statebits * n];
    for (int i=0; i<n; i++) {
        for (int j=0; j<statebits; j++) {
            state_vars[i*statebits+j] = 2*(i*statebits+j);
            prime_vars[i*statebits+j] = 2*(i*statebits+j)+1;
        }
    }

    LACE_ME;

    dom->state_variables = sylvan_ref(sylvan_set_fromarray(state_vars, statebits * n));
    dom->prime_variables = sylvan_ref(sylvan_set_fromarray(prime_vars, statebits * n));

    // Create action_variables
    BDDVAR action_vars[actionbits];
    for (int i=0; i<actionbits; i++) {
        action_vars[i] = 1000000+i;
    }
    dom->action_variables = sylvan_ref(sylvan_set_fromarray(action_vars, actionbits));

    return dom;
}

vdom_t
vdom_create_sylvan_from_file(FILE *f)
{
    int vector_size;
    if ((fread(&vector_size, sizeof(int), 1, f) != 1) ||
        (fread(&statebits, sizeof(int), 1, f) != 1) ||
        (fread(&actionbits, sizeof(int), 1, f) != 1)) {
        Abort("invalid file format");
    }
    return vdom_create_sylvan(vector_size);
}
