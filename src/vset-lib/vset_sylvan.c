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
static int datasize = 23;
static int cachesize = 23;
static int granularity = 1;

static void
ltsmin_sylvan_init() 
{
    static int initialized=0;
    if (!initialized) {
        sylvan_init(datasize, cachesize, granularity);
        initialized=1;
    }
}

struct poptOption sylvan_options[] = {
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

    LACE_ME;

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
    LACE_ME;

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
            if (X > fddbits) Abort("%d bits are not enough for the state vector (try option --sylvan-bits=%d)!", fddbits, X);
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
    LACE_ME;

    check_state(e, vec_length);

    size_t varcount = vec_length * fddbits;
    char* cube = (char*)alloca(varcount*sizeof(char));
    memset(cube, 0, varcount*sizeof(char));

    size_t i;
    int j;
    for (i=0;i<vec_length;i++) {
        for (j=0;j<fddbits;j++) {
            if (e[i] & (1<<(fddbits-j-1))) cube[i*fddbits+j] = 1;
        }
    }

    // check if in right order
    for (i=0;i<varcount-1;i++) assert(vec_to_bddvar[i]<vec_to_bddvar[i+1]);

    BDDSET meta = sylvan_set_fromarray(vec_to_bddvar, varcount);
    BDD bdd = sylvan_ref(sylvan_cube(meta, cube));
    BDD proj = sylvan_ref(sylvan_exists(bdd, projection));
    sylvan_deref(bdd);

    return proj;
}

/**
 * Adds e to set
 */
static void
set_add(vset_t set, const int* e)
{
    LACE_ME;

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
    LACE_ME;

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

/**
 * Generate one possible state
 */
static void
set_example(vset_t set, int *e)
{
    assert(set->bdd != sylvan_false);

    memset(e, 0, sizeof(int)*set->vector_size);

    char* cube = (char*)alloca(set->vector_size*fddbits*sizeof(char));
    sylvan_sat_one(set->bdd, set->vec_to_bddvar, set->vector_size*fddbits, cube);

    size_t i;
    int j;
    for (i=0;i<set->vector_size;i++) {
        for (j=0;j<fddbits;j++) {
            if (cube[i*fddbits+j]==1) e[i] |= 1<<(fddbits-j-1);
        }
    }
}

/**
 * Enumerate all states that match partial state <match>
 * <match> is p_len long
 * <proj> is a list of integers, containing indices of each match integer
 */
static void
set_enum_match(vset_t set, int p_len, int* proj, int* match, vset_element_cb cb, void* context) 
{
    LACE_ME;

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
    LACE_ME;

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
    LACE_ME;
    if (nodes != NULL) *nodes = sylvan_nodecount(set->bdd);
    if (elements != NULL) bn_double2int((double)sylvan_satcount(set->bdd, set->variables), elements);
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
    dst->bdd = sylvan_ref(sylvan_relprod_paired(src->bdd, rel->bdd, rel->all_variables));
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
    dst->bdd = sylvan_ref(sylvan_relprod_paired_prev(src->bdd, rel->bdd, rel->all_variables));
    set_intersect(dst, univ);
}

/**
 * Calculate projection of src onto dst
 */
static void
set_project(vset_t dst,vset_t src)
{
    LACE_ME;

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
    sylvan_serialize_tofile(f);
    fwrite(&bdd, sizeof(size_t), 1, f);
    fwrite(&set->vector_size, sizeof(size_t), 1, f);
    fwrite(set->vec_to_bddvar, sizeof(BDDVAR), set->vector_size*fddbits, f);
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
    sylvan_serialize_tofile(f);
    fwrite(&bdd, sizeof(size_t), 1, f);
    fwrite(&rel->vector_size, sizeof(size_t), 1, f);
    fwrite(rel->vec_to_bddvar, sizeof(BDDVAR), rel->vector_size*fddbits, f);
    fwrite(rel->prime_vec_to_bddvar, sizeof(BDDVAR), rel->vector_size*fddbits, f);
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

    size_t bdd;
    fread(&bdd, sizeof(size_t), 1, f);
    set->bdd = sylvan_ref(sylvan_serialize_get_reversed(bdd));

    fread(&set->vector_size, sizeof(size_t), 1, f);
    set->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * set->vector_size);
    fread(set->vec_to_bddvar, sizeof(BDDVAR), fddbits * set->vector_size, f);

    LACE_ME;
    sylvan_gc_disable();
    set->variables = sylvan_ref(sylvan_set_fromarray(set->vec_to_bddvar, fddbits * set->vector_size));
    set->projection = sylvan_ref(sylvan_set_removeall(dom->universe, set->variables));
    sylvan_gc_enable();

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
    if (rel->vec_to_bddvar) RTfree(rel->vec_to_bddvar);
    if (rel->prime_vec_to_bddvar) RTfree(rel->prime_vec_to_bddvar);
    if (rel->variables) sylvan_deref(rel->variables);
    if (rel->prime_variables) sylvan_deref(rel->prime_variables);
    if (rel->all_variables) sylvan_deref(rel->all_variables);

    sylvan_serialize_fromfile(f);

    size_t bdd;
    fread(&bdd, sizeof(size_t), 1, f);
    rel->bdd = sylvan_ref(sylvan_serialize_get_reversed(bdd));

    fread(&rel->vector_size, sizeof(size_t), 1, f);
    rel->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * rel->vector_size);
    rel->prime_vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * fddbits * rel->vector_size);
    fread(rel->vec_to_bddvar, sizeof(BDDVAR), rel->vector_size*fddbits, f);
    fread(rel->prime_vec_to_bddvar, sizeof(BDDVAR), rel->vector_size*fddbits, f);

    sylvan_gc_disable();
    LACE_ME;
    rel->variables = sylvan_ref(sylvan_set_fromarray(rel->vec_to_bddvar, fddbits * rel->vector_size));
    rel->prime_variables = sylvan_ref(sylvan_set_fromarray(rel->prime_vec_to_bddvar, fddbits * rel->vector_size));
    rel->all_variables = sylvan_ref(sylvan_set_addall(rel->prime_variables, rel->variables));
    sylvan_gc_enable();
}

