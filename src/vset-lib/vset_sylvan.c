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

static int fddbits = 16;
static int threads = 1;
static int datasize = 23;
static int cachesize = 23;
static int dqsize = 100000;
static int granularity = 1;

static void
ltsmin_sylvan_init() 
{
    static int initialized=0;
    if (!initialized) {
        lace_init(threads, dqsize, 0);
        sylvan_init(datasize, cachesize, granularity);
        initialized=1;
    }
}

struct poptOption sylvan_options[] = {
    { "sylvan-threads",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &threads , 0 , "set number of threads for parallelization","<threads>"},
    { "sylvan-dqsize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &dqsize , 0 , "set length of task queue","<dqsize>"},
    { "sylvan-bits",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &fddbits, 0, "set number of bits per integer in the state vector","<bits>"},
    { "sylvan-tablesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &datasize , 0 , "set size of BDD table to 1<<datasize","<datasize>"},
    { "sylvan-cachesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cachesize , 0 , "set size of memoization cache to 1<<cachesize","<cachesize>"},
    { "sylvan-granularity",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &granularity , 0 , "only use memoization cache for every 1/granularity BDD levels","<granularity>"},
    POPT_TABLEEND
};

struct vector_domain
{
    struct vector_domain_shared shared;
    BDDVAR *vec_to_bddvar;      // Translation of bit to BDDVAR for X
    BDDVAR *prime_vec_to_bddvar;// Translation of bit to BDDVAR for X'

    size_t bits_per_integer;

    // Generated based on vec_to_bddvar and prime_vec_to_bddvar
    BDD universe;               // Every BDDVAR used for X
    BDD prime_universe;         // Every BDDVAR used for X'
};

struct vector_set
{
    vdom_t dom;

    BDD bdd;                    // Represented BDD
    size_t vector_size;         // How long is the vector in integers
    BDDVAR *vec_to_bddvar;      // Translation of bit to BDDVAR

    // Generated based on vec_to_bddvar and vector_size
    BDD projection;             // Universe \ X (for projection)
    BDD variables;              // X (for satcount etc)
};

struct vector_relation
{
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;

    BDD bdd;                    // Represented BDD
    size_t vector_size;         // How long is the vector in integers
    BDDVAR *vec_to_bddvar;      // Translation of bit to BDDVAR for X
    BDDVAR *prime_vec_to_bddvar;// Translation of bit to BDDVAR for X'

    // Generated based on vec_to_bddvar and vector_size
    BDD variables;              // X
    BDD prime_variables;        // X'
    BDD all_variables;          // X U X'
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

    set->dom = dom;
    set->bdd = sylvan_false; // Initialize with an empty BDD

    if (k>=0 && k<dom->shared.size) {
        // We are creating a projection
        set->vector_size = k;
        set->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * set->vector_size);
        for (int i=0; i<k; i++) {
            for (int j=0; j<fddbits; j++) {
                set->vec_to_bddvar[i*fddbits + j] = dom->vec_to_bddvar[proj[i]*fddbits + j];
            }
        }
    } else {
        // Use all variables
        set->vector_size = dom->shared.size;
        set->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * set->vector_size);
        memcpy(set->vec_to_bddvar, dom->vec_to_bddvar, sizeof(BDDVAR) * fddbits * set->vector_size);
    }

    sylvan_gc_disable();
    set->variables = sylvan_ref(sylvan_set_fromarray(set->vec_to_bddvar, fddbits * set->vector_size));
    set->projection = sylvan_ref(sylvan_set_removeall(dom->universe, set->variables));
    sylvan_gc_enable();

    return set;
}

/**
 * Destroy a set.
 * The set must be created first with set_create
 */
static void
set_destroy(vset_t set) 
{
    sylvan_deref(set->bdd);
    sylvan_deref(set->projection);
    sylvan_deref(set->variables);
    RTfree(set->vec_to_bddvar);
    RTfree(set);
}

/**
 * Create a "relation" (Dom x Dom)
 * 0 < k <= dom->shared.size
 * The integers in proj, a k-length array, are indices of the 
 * state vector.
 */
static vrel_t
rel_create(vdom_t dom, int k, int* proj)
{
    sylvan_gc_disable();

    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));

    rel->dom = dom;
    rel->bdd = sylvan_false; // Initially, empty.

    // Relations are always projections.
    assert (k >= 0 && k<=dom->shared.size); 

    rel->vector_size = k;
    rel->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * rel->vector_size);
    rel->prime_vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * rel->vector_size);

    for (int i=0; i<k; i++) {
        for (int j=0; j<fddbits; j++) {
            rel->vec_to_bddvar[i*fddbits + j] = dom->vec_to_bddvar[proj[i]*fddbits + j];
            rel->prime_vec_to_bddvar[i*fddbits + j] = dom->prime_vec_to_bddvar[proj[i]*fddbits + j];
        }
    }

    sylvan_gc_disable();
    rel->variables = sylvan_ref(sylvan_set_fromarray(rel->vec_to_bddvar, fddbits * rel->vector_size));
    rel->prime_variables = sylvan_ref(sylvan_set_fromarray(rel->prime_vec_to_bddvar, fddbits * rel->vector_size));
    rel->all_variables = sylvan_ref(sylvan_set_addall(rel->prime_variables, rel->variables));
    sylvan_gc_enable();

    return rel;
}

