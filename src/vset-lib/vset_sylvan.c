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
static int datasize = 23;
static int maxtablesize = 28;
static int cachesize = 24;
static int maxcachesize = 28;
static int granularity = 1;

struct poptOption sylvan_options[] = {
    { "sylvan-bits",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &statebits, 0, "set number of bits per integer in the state vector","<bits>"},
    { "sylvan-tablesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &datasize , 0 , "set initial size of BDD table to 1<<datasize","<datasize>"},
    { "sylvan-maxtablesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxtablesize , 0 , "set maximum size of BDD table to 1<<maxsize","<maxtablesize>"},
    { "sylvan-cachesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cachesize , 0 , "set initial size of memoization cache to 1<<cachesize","<cachesize>"},
    { "sylvan-maxcachesize",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxcachesize , 0 , "set maximum size of memoization cache to 1<<cachesize","<maxcachesize>"},
    { "sylvan-granularity",0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &granularity , 0 , "only use memoization cache for every 1/granularity BDD levels","<granularity>"},
    POPT_TABLEEND
};

struct vector_domain
{
    struct vector_domain_shared shared;

    // Generated based on vec_to_bddvar and prime_vec_to_bddvar
    BDD state_variables;        // Every BDDVAR used for X
    BDD prime_variables;        // Every BDDVAR used for X'
};

struct vector_set
{
    vdom_t dom;

    BDD bdd;                    // Represented BDD
    size_t vector_size;         // How long is the vector in integers

    BDD projection;             // Universe \ X (for projection)
    BDD state_variables;        // X (for satcount etc)
};

struct vector_relation
{
    vdom_t dom;
    expand_cb expand;
    void *expand_ctx;

    BDD bdd;                    // Represented BDD
    size_t vector_size;         // How long is the vector in integers

    BDD state_variables;        // X
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

    LACE_ME;

    set->dom = dom;
    set->bdd = sylvan_false; // Initialize with an empty BDD

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
    sylvan_deref(set->bdd);
    sylvan_deref(set->projection);
    sylvan_deref(set->state_variables);
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
    LACE_ME;

    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));

    rel->dom = dom;
    rel->bdd = sylvan_false; // Initially, empty.

    // Relations are always projections.
    assert (k >= 0 && k<=dom->shared.size); 

    rel->vector_size = k;

    BDDVAR state_vars[statebits * k];
    BDDVAR prime_vars[statebits * k];

    for (int i=0; i<k; i++) {
        for (int j=0; j<statebits; j++) {
            state_vars[i*statebits+j] = 2*(proj[i]*statebits+j);
            prime_vars[i*statebits+j] = 2*(proj[i]*statebits+j)+1;
        }
    }

    rel->state_variables = sylvan_ref(sylvan_set_fromarray(state_vars, statebits * k));
    rel->prime_variables = sylvan_ref(sylvan_set_fromarray(prime_vars, statebits * k));
    rel->all_variables = sylvan_ref(sylvan_set_addall(rel->prime_variables, rel->state_variables));
    rel->all_action_variables = sylvan_ref(sylvan_set_addall(rel->all_variables, rel->dom->action_variables));

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
state_to_cube(const int* state, size_t state_length, char *arr)
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
    char cube[set->dom->shared.size * statebits];
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
    BDD prev = set->bdd;
    bdd_refs_push(bdd);
    set->bdd = sylvan_ref(sylvan_or(prev, bdd));
    bdd_refs_pop(1);
    sylvan_deref(prev);
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
    char cube[set->dom->shared.size * statebits];
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

    char* cube = (char*)alloca(set->vector_size*statebits*sizeof(char));
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
    BDD old = dst->bdd;
    if (src != dst) {
        sylvan_deref(old);
        dst->bdd = sylvan_ref(sylvan_and(match_bdd, src->bdd));
    } else {
        dst->bdd = sylvan_ref(sylvan_and(match_bdd, src->bdd));
        sylvan_deref(old);
    }
    bdd_refs_pop(1);
}

static void
set_count(vset_t set, long *nodes, bn_int_t *elements) 
{
    LACE_ME;
    if (nodes != NULL) *nodes = sylvan_nodecount(set->bdd);
    if (elements != NULL) bn_double2int((double)sylvan_satcount(set->bdd, set->state_variables), elements);
}

static void
rel_count(vrel_t rel, long *nodes, bn_int_t *elements)
{
    LACE_ME;
    if (nodes != NULL) *nodes = sylvan_nodecount(rel->bdd);
    if (elements != NULL) bn_double2int((double)sylvan_satcount(rel->bdd, rel->all_variables), elements);
}

/**
 * Calculate dst = dst + src
 */
static void
set_union(vset_t dst, vset_t src)
{
    LACE_ME;

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
    LACE_ME;

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
    LACE_ME;

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
    LACE_ME;

    assert(dst->projection == src->projection);
    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(sylvan_relnext(src->bdd, rel->bdd, rel->all_variables));
} 

/**
 * Calculate dst = prev(src, rel) intersected with univ
 */
static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    LACE_ME;