static void
dom_save(FILE* f, vdom_t dom)
{
    size_t vector_size = dom->shared.size;
    size_t bits_per_integer = fddbits;
    fwrite(&vector_size, sizeof(size_t), 1, f);
    fwrite(&bits_per_integer, sizeof(size_t), 1, f);
    fwrite(dom->vec_to_bddvar, sizeof(BDDVAR), vector_size*bits_per_integer, f);
    fwrite(dom->prime_vec_to_bddvar, sizeof(BDDVAR), vector_size*bits_per_integer, f);
}

static void
init_universe(vdom_t dom)
{
    LACE_ME;

    int n = dom->shared.size;

    sylvan_gc_disable();
    dom->universe = sylvan_ref(sylvan_set_fromarray(dom->vec_to_bddvar, fddbits * n));
    dom->prime_universe = sylvan_ref(sylvan_set_fromarray(dom->prime_vec_to_bddvar, fddbits * n));
    sylvan_gc_enable();
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

    dom->shared.init_universe=init_universe;
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

    return dom;
}

vdom_t
vdom_create_sylvan_from_file(FILE *f)
{
    Warning(info,"Creating a Sylvan domain.");

    // Call initializator of library (if needed)
    ltsmin_sylvan_init();

    size_t vector_size, bits_per_integer;
    fread(&vector_size, sizeof(size_t), 1, f);
    fread(&bits_per_integer, sizeof(size_t), 1, f);

    // Create data structure of domain
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom, vector_size);
    dom_set_function_pointers(dom);
    fddbits = dom->bits_per_integer = bits_per_integer;

    dom->vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * bits_per_integer * vector_size);
    dom->prime_vec_to_bddvar = (BDDVAR*)RTmalloc(sizeof(BDDVAR) * bits_per_integer * vector_size);

    fread(dom->vec_to_bddvar, sizeof(BDDVAR), vector_size * bits_per_integer, f);
    fread(dom->prime_vec_to_bddvar, sizeof(BDDVAR), vector_size * bits_per_integer, f);

    return dom;
}