/**
 * Destroy a relation.
 */
static void
rel_destroy(vrel_t rel)
{
    sylvan_deref(rel->bdd);
    sylvan_deref(rel->variables);
    sylvan_deref(rel->prime_variables);
    sylvan_deref(rel->all_variables);
    RTfree(rel->vec_to_bddvar);
    RTfree(rel->prime_vec_to_bddvar);
    RTfree(rel);
}


static int optimal_bits_per_state = 0;

/* Helper function to detect problems with the number of bits per stats 
TODO: handle negative numbers?? */
static void
check_state(const int *e, unsigned int N)
{
    for (unsigned int i=0; i<N; i++) {
        if (e[i] != 0) {
            register int X = 32 - __builtin_clz(e[i]);
            if (X > optimal_bits_per_state) optimal_bits_per_state = X;
            if (X > fddbits) Abort("%d bits are not enough for the state vector (try %d)!", fddbits, X);
        }
    }
}

/* Call this function to retrieve the automatically 
   determined optimal number of bits per state */
void
sylvan_print_optimal_bits_per_state()
{
    if (optimal_bits_per_state != fddbits) 
        Warning(info, "Optimal --sylvan-bits value is %d", optimal_bits_per_state);
}

/**
 * Returns the BDD corresponding to state e according to the projection of set.
 * Order: [0]<[1]<[n] and hi < lo
 */
static BDD
state_to_bdd(const int* e, size_t vec_length, BDDVAR* vec_to_bddvar, BDD projection)
{
    BDD bdd = sylvan_true;
    check_state(e, vec_length);

    sylvan_gc_disable();

    // Construct BDD from state (go from last variable to first variable)
    for(int i=vec_length;i-->0;) {
        unsigned int b = e[i]; // convert from signed to unsigned...
        for (int j=fddbits;j-->0;) {
            BDD val = sylvan_ithvar(vec_to_bddvar[i*fddbits+j]);
            if (!(b&1)) val = sylvan_not(val);
            bdd = sylvan_and(val, bdd);
            b >>= 1;
        } 
    }

    bdd = sylvan_ref(sylvan_exists(bdd, projection));
    sylvan_gc_enable();

    return bdd;
}

/**
 * Adds e to set
 */
static void
set_add(vset_t set, const int* e)
{
    // For some reason, we never get projected e, we get full e.
    BDD bdd = state_to_bdd(e, set->dom->shared.size, set->dom->vec_to_bddvar, set->projection);
    BDD prev = set->bdd;
    set->bdd = sylvan_ref(sylvan_or(prev, bdd));
    sylvan_deref(bdd);
    sylvan_deref(prev);
}

/**
 * Returns 1 if e is a member of set, 0 otherwise
 */
static int
set_member(vset_t set, const int* e)
{
    // For some reason, we never get projected e, we get full e.
    BDD bdd = state_to_bdd(e, set->dom->shared.size, set->dom->vec_to_bddvar, set->projection);
    int res = sylvan_and(set->bdd, bdd) != sylvan_false ? 1 : 0;
    sylvan_deref(bdd);
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
    sylvan_deref(set->bdd);
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
    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(src->bdd);
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

        int i = n / fddbits;        // which slot in the state vector
        int j = n % fddbits;        // which bit? 

        uint32_t bitmask = 1 << (fddbits-1-j);

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
    set_enum_do(set->bdd, set->variables, vec, 0, cb, context);
}

