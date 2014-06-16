#include <hre/config.h>

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

#include <hre/user.h>
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

struct poptOption sylvan_options[]= {
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
};

struct vector_set
{
    vdom_t dom;

    BDD bdd;                    // Represented BDD
    BDD projection;             // Universe \ X (for projection)
    BDD variables_set;          // X (for satcount etc)
    BDDVAR *variables_arr;      // for enum and other purposes
    int variables_size; 
};

struct vector_relation
{
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;

    BDD bdd;                    // Represented BDD
    BDD projection;             // Universe \ (X U X') (for projection)
    BDD variables_set;          // X U X'
    BDD variables_prime_set;    // X
    BDD variables_nonprime_set; // X'

    BDDVAR *variables_arr;      // for enum and other internal purposes
    int variables_size; 
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
    sylvan_gc_disable();

    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));

    set->dom = dom;
    set->bdd = sylvan_false; // Initialize with an empty BDD

    if (k>=0 && k<dom->shared.size) {
        // We are creating a projection set, including only variables in "proj"
        // Variables not in proj will be existentially quantified 
        set->projection = sylvan_false;
        set->variables_set = sylvan_false;

        set->variables_size = fddbits * k;
        set->variables_arr = RTmalloc(sizeof(BDDVAR[k*fddbits]));

        int j=k-1, m=(k*fddbits)-1, n=0;
        for (int i=dom->shared.size-1;i>=0;i--) {
            if (j>=0 && j<k && proj[j] == i) {
                j--;
                for (n=fddbits-1;n>=0;n--) {
                    set->variables_arr[m--] = (i*fddbits+n) * 2;
                    set->variables_set = sylvan_or(set->variables_set, sylvan_ithvar((i*fddbits+n)*2));
                }
            } else {
                // to be quantified
                for (n=fddbits-1;n>=0;n--) { // note starting with last
                    set->projection = sylvan_or(set->projection, sylvan_ithvar((i*fddbits+n)*2));
                }
            }
        }
    } else {
        set->projection = sylvan_false;
        set->variables_set = sylvan_false;
        set->variables_size = dom->shared.size*fddbits;
        set->variables_arr = RTmalloc(sizeof(BDDVAR) * set->variables_size);

        // start with last
        for (int i=dom->shared.size-1;i>=0;i--) {
            for (int j=fddbits-1;j>=0;j--) {
                set->variables_arr[(i*fddbits+j)] = (i*fddbits+j) * 2;
                set->variables_set = sylvan_or(set->variables_set, sylvan_ithvar((i*fddbits+j)*2));
            }
        }
    }

    sylvan_ref(set->projection);
    sylvan_ref(set->variables_set);
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
    sylvan_deref(set->variables_set);
    RTfree(set->variables_arr);
    RTfree(set);
}