    assert(dst->projection == src->projection);
    sylvan_deref(dst->bdd);
    dst->bdd = sylvan_ref(sylvan_relprev(rel->bdd, src->bdd, rel->all_variables));
    set_intersect(dst, univ);
}

/**
 * Calculate projection of src onto dst
 */
static void
set_project(vset_t dst, vset_t src)
{
    sylvan_deref(dst->bdd);
    if (dst->projection != 0 && dst->projection != src->projection) {
        LACE_ME;
        dst->bdd = sylvan_exists(src->bdd, dst->projection);
    } else {
        dst->bdd = src->bdd;
    }
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
    LACE_ME;

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
    LACE_ME;

    check_state(src, rel->vector_size);
    check_state(dst, rel->vector_size);

    // make cube (interleaved)
    char cube[rel->vector_size * statebits * 2];
    char *arr = cube;

    for (size_t i=0; i<rel->vector_size; i++) {
        for (int j=0; j<statebits; j++) {
            *arr++ = (*src & (1LL<<(statebits-j-1))) ? 1 : 0;
            *arr++ = (*dst & (1LL<<(statebits-j-1))) ? 1 : 0;
        }
        src++;
        dst++;
    }

    // cube it
    BDD add = sylvan_cube(rel->all_variables, cube);

    // add it to old relation
    BDD old = rel->bdd;
    bdd_refs_push(add);
    rel->bdd = sylvan_ref(sylvan_or(rel->bdd, add));
    bdd_refs_pop(1);
    sylvan_deref(old);
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
rel_dot(FILE* fp, vrel_t src)
{
    sylvan_fprintdot(fp, src->bdd);
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
    size_t state_vars = sylvan_serialize_add(rel->state_variables);
    size_t prime_vars = sylvan_serialize_add(rel->prime_variables);
    sylvan_serialize_tofile(f);
    fwrite(&bdd, sizeof(size_t), 1, f);
    fwrite(&rel->vector_size, sizeof(size_t), 1, f);
    fwrite(&state_vars, sizeof(size_t), 1, f);
    fwrite(&prime_vars, sizeof(size_t), 1, f);
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
    fread(&bdd, sizeof(size_t), 1, f);
    fread(&set->vector_size, sizeof(size_t), 1, f);
    fread(&state_vars, sizeof(size_t), 1, f);

    set->bdd = sylvan_ref(sylvan_serialize_get_reversed(bdd));
    set->state_variables = sylvan_ref(sylvan_serialize_get_reversed(state_vars));

    LACE_ME;
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
    if (rel->state_variables) sylvan_deref(rel->state_variables);
    if (rel->prime_variables) sylvan_deref(rel->prime_variables);
    if (rel->all_variables) sylvan_deref(rel->all_variables);

    sylvan_serialize_fromfile(f);

    size_t bdd, state_vars, prime_vars;
    fread(&bdd, sizeof(size_t), 1, f);
    fread(&rel->vector_size, sizeof(size_t), 1, f);
    fread(&state_vars, sizeof(size_t), 1, f);
    fread(&prime_vars, sizeof(size_t), 1, f);

    rel->bdd = sylvan_ref(sylvan_serialize_get_reversed(bdd));
    rel->state_variables = sylvan_ref(sylvan_serialize_get_reversed(state_vars));
    rel->prime_variables = sylvan_ref(sylvan_serialize_get_reversed(prime_vars));

    LACE_ME;
    rel->all_variables = sylvan_ref(sylvan_set_addall(rel->prime_variables, rel->state_variables));
    rel->all_action_variables = sylvan_ref(sylvan_set_addall(rel->all_variables, rel->dom->action_variables));
}

static void
dom_save(FILE* f, vdom_t dom)
{
    int vector_size = dom->shared.size;
    fwrite(&vector_size, sizeof(int), 1, f);
    fwrite(&statebits, sizeof(int), 1, f);
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
}

/**
 * Create a domain with object size n
 */
vdom_t
vdom_create_sylvan(int n)
{
    LACE_ME;

    Warning(info,"Creating a Sylvan domain.");

    // Call initializator of library (if needed)
    static int initialized=0;
    if (!initialized) {
        sylvan_init_package(1LL<<datasize, 1LL<<maxtablesize, 1LL<<cachesize, 1LL<<maxcachesize);
        sylvan_init_bdd(granularity);
        initialized=1;
    }

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

    dom->state_variables = sylvan_ref(sylvan_set_fromarray(state_vars, statebits * n));
    dom->prime_variables = sylvan_ref(sylvan_set_fromarray(prime_vars, statebits * n));

    return dom;
}

vdom_t
vdom_create_sylvan_from_file(FILE *f)
{
    int vector_size;
    fread(&vector_size, sizeof(int), 1, f);
    fread(&statebits, sizeof(int), 1, f);
    return vdom_create_sylvan(vector_size);
}