static int
set_example_do(BDD root, BDD variables, int *vec, int n)
{
    if (root == sylvan_false) return 0;
    if (sylvan_set_isempty(variables)) {
        // Make sure that there are no variables left
        assert(root == sylvan_true);
        // We have at least one satisfying assignment!
        return 1;
    } else {
        BDDVAR var = sylvan_var(variables);
        BDD variables_next = sylvan_set_next(variables);
        if (root == sylvan_true || var != sylvan_var(root)) {
            // n is skipped, just take assignment 0
            return set_example_do(root, variables_next, vec, n+1);
        } else {
            int i = n / fddbits;
            int j = n % fddbits;
            uint32_t bitmask = 1 << (fddbits-1-j);
            vec[i] |= bitmask;
            if (set_example_do(sylvan_high(root), variables_next, vec, n+1)) return 1;
            vec[i] &= ~bitmask;
            int result = set_example_do(sylvan_low(root), variables_next, vec, n+1);
            return result;
        }
    }
}

/**
 * Generate one possible state
 */
static void
set_example(vset_t set, int *e)
{
    assert(set->bdd != sylvan_false);
    set_example_do(set->bdd, set->variables, e, 0);
}

/**
 * Enumerate all states that match partial state <match>
 * <match> is p_len long
 * <proj> is a list of integers, containing indices of each match integer
 */
static void
set_enum_match(vset_t set, int p_len, int* proj, int* match, vset_element_cb cb, void* context) 
{
    // Create bdd of "match"
    // Assumption: proj is ordered (if not, you get bad performance)

    sylvan_gc_disable();
    BDD match_bdd = sylvan_true;
    for (int i=p_len;i-->0;) {
        uint32_t b = match[i];
        for (int j=fddbits;j-->0;) {
            BDD val = sylvan_ithvar(set->dom->vec_to_bddvar[proj[i]*fddbits+j]);
            if (!(b&1)) val = sylvan_not(val);
            match_bdd = sylvan_and(val, match_bdd);
            b >>= 1;
        }
    }
    sylvan_ref(match_bdd);
    sylvan_gc_enable();

    BDD old = match_bdd;
    match_bdd = sylvan_ref(sylvan_and(match_bdd, set->bdd));
    sylvan_deref(old);

    int vec[set->vector_size];
    memset(vec, 0, sizeof(int)*set->vector_size);
    set_enum_do(match_bdd, set->variables, vec, 0, cb, context);
    sylvan_deref(match_bdd);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int* proj, int*match)
{
    // Create bdd of "match"
    // Assumption: proj is ordered (if not, you get bad performance)

    sylvan_deref(dst->bdd);

    sylvan_gc_disable();
    BDD match_bdd = sylvan_true;
    for (int i=p_len;i-->0;) {
        uint32_t b = match[i];
        for (int j=fddbits;j-->0;) {
            BDD val = sylvan_ithvar(src->dom->vec_to_bddvar[proj[i]*fddbits+j]);
            if (!(b&1)) val = sylvan_not(val);
            match_bdd = sylvan_and(val, match_bdd);
            b >>= 1;
        }
    }
    sylvan_ref(match_bdd);
    sylvan_gc_enable();

    dst->bdd = sylvan_ref(sylvan_and(match_bdd, src->bdd));
    sylvan_deref(match_bdd);
}

static void
set_count(vset_t set, long *nodes, bn_int_t *elements) 
{
    *nodes = sylvan_nodecount(set->bdd);
    double count = (double)sylvan_satcount(set->bdd, set->variables);
    bn_double2int(count, elements);
}

static void
rel_count(vrel_t rel, long *nodes, bn_int_t *elements)
{
    *nodes = sylvan_nodecount(rel->bdd);
    double count = (double)sylvan_satcount(rel->bdd, rel->all_variables);
    bn_double2int(count, elements);
}

/**
 * Calculate dst = dst + src
 */
static void
set_union(vset_t dst, vset_t src)
{
    BDD old = dst->bdd;
    dst->bdd = sylvan_ref(sylvan_or(dst->bdd, src->bdd));
    sylvan_deref(old);
}

/**
 * Calculate dst = dst /\ src
 */