/**
 * Create a "relation" (Dom x Dom)
 * k must not be 0
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

    rel->projection = sylvan_false;
    rel->variables_set = sylvan_false;
    rel->variables_prime_set = sylvan_false;
    rel->variables_nonprime_set = sylvan_false;

    rel->variables_size = 2 * fddbits * k;
    rel->variables_arr = RTmalloc(sizeof(BDDVAR[rel->variables_size]));

    int j=k-1, m=(k*fddbits)*2-1, n=0;
    for (int i=dom->shared.size-1; i>=0; i--) {
        if (j>=0 && proj[j] == i) {
            j--;
            for (n=fddbits-1; n>=0; n--) {
                BDDVAR x = (i*fddbits+n) * 2;
                BDDVAR x_prime = (i*fddbits+n) * 2 + 1;
                rel->variables_arr[m--] = x_prime;
                rel->variables_arr[m--] = x;

                BDD bdd_x_prime, bdd_x;

                rel->variables_set = sylvan_or(rel->variables_set, sylvan_or(sylvan_ithvar(x_prime), sylvan_ithvar(x)));
                rel->variables_prime_set = sylvan_or(rel->variables_prime_set, sylvan_ithvar(x_prime));
                rel->variables_nonprime_set = sylvan_or(rel->variables_nonprime_set, sylvan_ithvar(x));
            }
        } else {
            for (n=fddbits-1; n>=0; n--) {
                BDDVAR x = (i*fddbits+n) * 2;
                BDDVAR x_prime = (i*fddbits+n) * 2 + 1;
                rel->projection = sylvan_or(rel->projection, sylvan_or(sylvan_ithvar(x_prime), sylvan_ithvar(x)));
            }
        }
    }

    sylvan_ref(rel->variables_set);
    sylvan_ref(rel->variables_prime_set);
    sylvan_ref(rel->variables_nonprime_set);
    sylvan_ref(rel->projection);
    sylvan_gc_enable();

    return rel;
}

static int optimal_bits_per_state = 0;

/* Helper function to detect problems with the number of bits per stats 
TODO: handle negative numbers?? */
static void
check_state(const int *e, int N) 
{
    for (int i=0; i<N; i++) {
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
state_to_bdd(vset_t set, const int* e)
{
    int N = set->dom->shared.size;
    BDD bdd = sylvan_true;

    check_state(e, N);

    // Construct BDD from state (go from last variable to first variable)
    for(int i=N-1;i>=0;i--) { // start with last byte (N-1)
        uint32_t b = e[i];

        for (int j=fddbits-1;j>=0;j--) { // start with last bit (=lowest)
            int x = i*fddbits+j;
            // TODO: fix this. if in variables_arr then do else not...
            BDD val = sylvan_ref(sylvan_ithvar(x * 2));
            BDD old = bdd; 
            if ((b & 1) == 0) val = sylvan_not(val);
            bdd = sylvan_ref(sylvan_and(val, bdd));
            sylvan_deref(old);
            sylvan_deref(val);
            b >>= 1;
        } 
    }

    if (set->projection != sylvan_false) {
        BDD old = bdd;
        bdd = sylvan_ref(sylvan_exists(old, set->projection)); // NOTE: this can be more efficient
        sylvan_deref(old);
    }

    return bdd;
}

/**
 * Adds e to set
 */
static void
set_add(vset_t set, const int* e)
{
    BDD bdd = state_to_bdd(set,e);
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
    BDD ebdd = state_to_bdd(set, e);
    int res = sylvan_and(set->bdd, ebdd) != sylvan_false ? 1 : 0;
    sylvan_deref(ebdd);
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
set_enum_do(BDD root, const int variables_size, const BDDVAR *variables, int *vec, int n, vset_element_cb cb, void* context)
{
    if (root == sylvan_false) return;
    if (n == variables_size) {
        // We're at the tail
        assert(root == sylvan_true);
        cb(context,vec);
    } else {
        uint32_t v = variables[n]; 

        int i = n / fddbits;        // which slot in the state vector
        int j = n % fddbits;        // which bit? 

        uint32_t bitmask = 1 << (fddbits-1-j);

        if (root == sylvan_true || v != sylvan_var(root)) {
            // n is skipped, take both
            vec[i] |= bitmask;
            set_enum_do(root, variables_size, variables, vec, n+1, cb, context);
            vec[i] &= ~bitmask;
            set_enum_do(root, variables_size, variables, vec, n+1, cb, context);
        } else {
            vec[i] |= bitmask;
            set_enum_do((sylvan_high(root)), variables_size, variables, vec, n+1, cb, context);
            vec[i] &= ~bitmask;
            set_enum_do((sylvan_low(root)), variables_size, variables, vec, n+1, cb, context);
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
    int vec[set->variables_size/fddbits];
    memset(vec, 0, sizeof(int)*set->variables_size/fddbits);
    set_enum_do(set->bdd, set->variables_size, set->variables_arr, vec, 0, cb, context);
}

static int
set_example_do(vset_t set, BDD root, int *vec, int n)
{
    if (root == sylvan_false) return 0;
    if (n == set->variables_size) {
        // We found one!
        assert(root == sylvan_true);
        return 1;
    } else {
        uint32_t v = set->variables_arr[n]; // figure out level
        int i = n / fddbits;
        int j = n % fddbits;
        uint32_t bitmask = 1 << (fddbits-1-j);
        if (root == sylvan_true || v != sylvan_var(root)) {
            // n is skipped, try any
            vec[i] |= bitmask;
            return set_example_do(set, root, vec, n+1);
        } else {
            vec[i] |= bitmask;
            if (set_example_do(set, (sylvan_high(root)), vec, n+1)) {
                return 1;
            }
            vec[i] &= ~bitmask;
            int result = set_example_do(set, (sylvan_low(root)), vec, n+1);
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
    set_example_do(set, set->bdd, e, 0);
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

    BDD match_bdd = sylvan_true;
    for (int i=p_len-1;i>=0;i--) {
        int j = proj[i];
        uint32_t b = match[i];
        for (int p=fddbits-1;p>=0;p--) {
            BDD p_bdd = sylvan_ref(sylvan_ithvar(j*fddbits*2+p*2));
            BDD old = match_bdd;
            if (b & 1) match_bdd = sylvan_ref(sylvan_and(p_bdd, match_bdd));
            else match_bdd = sylvan_ref(sylvan_and(sylvan_not(p_bdd), match_bdd));
            sylvan_deref(p_bdd);
            sylvan_deref(old);
            b>>=1;
        }
    }

    BDD old = match_bdd;
    match_bdd = sylvan_ref(sylvan_and(match_bdd, set->bdd));
    sylvan_deref(old);

    int vec[set->variables_size/fddbits];
    memset(vec, 0, sizeof(int)*set->variables_size/fddbits);
    set_enum_do(match_bdd, set->variables_size, set->variables_arr, vec, 0, cb, context);
    sylvan_deref(match_bdd);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int* proj, int*match)
{
    // Create bdd of "match"
    // Assumption: proj is ordered (if not, you get bad performance)

    BDD match_bdd = sylvan_true;
    for (int i=p_len-1; i>=0; i--) {
        int j = proj[i];
        uint32_t b = match[i];
        for (int p=fddbits-1; p>=0; p--) {
            BDD p_bdd = sylvan_ref(sylvan_ithvar(j*fddbits*2+p*2));
            BDD old = match_bdd;
            if (b & 1) match_bdd = sylvan_ref(sylvan_and(p_bdd, match_bdd));
            else match_bdd = sylvan_ref(sylvan_and(sylvan_not(p_bdd), match_bdd));
            sylvan_deref(p_bdd);
            sylvan_deref(old);
            b>>=1;
        }
    }

    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(sylvan_and(match_bdd, src->bdd));
    sylvan_deref(match_bdd);
}

static void
set_count(vset_t set, long *nodes, bn_int_t *elements) 
{
    *nodes = sylvan_nodecount(set->bdd);
    double count = (double)sylvan_satcount(set->bdd, set->variables_set);
    bn_double2int(count, elements);
}

static void
rel_count(vrel_t rel, long *nodes, bn_int_t *elements)
{
    *nodes = sylvan_nodecount(rel->bdd);
    double count = (double)sylvan_satcount(rel->bdd, rel->variables_set);
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
    dst->bdd = sylvan_ref(sylvan_relprods(src->bdd, rel->bdd, rel->variables_set));

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
    dst->bdd = sylvan_ref(sylvan_relprods_reversed(src->bdd, rel->bdd, rel->variables_set));
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
    int N = rel->variables_size / (fddbits*2);
    BDD bdd = sylvan_true;

    check_state(src, N);
    check_state(dst, N);

    // Construct BDD from state (go from last variable to first variable)
    for(int i=N-1;i>=0;i--) { // bytes, begin with last
        int s = src[i];
        int d = dst[i];

        // k is the last variable...
        BDDVAR k = rel->variables_arr[(i*fddbits+fddbits-1)*2];

        for (int j=fddbits-1;j>=0;j--) { // bits, begin with last
            BDD x = sylvan_ref(sylvan_ithvar(k));
            BDD x_prime = sylvan_ref(sylvan_ithvar(k+1));

            BDD old = bdd;
            if (d & 1) bdd = sylvan_ref(sylvan_and(bdd, x_prime));
            else bdd = sylvan_ref(sylvan_and(bdd, sylvan_not(x_prime)));
            sylvan_deref(old);
            d >>= 1;

            old = bdd;
            if (s & 1) bdd = sylvan_ref(sylvan_and(bdd, x));
            else bdd = sylvan_ref(sylvan_and(bdd, sylvan_not(x)));
            sylvan_deref(old);
            s >>= 1;

            k-=2;
            sylvan_deref(x);
            sylvan_deref(x_prime);
        } 
    }

    BDD old = rel->bdd;
    rel->bdd = sylvan_ref(sylvan_or(rel->bdd, bdd));
    sylvan_deref(old);
    sylvan_deref(bdd);
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

    return dom;
}