static void
set_intersect(vset_t dst, vset_t src)
{
    BDD old = dst->bdd;
    dst->bdd = sylvan_ref(sylvan_and(dst->bdd, src->bdd));
    sylvan_deref(old);
}

/**
 * Calculate dst = dst - src
 */
static void
set_minus(vset_t dst, vset_t src)
{
    BDD old = dst->bdd;
    dst->bdd = sylvan_ref(sylvan_diff(dst->bdd, src->bdd));
    sylvan_deref(old);
}

/**
 * Calculate dst = next(src, rel)
 */
static void
set_next(vset_t dst, vset_t src, vrel_t rel)
{
    assert(dst->projection == src->projection);
    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(sylvan_relprods(src->bdd, rel->bdd, rel->all_variables));

    // To use RelProd instead of RelProdS, uncomment the following lines and comment the preceding line
    // BDD temp = sylvan_relprod(src->bdd, rel->bdd, rel->variables_nonprime_set);
    // dst->bdd = sylvan_substitute(temp, rel->variables_prime_set);
} 

/**
 * Calculate dst = prev(src, rel)
 */
static void
set_prev(vset_t dst, vset_t src, vrel_t rel)
{
    assert(dst->projection == src->projection);
    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(sylvan_relprods_reversed(src->bdd, rel->bdd, rel->all_variables));
}

/**
 * Calculate projection of src onto dst
 */
static void
set_project(vset_t dst,vset_t src)
{
    sylvan_deref(dst->bdd);
    if (dst->projection != 0) dst->bdd = sylvan_exists(src->bdd, dst->projection);
    else dst->bdd = src->bdd;
    sylvan_ref(dst->bdd);
}

/**
 * Add all elements of src to dst and remove all elements that were in dst already from src
 * in other words: newDst = dst + src
 *                 newSrc = src - dst
 */
static void
set_zip(vset_t dst, vset_t src)
{
    BDD tmp1 = dst->bdd;
    BDD tmp2 = src->bdd;
    dst->bdd = sylvan_ref(sylvan_or(tmp1, tmp2));
    src->bdd = sylvan_ref(sylvan_diff(tmp2, tmp1));
    sylvan_deref(tmp1);
    sylvan_deref(tmp2);
}

/**
 * Add (src, dst) to the relation
 * Note that src and dst are PROJECTED (small) vectors!
 */
static void
rel_add(vrel_t rel, const int *src, const int *dst)
{
    BDD bdd_src = state_to_bdd(src, rel->vector_size, rel->vec_to_bddvar, sylvan_false);
    BDD bdd_dst = state_to_bdd(dst, rel->vector_size, rel->prime_vec_to_bddvar, sylvan_false);

    BDD part = sylvan_ref(sylvan_and(bdd_src, bdd_dst));
    sylvan_deref(bdd_src);
    sylvan_deref(bdd_dst);

    BDD old = rel->bdd;
    rel->bdd = sylvan_ref(sylvan_or(rel->bdd, part));
    sylvan_deref(old);
    sylvan_deref(part);
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
    dom->shared.rel_create=rel_create;
    dom->shared.rel_add=rel_add;
    dom->shared.rel_count=rel_count;
    dom->shared.set_next=set_next;
    dom->shared.set_prev=set_prev;
    // set_least_fixpoint
    // set_dot
    // rel_dot
}

/**
 * Create a domain with object size n
 */
vdom_t
vdom_create_sylvan(int n)
{
    Warning(info,"Creating a Sylvan domain.");

    // Call initializator of library (if needed)
    ltsmin_sylvan_init();

    // Create data structure of domain
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom,n);
    dom_set_function_pointers(dom);
    dom->bits_per_integer = fddbits;

    // Create universe
    dom->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * n);
    dom->prime_vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * n);
    for (int i=0; i<n; i++) {
        for (int j=0; j<fddbits; j++) {
            dom->vec_to_bddvar[i*fddbits+j] = 2*(i*fddbits+j);
            dom->prime_vec_to_bddvar[i*fddbits+j] = 2*(i*fddbits+j)+1;
        }
    }

    sylvan_gc_disable();
    dom->universe = sylvan_ref(sylvan_set_fromarray(dom->vec_to_bddvar, fddbits * n));
    dom->prime_universe = sylvan_ref(sylvan_set_fromarray(dom->prime_vec_to_bddvar, fddbits * n));
    sylvan_gc_enable();

    return dom;
}

