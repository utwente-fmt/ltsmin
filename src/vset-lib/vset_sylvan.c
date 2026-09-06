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
#include <vset-lib/vset_sylvan_reachdd.h>
#include <sylvan.h>
#include <sylvan_int.h>

static int xstatebits = 16;   // maximum / default bits per integer
static int xactionbits = 16;  // maximum / default bits per action variable
static int tablesize = 24;    // initial size of nodes table
static int maxtablesize = 28; // maximum size of nodes table
static int cachesize = 23;    // initial size of operation cache
static int maxcachesize = 27; // maximum size of operation cache
static int granularity = 1;   // caching granularity for Sylvan
static char* sizes = NULL;    // for setting table sizes in command line
static int reachdd_pin_all = 0; // ReachDD: use the apply-whole schedule
static int reachdd_learn = 1;   // ReachDD: learn transitions inside the operation

struct poptOption sylvan_options[] = {
    { "sylvan-bits", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &xstatebits, 0, "set number of bits per integer in the state vector", "<bits>"},
    { "sylvan-sizes", 0, POPT_ARG_STRING, &sizes, 0, "set nodes table and operation cache sizes (powers of 2)", "<tablesize>,<tablemax>,<cachesize>,<cachemax>"},
    { "sylvan-tablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &tablesize , 0 , "set initial size of BDD table to 1<<tablesize", "<tablesize>"},
    { "sylvan-maxtablesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxtablesize , 0 , "set maximum size of BDD table to 1<<maxsize", "<maxtablesize>"},
    { "sylvan-cachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &cachesize , 0 , "set initial size of operation cache to 1<<cachesize", "<cachesize>"},
    { "sylvan-maxcachesize", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &maxcachesize , 0 , "set maximum size of operation cache to 1<<cachesize", "<maxcachesize>"},
    { "sylvan-granularity", 0, POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT, &granularity , 0 , "only use operation cache every <granularity> BDD levels", "<granularity>"},
    { "reachdd-pin-all", 0, POPT_ARG_VAL, &reachdd_pin_all, 1, "with --saturation=reachdd, apply every relation whole at its entry level (the saturation schedule of Cor. 5) instead of splitting it; for debugging", NULL},
    { "reachdd-no-learn", 0, POPT_ARG_VAL, &reachdd_learn, 0, "with --saturation=reachdd, do not expand relations inside the operation; the driver then has to learn over all visited states and iterate, as it did before on-the-fly learning; for A/B testing", NULL},
    POPT_TABLEEND
};

struct vector_domain
{
    struct vector_domain_shared shared;

    int vectorsize;             // number of integers for each full state
    int *statebits;             // number of bits for each state variable
    int actionbits;             // number of bits for the action label

    /* The following are automatically generated from the above */

    int totalbits;              // number of bits for each full state
    BDD state_variables;        // set of all state variables
    BDD prime_variables;        // set of all prime state variables
    BDD action_variables;       // set of action label variables
};

struct vector_set
{
    vdom_t dom;

    BDD bdd;                    // the BDD that encodes this set

    int k;                      // size of state vectors in this set
    int *proj;                  // which variables are in the set

    /* The following are automatically generated from the above */

    BDD state_variables;        // set of state variables in this set
};

struct vector_relation
{
    vdom_t dom;

    expand_cb expand;           // for transition learning during saturation
    void *expand_ctx;           // context pointer for expand

    BDD bdd;                    // the BDD that encodes this transition relation

    int r_k, w_k;               // number of read/write in this relation
    int *r_proj;                // which variables are read
    int *w_proj;                // which variables are written

    /* The following are automatically generated from the above */

    BDD all_variables;          // all state/prime variables of this relation
    BDD all_action_variables;   // union of all_variables and dom->action_variables
    BDD cur_is_next;            // helper BDD for read-only (copy) variables
    BDD state_variables;        // set of read state variables
    BDD prime_variables;        // set of written prime state variables
};

/**
 * Create a new BDD set in the domain dom
 * k is the number of integers in proj
 * proj is a list of indices of the state vector in the projection
 * (if k=-1, proj=NULL, then it is not projected)
 */
static vset_t
set_create(vdom_t dom, int k, int* proj) 
{
    vset_t set = (vset_t)RTmalloc(sizeof(struct vector_set));

    set->dom = dom;
    set->bdd = sylvan_false; // Initialize with an empty BDD
    set->state_variables = sylvan_false;

    sylvan_protect(&set->bdd);
    sylvan_protect(&set->state_variables);

    if (k == -1) {
        set->k = -1;
        set->proj = NULL;
        set->state_variables = dom->state_variables;
    } else {
        set->k = k;
        set->proj = RTmalloc(sizeof(int[k]));
        memcpy(set->proj, proj, sizeof(int[k]));

        uint32_t vars[xstatebits * k];
        uint32_t curvar = 0; // start with variable 0
        int j=0; // j: current proj element
        int n=0; // n: current number of vars in vars
        for (int i=0; i<dom->vectorsize && j<k; i++) {
            if (i == proj[j]) {
                for (int x=0; x<dom->statebits[i]; x++) {
                    vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * dom->statebits[i];
            }
        }

        set->state_variables = sylvan_set_fromarray(vars, n);
    }

    return set;
}

/**
 * Destroy a set.
 */
static void
set_destroy(vset_t set) 
{
    sylvan_unprotect(&set->bdd);
    sylvan_unprotect(&set->state_variables);
    if (set->proj != NULL) RTfree(set->proj);
    RTfree(set);
}

/**
 * Create a transition relation
 * There are r_k 'read' variables and w_k 'write' variables
 */
static vrel_t
rel_create_rw(vdom_t dom, int r_k, int *r_proj, int w_k, int *w_proj)
{
    vrel_t rel = (vrel_t)RTmalloc(sizeof(struct vector_relation));

    rel->dom = dom;
    rel->bdd = sylvan_false; // Initially, empty.
    rel->all_variables = sylvan_false;
    rel->all_action_variables = sylvan_false;
    rel->cur_is_next = sylvan_false;
    rel->state_variables = sylvan_false;
    rel->prime_variables = sylvan_false;

    sylvan_protect(&rel->bdd);
    sylvan_protect(&rel->all_variables);
    sylvan_protect(&rel->all_action_variables);
    sylvan_protect(&rel->cur_is_next);
    sylvan_protect(&rel->state_variables);
    sylvan_protect(&rel->prime_variables);

    rel->r_k = r_k;
    rel->w_k = w_k;
    rel->r_proj = (int*)RTmalloc(sizeof(int)*r_k);
    rel->w_proj = (int*)RTmalloc(sizeof(int)*w_k);
    memcpy(rel->r_proj, r_proj, sizeof(int)*r_k);
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
    {
        uint32_t state_vars[xstatebits * r_k];
        uint32_t curvar = 0; // start with variable 0
        int i=0, j=0, n=0;
        for (; i<dom->vectorsize && j<r_k; i++) {
            if (i == r_proj[j]) {
                for (int k=0; k<dom->statebits[i]; k++) {
                    state_vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * dom->statebits[i];
            }
        }
        rel->state_variables = sylvan_set_fromarray(state_vars, n);
    }

    /* Create set of (write) prime variables for sylvan_cube */
    {
        uint32_t prime_vars[xstatebits * w_k];
        uint32_t curvar = 0; // start with variable 0
        int i=0, j=0, n=0;
        for (; i<dom->vectorsize && j<w_k; i++) {
            if (i == w_proj[j]) {
                for (int k=0; k<dom->statebits[i]; k++) {
                    prime_vars[n++] = curvar + 1;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * dom->statebits[i];
            }
        }
        rel->prime_variables = sylvan_set_fromarray(prime_vars, n);
    }

    /* Compute all_variables, which are all variables the transition relation is defined on */
    {
        uint32_t all_vars[xstatebits * a_k * 2];
        uint32_t curvar = 0; // start with variable 0
        int i=0, j=0, n=0;
        for (; i<dom->vectorsize && j<a_k; i++) {
            if (i == a_proj[j]) {
                for (int k=0; k<dom->statebits[i]; k++) {
                    all_vars[n++] = curvar;
                    all_vars[n++] = curvar + 1;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * dom->statebits[i];
            }
        }
        rel->all_variables = sylvan_set_fromarray(all_vars, n);
        rel->all_action_variables = sylvan_set_addall(rel->all_variables, rel->dom->action_variables);
    }

    /* Compute cur_is_next for variables in ro_proj */
    {
        /* Build ro_vars first */
        uint32_t ro_vars[xstatebits * ro_k];
        uint32_t curvar = 0; // start with variable 0
        int i=0, j=0, n=0;
        for (; i<dom->vectorsize && j<ro_k; i++) {
            if (i == ro_proj[j]) {
                for (int k=0; k<dom->statebits[i]; k++) {
                    ro_vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * dom->statebits[i];
            }
        }
        /* Now build cur_is_next */
        rel->cur_is_next = sylvan_true;
        for (int i=n-1; i>=0; i--) {
            // FIXME sylvan_makenode is dangerous!! it should run in a Lace thread!!
            BDD low = sylvan_makenode(ro_vars[i]+1, rel->cur_is_next, sylvan_false);
            mtbdd_refs_push(low);
            BDD high = sylvan_makenode(ro_vars[i]+1, sylvan_false, rel->cur_is_next);
            mtbdd_refs_pop(1);
            rel->cur_is_next = sylvan_makenode(ro_vars[i], low, high);
        }
    }

    return rel;
}

/**
 * Destroy a relation.
 */
static void
rel_destroy(vrel_t rel)
{
    sylvan_unprotect(&rel->bdd);
    sylvan_unprotect(&rel->all_variables);
    sylvan_unprotect(&rel->all_action_variables);
    sylvan_unprotect(&rel->cur_is_next);
    sylvan_unprotect(&rel->state_variables);
    sylvan_unprotect(&rel->prime_variables);
    RTfree(rel->r_proj);
    RTfree(rel->w_proj);
    RTfree(rel);
}

/**
 * Helper function to detect problems with the number of bits per state.
 * e is a projected state.
 */
static void
check_state(vdom_t dom, int k, int* proj, const int *e)
{
    if (k == -1) {
        for (int i=0; i<dom->vectorsize; i++) {
            if (e[i] == 0) continue;
            register int X = 32 - __builtin_clz(e[i]);
            if (X <= dom->statebits[i]) continue;
            Abort("%d bits are not enough for the state vector (try option --sylvan-bits=%d)!", dom->statebits[i], X);
        }
    } else {
        int j=0;
        for (int i=0; i<dom->vectorsize && j<k; i++) {
            if (i != proj[j]) continue;
            if (e[j] != 0) {
                register int X = 32 - __builtin_clz(e[j]);
                if (X <= dom->statebits[i]) continue;
                Abort("%d bits are not enough for the state vector (try option --sylvan-bits=%d)!", dom->statebits[i], X);
            }
            j++;
        }
    }
}

/**
 * Obtain Boolean encoding from integer state vector
 * (projected vector)
 */
static void
state_to_cube(vdom_t dom, int k, int* proj, const int* state, uint8_t *arr)
{
    if (k == -1) {
        for (int i=0; i<dom->vectorsize; i++) {
            const int sb = dom->statebits[i];
            for (int n=0; n<sb; n++) *arr++ = (*state & (1LL<<(sb-n-1))) ? 1 : 0;
            state++;
        }
    } else {
        int j=0;
        for (int i=0; i<dom->vectorsize && j<k; i++) {
            if (i != proj[j]) continue;
            const int sb = dom->statebits[i];
            for (int n=0; n<sb; n++) *arr++ = (*state & (1LL<<(sb-n-1))) ? 1 : 0;
            state++;
            j++;
        }
    }
}

/**
 * Obtain integer state vector from Boolean encoding
 * (projected vector)
 */
static void
state_from_cube(vdom_t dom, int k, int* proj, int* vec, const uint8_t *arr)
{
    if (k == -1) {
        for (int i=0; i<dom->vectorsize; i++) {
            const int sb = dom->statebits[i];
            *vec = 0;
            for (int n=0; n<sb; n++) {
                *vec <<= 1;
                if (*arr++) *vec |= 1;
            }
            vec++;
        }
    } else {
        int j=0;
        for (int i=0; i<dom->vectorsize && j<k; i++) {
            if (i != proj[j]) continue;
            const int sb = dom->statebits[i];
            *vec = 0;
            for (int n=0; n<sb; n++) {
                *vec <<= 1;
                if (*arr++) *vec |= 1;
            }
            vec++;
            j++;
        }
    }
}

/**
 * Adds e to set
 * Note that e is a full state vector and might have to be projected
 */
static void
set_add(vset_t set, const int* e)
{
    BDD bdd;
    if (set->k == -1) {
        check_state(set->dom, -1, NULL, e);
        uint8_t cube[set->dom->vectorsize * xstatebits];
        state_to_cube(set->dom, -1, NULL, e, cube);
        bdd = sylvan_cube(set->state_variables, cube);
    } else {
        // e is a full state vector; get the short vector
        int f[set->k];
        for (int i=0, j=0; i<set->dom->vectorsize && j<set->k; i++) {
            if (i == set->proj[j]) f[j++] = e[i];
        }
        check_state(set->dom, set->k, set->proj, f);
        uint8_t cube[set->k * xstatebits];
        state_to_cube(set->dom, set->k, set->proj, f, cube);
        bdd = sylvan_cube(set->state_variables, cube);
    }

    // add to set
    mtbdd_refs_push(bdd);
    set->bdd = sylvan_or(set->bdd, bdd);
    mtbdd_refs_pop(1);
}

/**
 * Returns 1 if e is a member of set, 0 otherwise
 */
static int
set_member(vset_t set, const int* e)
{
    BDD bdd;
    if (set->k == -1) {
        check_state(set->dom, -1, NULL, e);
        uint8_t cube[set->dom->shared.size * xstatebits];
        state_to_cube(set->dom, -1, NULL, e, cube);
        bdd = sylvan_cube(set->dom->state_variables, cube);
    } else {
        // e is a full state vector; get the short vector
        int f[set->k];
        for (int i=0, j=0; i<set->dom->vectorsize && j<set->k; i++) {
            if (i == set->proj[j]) f[j++] = e[i];
        }
        check_state(set->dom, set->k, set->proj, f);
        uint8_t cube[set->k * xstatebits];
        state_to_cube(set->dom, set->k, set->proj, f, cube);
        bdd = sylvan_cube(set->state_variables, cube);
     }

    // check if in set
    mtbdd_refs_push(bdd);
    int res = sylvan_and(set->bdd, bdd) != sylvan_false ? 1 : 0;
    mtbdd_refs_pop(1);
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
    assert(set1->state_variables == set2->state_variables);
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
    assert(dst->state_variables == src->state_variables);
    dst->bdd = src->bdd;
}

/**
 * Enumerate all elements of the set. Calls cb(context, const int* ELEMENT)
 * for every found element. Elements are projected, meaning not the full
 * state vector is returned, but only the selected bytes.
 */
static void
set_enum(vset_t set, vset_element_cb cb, void* context)
{
    int k = set->k == -1 ? set->dom->vectorsize : set->k;
    int vec[k];
    uint8_t arr[xstatebits * k];
    MTBDD res = mtbdd_enum_all_first(set->bdd, set->state_variables, arr, NULL);
    while (res != mtbdd_false) {
        state_from_cube(set->dom, set->k, set->proj, vec, arr);
        cb(context, vec);
        res = mtbdd_enum_all_next(set->bdd, set->state_variables, arr, NULL);
    }
}

/**
 * Context struct for set_update
 */
struct set_update_context
{
    vset_t set;
    vset_t src;
    vset_update_cb cb;
    void* context;
};

/**
 * Callback implementation for set_update
 */
TASK_2(BDD, bdd_set_updater, void*, _ctx, uint8_t*, arr)
{
    struct set_update_context *ctx = (struct set_update_context*)_ctx;
    const vset_t src = ctx->src;
    size_t vec_len = src->k == -1 ? src->dom->vectorsize : src->k;
    int vec[vec_len];
    state_from_cube(src->dom, src->k, src->proj, vec, arr);

    const vset_t dst = ctx->set;
    struct vector_set dummyset;
    dummyset.dom = dst->dom;
    dummyset.k = dst->k;
    dummyset.proj = dst->proj;
    dummyset.state_variables = dst->state_variables;
    dummyset.bdd = sylvan_false;

    mtbdd_refs_pushptr(&dummyset.bdd);
    ctx->cb(&dummyset, ctx->context, (int*)vec);
    mtbdd_refs_popptr(1);

    return dummyset.bdd;
}

/**
 * Combination of enumeration and set union of the result
 */
static void
set_update(vset_t dst, vset_t src, vset_update_cb cb, void* context)
{
    struct set_update_context ctx = (struct set_update_context){dst, src, cb, context};
    BDD result = sylvan_collect(src->bdd, src->state_variables, bdd_set_updater_CALL, (void*)&ctx);
    mtbdd_refs_push(result);
    dst->bdd = sylvan_or(dst->bdd, result);
    mtbdd_refs_pop(1);
}

/**
 * Generate one possible state (short vector?!)
 */
static void
set_example(vset_t set, int *e)
{
    assert(set->bdd != sylvan_false);

    size_t k = set->k == -1 ? set->dom->vectorsize : set->k;
    uint8_t cube[xstatebits*k];
    sylvan_sat_one(set->bdd, set->state_variables, cube);

    // memset(e, 0, k); actually, state_from_cube takes care of that
    state_from_cube(set->dom, set->k, set->proj, e, cube);
}

/**
 * Enumerate all states that match partial state <match>
 * <match> is p_len long
 * <proj> is an ordered list of integers, containing indices of each match integer
 */
static void
set_enum_match(vset_t set, int p_len, int* proj, int* match, vset_element_cb cb, void* context) 
{
    /* create bdd of 'match' */
    BDD match_bdd = sylvan_true;
    mtbdd_refs_pushptr(&match_bdd);
    {
        // first create array with vars
        uint32_t vars[p_len * xstatebits];
        uint32_t curvar = 0; // start with variable 0
        int j=0; // j: current proj element
        int n=0; // n: current number of vars in state_vars
        for (int i=0; i<set->dom->vectorsize && j<p_len; i++) {
            const int sb = set->dom->statebits[i];
            if (i == proj[j]) {
                for (int x=0; x<sb; x++) {
                    vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * sb;
            }
        }
        // then create match_bdd
        uint8_t arr[p_len * xstatebits];
        int m=0;
        for (int i=0; i<p_len; i++) {
            const int sb = set->dom->statebits[proj[i]];
            for (int j=0; j<sb; j++) arr[m++] = (match[i] & (1LL<<(sb-j-1))) ? 1 : 0;
        }
        assert(n==m);
        BDD _vars = sylvan_set_fromarray(vars, n);
        mtbdd_refs_push(_vars);
        match_bdd = sylvan_cube(_vars, arr);
        mtbdd_refs_pop(1);
    }
    match_bdd = sylvan_and(match_bdd, set->bdd);

    int k = set->k == -1? set->dom->vectorsize : set->k;
    int vec[k];
    uint8_t arr[xstatebits * k];
    MTBDD res = mtbdd_enum_all_first(match_bdd, set->state_variables, arr, NULL);
    while (res != mtbdd_false) {
        state_from_cube(set->dom, set->k, set->proj, vec, arr);
        cb(context, vec);
        res = mtbdd_enum_all_next(match_bdd, set->state_variables, arr, NULL);
    }

    mtbdd_refs_popptr(1);
}

static void
set_copy_match(vset_t dst, vset_t src, int p_len, int* proj, int* match)
{
    /* create bdd of 'match' */
    BDD match_bdd = sylvan_true;
    mtbdd_refs_pushptr(&match_bdd);
    {
        // first create array with vars
        uint32_t vars[p_len * xstatebits];
        uint32_t curvar = 0; // start with variable 0
        int j=0; // j: current proj element
        int n=0; // n: current number of vars in state_vars
        for (int i=0; i<src->dom->vectorsize && j<p_len; i++) {
            const int sb = src->dom->statebits[i];
            if (i == proj[j]) {
                for (int x=0; x<sb; x++) {
                    vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * sb;
            }
        }
        // then create match_bdd
        uint8_t arr[p_len * xstatebits];
        state_to_cube(src->dom, p_len, proj, match, arr);
        BDD _vars = sylvan_set_fromarray(vars, n);
        mtbdd_refs_push(_vars);
        match_bdd = sylvan_cube(_vars, arr);
        mtbdd_refs_pop(1);
    }
    dst->bdd = sylvan_and(match_bdd, src->bdd);
    mtbdd_refs_popptr(1);
}

static void
set_count(vset_t set, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = sylvan_nodecount(set->bdd);
    if (elements != NULL) *elements = (double) sylvan_satcount(set->bdd, set->state_variables);
}

static void
rel_count(vrel_t rel, long *nodes, double *elements)
{
    if (nodes != NULL) *nodes = sylvan_nodecount(rel->bdd);
    if (elements != NULL) *elements = (double)sylvan_satcount(rel->bdd, rel->all_action_variables);
}

/**
 * Calculate dst = dst + src
 * This implementation is thread-safe; i.e., if dst is changed during operation, reload dst and retry
 */
static void
set_union(vset_t dst, vset_t src)
{
    if (dst != src) {
        BDD cur = dst->bdd;
        BDD res = src->bdd;
        mtbdd_refs_pushptr(&cur);
        mtbdd_refs_pushptr(&res);
        for (;;) {
            res = sylvan_or(cur, res);
            BDD test = __sync_val_compare_and_swap(&dst->bdd, cur, res);
            if (test == cur) break;
            else cur = test;
        }
        mtbdd_refs_popptr(2);
    }
}

/**
 * Calculate dst = dst /\ src
 */
static void
set_intersect(vset_t dst, vset_t src)
{
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
    // check if dst and src are the same projections
    assert(dst->state_variables == src->state_variables);

    dst->bdd = sylvan_relnext(src->bdd, rel->bdd, rel->all_variables);
} 

/**
 * Calculate dst = prev(src, rel) intersected with univ
 */
static void
set_prev(vset_t dst, vset_t src, vrel_t rel, vset_t univ)
{
    // defined on same variables?
    assert(dst->state_variables == src->state_variables);

    if (dst == univ) {
        BDD tmp = sylvan_relprev(rel->bdd, src->bdd, rel->all_variables);
        mtbdd_refs_push(tmp);
        dst->bdd = sylvan_and(tmp, univ->bdd);
        mtbdd_refs_pop(1);
    } else {
        dst->bdd = sylvan_relprev(rel->bdd, src->bdd, rel->all_variables);
        dst->bdd = sylvan_and(dst->bdd, univ->bdd);
    }
}

/**
 * Calculate projection of src onto dst
 */
static void
set_project(vset_t dst, vset_t src)
{
    if (dst->state_variables != src->state_variables) {
        dst->bdd = sylvan_project(src->bdd, dst->state_variables);
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
    if (src == dst) {
        Abort("Do not call set_zip with dst == src");
    }

    BDD tmp1 = dst->bdd;
    BDD tmp2 = src->bdd;
    mtbdd_refs_push(tmp1);
    mtbdd_refs_push(tmp2);
    dst->bdd = sylvan_or(tmp1, tmp2);
    src->bdd = sylvan_diff(tmp2, tmp1);
    mtbdd_refs_pop(2);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add_act(vrel_t rel, const int *src, const int *dst, const int *cpy, const int act)
{
    check_state(rel->dom, rel->r_k, rel->r_proj, src);
    check_state(rel->dom, rel->w_k, rel->w_proj, dst);

    // make cube of src
    uint8_t src_cube[rel->r_k * xstatebits];
    state_to_cube(rel->dom, rel->r_k, rel->r_proj, src, src_cube);
    BDD src_bdd = sylvan_cube(rel->state_variables, src_cube);
    mtbdd_refs_push(src_bdd);

    // create the BDD representing the dst+cpy structure
    BDD dst_bdd = sylvan_true;
    {
        // prepare variables...
        uint32_t w_vars[rel->w_k*xstatebits];
        uint32_t curvar = 0;
        int j=0, n=0;
        for (int i=0; i<rel->dom->vectorsize && j<rel->w_k; i++) {
            const int sb = rel->dom->statebits[i];
            if (i == rel->w_proj[j]) {
                for (int m=0; m<sb; m++) {
                    w_vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * sb;
            }
        }
        // go!
        for (int i=rel->w_k-1; i>=0; i--) {
            const int k = rel->w_proj[i];
            const int sb = rel->dom->statebits[k];
            if (cpy && cpy[i]) {
                // take copy of read
                for (int j=sb-1; j>=0; j--) {
                    n--;
                    // FIXME sylvan_makenode is dangerous!! it should run in a Lace thread!!
                    BDD low = sylvan_makenode(w_vars[n]+1, dst_bdd, sylvan_false);
                    mtbdd_refs_push(low);
                    BDD high = sylvan_makenode(w_vars[n]+1, sylvan_false, dst_bdd);
                    mtbdd_refs_pop(1);
                    dst_bdd = sylvan_makenode(w_vars[n], low, high);
                }
            } else {
                // actually write
                for (int j=sb-1; j>=0; j--) {
                    n--;
                    if (dst[i] & (1LL<<(sb-j-1))) dst_bdd = sylvan_makenode(w_vars[n]+1, sylvan_false, dst_bdd);
                    else dst_bdd = sylvan_makenode(w_vars[n]+1, dst_bdd, sylvan_false);
                }
            }
        }
    }
    mtbdd_refs_push(dst_bdd);

    // make cube of action
    const int ab = rel->dom->actionbits;
    uint8_t act_cube[ab];
    for (int i=0; i<ab; i++) {
        act_cube[i] = (act & (1LL<<(ab-i-1))) ? 1 : 0;
    }
    BDD act_bdd = sylvan_cube(rel->dom->action_variables, act_cube);
    mtbdd_refs_push(act_bdd);

    // concatenate dst and act
    BDD dst_and_act = sylvan_and(dst_bdd, act_bdd);
    mtbdd_refs_pop(2);
    mtbdd_refs_push(dst_and_act);

    // concatenate src and dst and act
    BDD src_and_dst_and_act = sylvan_and(src_bdd, dst_and_act);
    mtbdd_refs_pop(2);
    mtbdd_refs_push(src_and_dst_and_act);

    // intersect with cur_is_next
    BDD to_add = sylvan_and(src_and_dst_and_act, rel->cur_is_next);
    mtbdd_refs_pop(1);
    mtbdd_refs_push(to_add);

    // add result to relation
    rel->bdd = sylvan_or(rel->bdd, to_add);
    mtbdd_refs_pop(1);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add_cpy(vrel_t rel, const int *src, const int *dst, const int *cpy)
{
    check_state(rel->dom, rel->r_k, rel->r_proj, src);
    check_state(rel->dom, rel->w_k, rel->w_proj, dst);

    // make cube of src
    uint8_t src_cube[rel->r_k * xstatebits];
    state_to_cube(rel->dom, rel->r_k, rel->r_proj, src, src_cube);
    BDD src_bdd = sylvan_cube(rel->state_variables, src_cube);
    mtbdd_refs_push(src_bdd);

    // create the BDD representing the dst+cpy structure
    BDD dst_bdd = sylvan_true;
    {
        // prepare variables...
        uint32_t w_vars[rel->w_k*xstatebits];
        uint32_t curvar = 0;
        int j=0, n=0;
        for (int i=0; i<rel->dom->vectorsize && j<rel->w_k; i++) {
            const int sb = rel->dom->statebits[i];
            if (i == rel->w_proj[j]) {
                for (int m=0; m<sb; m++) {
                    w_vars[n++] = curvar;
                    curvar += 2;
                }
                j++;
            } else {
                curvar += 2 * sb;
            }
        }
        // go!
        for (int i=rel->w_k-1; i>=0; i--) {
            const int k = rel->w_proj[i];
            const int sb = rel->dom->statebits[k];
            if (cpy && cpy[i]) {
                // take copy of read
                for (int j=sb-1; j>=0; j--) {
                    n--;
                    // FIXME sylvan_makenode is dangerous!! it should run in a Lace thread!!
                    BDD low = sylvan_makenode(w_vars[n]+1, dst_bdd, sylvan_false);
                    mtbdd_refs_push(low);
                    BDD high = sylvan_makenode(w_vars[n]+1, sylvan_false, dst_bdd);
                    mtbdd_refs_pop(1);
                    dst_bdd = sylvan_makenode(w_vars[n], low, high);
                }
            } else {
                // actually write
                for (int j=sb-1; j>=0; j--) {
                    n--;
                    if (dst[i] & (1LL<<(sb-j-1))) dst_bdd = sylvan_makenode(w_vars[n]+1, sylvan_false, dst_bdd);
                    else dst_bdd = sylvan_makenode(w_vars[n]+1, dst_bdd, sylvan_false);
                }
            }
        }
        assert(n == 0);
    }
    mtbdd_refs_push(dst_bdd);

    // concatenate src and dst
    BDD src_and_dst = sylvan_and(src_bdd, dst_bdd);
    mtbdd_refs_pop(2);
    mtbdd_refs_push(src_and_dst);

    // intersect with cur_is_next
    BDD to_add = sylvan_and(src_and_dst, rel->cur_is_next);
    mtbdd_refs_pop(1);
    mtbdd_refs_push(to_add);

    // add result to relation
    rel->bdd = sylvan_or(rel->bdd, to_add);
    mtbdd_refs_pop(1);
}

/**
 * Add (src, dst) to the relation
 */
static void
rel_add(vrel_t rel, const int *src, const int *dst)
{
    return rel_add_cpy(rel, src, dst, 0);
}

/**
 * Context struct for rel_update
 */
struct rel_update_context
{
    vrel_t rel;
    vset_t src;
    size_t vec_len;
    vrel_update_cb cb;
    void* context;
};

/**
 * Callback implementation for rel_update
 */
TASK_2(BDD, bdd_rel_updater, void*, _ctx, uint8_t*, arr)
{
    struct rel_update_context *ctx = (struct rel_update_context*)_ctx;
    const vset_t src = ctx->src;
    int vec[ctx->vec_len];
    state_from_cube(src->dom, src->k, src->proj, vec, arr);

    struct vector_relation dummyrel;
    memcpy(&dummyrel, ctx->rel, sizeof(struct vector_relation));
    dummyrel.bdd = sylvan_false;

    mtbdd_refs_pushptr(&dummyrel.bdd);
    ctx->cb(&dummyrel, ctx->context, (int*)vec);
    mtbdd_refs_popptr(1);

    return dummyrel.bdd;
}

/**
 * Combination of enumeration and set union of the result
 * This implementation is thread-safe; i.e., if dst is changed during operation, reload dst and retry
 */
static void
rel_update(vrel_t dst, vset_t src, vrel_update_cb cb, void* context)
{
    struct rel_update_context ctx = (struct rel_update_context){dst, src, src->k == -1 ? src->dom->vectorsize : src->k, cb, context};
    BDD result = sylvan_collect(src->bdd, src->state_variables, bdd_rel_updater_CALL, (void*)&ctx);

    BDD cur = dst->bdd;
    mtbdd_refs_pushptr(&cur);
    mtbdd_refs_pushptr(&result);
    for (;;) {
        result = sylvan_or(cur, result);
        BDD test = __sync_val_compare_and_swap(&dst->bdd, cur, result);
        if (test == cur) break;
        else cur = test;
    }
    mtbdd_refs_popptr(2);
}

/**
 * Implementation of (sequential) saturation.
 * Assumes: set is of long vectors; relations are ordered on first variable.
 */
TASK_4(BDD, go_sat, BDD, set, vrel_t*, rels, int, count, int, id)
{
    /* Terminal cases */
    if (set == sylvan_false) return sylvan_false;
    if (count == 0) return set;

    /* Consult the cache */
    BDD result;
    BDD _set = set;
    if (cache_get3(200LL<<40, _set, (uint64_t)rels, id, &result)) return result;
    mtbdd_refs_pushptr(&_set);

    /* Check if the relation should be applied */
    uint32_t var = sylvan_var(rels[0]->all_variables);
    if (set == sylvan_true || var <= sylvan_var(set)) {
        /* Count the number of relations starting here */
        int n = 1;
        while (n < count && var == sylvan_var(rels[n]->all_variables)) n++;
        /*
         * Compute until fixpoint:
         * - SAT deeper
         * - learn and chain-apply all current level once
         */
        BDD prev = sylvan_false;
        BDD step = sylvan_false;
        struct vector_set dummy;
        mtbdd_refs_pushptr(&set);
        mtbdd_refs_pushptr(&prev);
        mtbdd_refs_pushptr(&step);
        while (prev != set) {
            prev = set;
            // SAT deeper
            set = RUN(go_sat, set, rels+n, count-n, id);
            // learn and chain-apply all current level once
            for (int i=0;i<n;i++) {
                if (rels[i]->expand != NULL) {
                    // project set
                    dummy.dom = rels[i]->dom;
                    dummy.k = rels[i]->r_k;
                    dummy.proj = rels[i]->r_proj;
                    dummy.state_variables = rels[i]->state_variables;
                    dummy.bdd = sylvan_project(set, dummy.state_variables);
                    // call expand callback
                    mtbdd_refs_pushptr(&dummy.bdd);
                    rels[i]->expand(rels[i], &dummy, rels[i]->expand_ctx);
                    mtbdd_refs_popptr(1);
                }
                // and then step
                step = sylvan_relnext(set, rels[i]->bdd, rels[i]->all_variables);
                set = sylvan_or(set, step);
                step = sylvan_false; // unset, for gc
            }
        }
        mtbdd_refs_popptr(3);
        result = set;
    } else {
        /* Recursive computation */
        BDD low = mtbdd_refs_push(RUN(go_sat, sylvan_low(set), rels, count, id));
        BDD high = mtbdd_refs_push(RUN(go_sat, sylvan_high(set), rels, count, id));
        mtbdd_refs_pop(2);
        result = sylvan_makenode(sylvan_var(set), low, high);
    }
    // Store in cache
    cache_put3(200LL<<40, _set, (uint64_t)rels, id, result);
    mtbdd_refs_popptr(1);
    return result;
}

/**
 * Implementation of (parallel) saturation.
 * Assumes: set is of long vectors; relations are ordered on first variable.
 */
TASK_4(BDD, go_sat_par, BDD, set, vrel_t*, rels, int, count, int, id)
{
    /* Terminal cases */
    if (set == sylvan_false) return sylvan_false;
    if (count == 0) return set;

    /* Consult the cache */
    BDD result;
    BDD _set = set;
    if (cache_get3(200LL<<40, _set, (uint64_t)rels, id, &result)) return result;
    mtbdd_refs_pushptr(&_set);

    /* Check if the relation should be applied */
    uint32_t var = sylvan_var(rels[0]->all_variables);
    if (set == sylvan_true || var <= sylvan_var(set)) {
        /* Count the number of relations starting here */
        int n = 1;
        while (n < count && var == sylvan_var(rels[n]->all_variables)) n++;
        /*
         * Compute until fixpoint:
         * - SAT deeper
         * - learn and chain-apply all current level once
         */
        BDD prev = sylvan_false;
        BDD step = sylvan_false;
        struct vector_set dummy;
        mtbdd_refs_pushptr(&set);
        mtbdd_refs_pushptr(&prev);
        mtbdd_refs_pushptr(&step);
        while (prev != set) {
            prev = set;
            // SAT deeper
            set = CALL(go_sat_par, set, rels+n, count-n, id);
            // learn and chain-apply all current level once
            for (int i=0;i<n;i++) {
                if (rels[i]->expand != NULL) {
                    // project set
                    dummy.dom = rels[i]->dom;
                    dummy.k = rels[i]->r_k;
                    dummy.proj = rels[i]->r_proj;
                    dummy.state_variables = rels[i]->state_variables;
                    dummy.bdd = sylvan_project(set, dummy.state_variables);
                    // call expand callback
                    mtbdd_refs_pushptr(&dummy.bdd);
                    rels[i]->expand(rels[i], &dummy, rels[i]->expand_ctx);
                    mtbdd_refs_popptr(1);
                }
                // and then step
                step = sylvan_relnext(set, rels[i]->bdd, rels[i]->all_variables);
                set = sylvan_or(set, step);
                step = sylvan_false; // unset, for gc
            }
        }
        mtbdd_refs_popptr(3);
        result = set;
    } else {
        /* Recursive computation */
        mtbdd_refs_spawn(SPAWN(go_sat_par, sylvan_low(set), rels, count, id));
        BDD high = mtbdd_refs_push(CALL(go_sat_par, sylvan_high(set), rels, count, id));
        BDD low = mtbdd_refs_sync(SYNC(go_sat_par));
        mtbdd_refs_pop(1);
        result = sylvan_makenode(sylvan_var(set), low, high);
    }
    // Store in cache
    cache_put3(200LL<<40, _set, (uint64_t)rels, id, result);
    mtbdd_refs_popptr(1);
    return result;
}

static int
init_least_fixpoint(vrel_t rels[], vrel_t _rels[], int rel_count)
{
    memcpy(rels, _rels, sizeof(vrel_t[rel_count]));

    // Sort the rels (using gnome sort)
    int i = 1, j = 2;
    vrel_t t;
    while (i < rel_count) {
        vrel_t *p = rels+i, *q = p-1;
        if (sylvan_var((*q)->all_variables) > sylvan_var((*p)->all_variables)) {
            t = *q;
            *q = *p;
            *p = t;
            if (--i) continue;
        }
        i = j++;
    }

    // Get next id (for cache)
    static volatile int id = 0;
    return __sync_fetch_and_add(&id, 1);
}

static void
set_least_fixpoint(vset_t dst, vset_t src, vrel_t _rels[], int rel_count)
{
    // Create copy of rels
    vrel_t rels[rel_count];

    int id = init_least_fixpoint(rels, _rels, rel_count);

    // Go!
    dst->bdd = RUN(go_sat, src->bdd, rels, rel_count, id);
}

static void
set_least_fixpoint_par(vset_t dst, vset_t src, vrel_t _rels[], int rel_count)
{
    // Create copy of rels
    vrel_t rels[rel_count];

    int id = init_least_fixpoint(rels, _rels, rel_count);

    // Go!
    dst->bdd = RUN(go_sat_par, src->bdd, rels, rel_count, id);
}

/**
 * ReachDD (Alg. 4): one recursive operation over the whole list of local
 * relations, splitting each relation at every level of its span instead of
 * applying it whole at its entry level, as saturation does.  The operation
 * itself is in vset_sylvan_reachdd.c; all this does is hand it Def. 1's
 * triples <R_rho, Rd_rho, W_rho>.
 *
 * ReachDD needs the projections as *state bit* level sets, where LTSmin
 * declares them as state-vector slots, so r_proj/w_proj are expanded here
 * using dom->statebits.  Slot i owns the contiguous bit range [base[i],
 * base[i+1]), and level b has variables 2b (unprimed) and 2b+1 (primed), as
 * dom_create numbers them.
 *
 * Dom_rho = Rd_rho union W_rho is exactly LTSmin's a_proj, which is what
 * rel->all_variables is built from, so the cube handed to sylvan_relnext for a
 * residual is a suffix of that existing set.  Rd_rho is not passed separately:
 * the operation needs it only through Dom (see vset_sylvan_reachdd.h).
 *
 * Expand callbacks ARE honoured, through rl_learn_cb below: ReachDD expands a
 * relation at its entry level, which is where its whole read projection is
 * still available and where saturation expands it too.  --reachdd-no-learn
 * suppresses that and leaves the learning to the caller's own fixpoint.
 */

/**
 * The rl_relspec_t::learn callback: expand one relation over the read
 * projection of S, and hand ReachDD the relation's new diagram.  This is
 * go_sat's expand block (above), and it is here rather than in
 * vset_sylvan_reachdd.c because it is the only place that may know what a
 * struct vector_relation looks like.
 *
 * S is rooted by ReachDD (it is the node the call is about to return);
 * dummy.bdd is rooted across the callback, which allocates.  vset_minus inside
 * expand_group_next_projected() mutates dummy.bdd, which is why the projection
 * is a fresh stack set rather than the relation's own.
 */
static BDD
rl_learn_cb(void *self, BDD S)
{
    vrel_t rel = (vrel_t)self;
    struct vector_set dummy;
    dummy.dom             = rel->dom;
    dummy.k               = rel->r_k;
    dummy.proj            = rel->r_proj;
    dummy.state_variables = rel->state_variables;
    dummy.bdd             = sylvan_project(S, dummy.state_variables);
    mtbdd_refs_pushptr(&dummy.bdd);
    rel->expand(rel, &dummy, rel->expand_ctx);
    mtbdd_refs_popptr(1);
    return rel->bdd;
}

/**
 * The body of both ReachDD entry points.  par selects Alg. 5's loop schedule
 * over Alg. 4's; everything else -- the projections, the learn callbacks, the
 * safety valve, the context -- is identical, because the two schedules differ
 * only in the order of one loop body's statements.
 */
static void
reachdd_run(vset_t dst, vset_t src, vrel_t rels[], int rel_count, int par)
{
    vdom_t dom = src->dom;
    const int n = dom->totalbits;
    /* Must match rl_ctx_create's RL_WORDS(n < 1 ? 1 : n), or a zero-bit domain
     * sizes the bitsets here at 0 words and is read at 1. */
    const int words = (n < 1) ? 1 : (n + 63) / 64;

    /* Def. 2 needs the action variables ordered below every state variable;
     * dom_create puts them at 1000000+j. */
    if (2 * n + 1 >= 1000000) Abort("reachdd: state variables overlap the action variables");

    int base[dom->vectorsize + 1];
    base[0] = 0;
    for (int i = 0; i < dom->vectorsize; i++) base[i+1] = base[i] + dom->statebits[i];

    const int nrel = rel_count > 0 ? rel_count : 1;
    uint64_t *bits = (uint64_t*)RTmallocZero(sizeof(uint64_t[2 * words * nrel]));
    rl_relspec_t *specs = (rl_relspec_t*)RTmalloc(sizeof(rl_relspec_t[nrel]));

    for (int m = 0; m < rel_count; m++) {
        uint64_t *dombits = bits + (size_t)(2*m) * words;
        uint64_t *wbits   = bits + (size_t)(2*m+1) * words;
        for (int j = 0; j < rels[m]->r_k; j++) {
            const int i = rels[m]->r_proj[j];
            for (int b = base[i]; b < base[i+1]; b++) dombits[b>>6] |= 1ULL << (b&63);
        }
        for (int j = 0; j < rels[m]->w_k; j++) {
            const int i = rels[m]->w_proj[j];
            for (int b = base[i]; b < base[i+1]; b++) {
                dombits[b>>6] |= 1ULL << (b&63);
                wbits[b>>6]   |= 1ULL << (b&63);
            }
        }
        specs[m].bdd           = rels[m]->bdd;
        specs[m].all_variables = rels[m]->all_variables;
        specs[m].dombits       = dombits;
        specs[m].wbits         = wbits;
        if (reachdd_learn && rels[m]->expand != NULL) {
            specs[m].learn      = rl_learn_cb;
            specs[m].learn_self = rels[m];
        } else {
            specs[m].learn      = NULL;
            specs[m].learn_self = NULL;
        }
    }

    rl_gc_hooks_install();
    rl_set_pin_all(reachdd_pin_all);

    rl_ctx_t *ctx = rl_ctx_create(n, specs, rel_count);
    /* ctx == NULL means Reduce emptied the list at the root: every relation is
     * zero or writes nothing, so [[L]] is empty and S.[[L]]* = S. */
    const uint64_t cells_before = rl_cells_created();
    const uint64_t calls_before = rl_calls_made();
    const uint64_t hits_before  = rl_cache_hits();
    const uint64_t waive_before = rl_cube_waivers();
    dst->bdd = (ctx == NULL) ? src->bdd
             : (par ? rl_run_par(ctx, src->bdd) : rl_run(ctx, src->bdd));
    Warning(infoLong, "ReachDD: %s schedule, %d relations over %d levels, "
                      "%u interned residual "
                      "projections, %"PRIu64" residual cells consed, "
                      "%"PRIu64" calls of which %"PRIu64" cache hits, "
                      "%"PRIu64" unexpanded full cubes (Alg. 4 line 5)",
            par ? "parallel (Alg. 5)" : "sequential (Alg. 4)",
            rl_ctx_relations(ctx), n, rl_ctx_projections(ctx),
            rl_cells_created() - cells_before,
            rl_calls_made() - calls_before,
            rl_cache_hits() - hits_before,
            rl_cube_waivers() - waive_before);
    rl_ctx_destroy(ctx);

    RTfree(specs);
    RTfree(bits);
}

static void
set_reach_dd(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    reachdd_run(dst, src, rels, rel_count, 0);
}

static int
reach_dd_supported()
{
    return 1;
}

/**
 * ReachDDPar (Alg. 5): the same operation with the loop body of Alg. 4
 * reordered so that the two recursions, then the two cross ImageAlls, then the
 * two unions each run as a spawn/call/sync pair, and each ImageAll is a
 * balanced reduction over its writer list.  It computes the same fixpoint (see
 * vset_sylvan_reachdd.c); only the number of rounds can differ.
 *
 * The sequential schedule stays reachable on three routes, and all three are
 * deliberate.
 *
 * (1) --reachdd-pin-all.  Cor. th:sat-special is an ordering claim as much as
 *     a scheduling one: a pinned relation is applied whole, at its entry
 *     level, in sequence, which is exactly what saturation does.  That switch
 *     exists to assert precisely this, so it must not run under a reordered
 *     loop -- and the operation additionally forces Alg. 4's body at any level
 *     with pinned relations entering, because the GC safety valve can turn
 *     pinning on in the middle of a run.
 *
 * (2) One worker.  SPAWN/SYNC then degenerate to inline execution, which is
 *     correct but pays a deque push, a fence and a leapfrog test per level for
 *     nothing.
 *
 * (3) MT-unsafe front ends.  pins2lts-sym calls this entry point only when
 *     USE_PARALLELISM is set, i.e. for a SPEC_MT_SAFE language module.  Under
 *     learning ReachDD does invoke expand from inside the operation, on
 *     several workers at once, exactly as parallel saturation's go_sat_par
 *     does, so honouring that flag matters.
 *
 * And a deterministic single-threaded reference run is the first thing one
 * wants when a state count comes out wrong.
 */
static void
set_reach_dd_par(vset_t dst, vset_t src, vrel_t rels[], int rel_count)
{
    reachdd_run(dst, src, rels, rel_count,
                lace_workers() > 1 && !reachdd_pin_all);
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
set_save(FILE* f, vset_t set)
{
    fwrite(&set->k, sizeof(int), 1, f);
    if (set->k != -1) fwrite(set->proj, sizeof(int), set->k, f);
    mtbdd_writer_tobinary(f, &set->bdd, 1);
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
    mtbdd_writer_tobinary(f, &rel->bdd, 1);
    // TODO also write all_variables...
}

/* LOADING */
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
    
    if (mtbdd_reader_frombinary(f, &set->bdd, 1) != 0) Abort("Invalid file format.");

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
    if (mtbdd_reader_frombinary(f, &rel->bdd, 1) != 0) Abort("Invalid file format.");
}

static void
dom_save(FILE* f, vdom_t dom)
{
    fwrite(&dom->vectorsize, sizeof(int), 1, f);
    fwrite(dom->statebits, sizeof(int), dom->vectorsize, f);
    fwrite(&dom->actionbits, sizeof(int), 1, f);
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
    dom->shared.set_update=set_update;
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
    dom->shared.rel_update=rel_update;
    dom->shared.rel_count=rel_count;
    dom->shared.set_next=set_next;
    dom->shared.set_prev=set_prev;
    dom->shared.set_least_fixpoint=set_least_fixpoint;
    dom->shared.set_least_fixpoint_par=set_least_fixpoint_par;
    dom->shared.set_reach_dd=set_reach_dd;
    dom->shared.set_reach_dd_par=set_reach_dd_par;
    dom->shared.supports_reach_dd=reach_dd_supported;

    dom->shared.reorder=set_reorder;
    dom->shared.set_dot=set_dot;
    dom->shared.rel_dot=rel_dot;

    // no pre_load or pre_save
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

    char buf[40];
    to_h((1ULL<<maxtablesize)*24+(1ULL<<maxcachesize)*36, buf);
    Warning(info, "Sylvan allocates %s virtual memory for nodes table and operation cache.", buf);
    to_h((1ULL<<tablesize)*24+(1ULL<<cachesize)*36, buf);
    Warning(info, "Initial nodes table and operation cache requires %s.", buf);

    // Call initializator of library (if needed)
    sylvan_set_sizes(1LL<<tablesize, 1LL<<maxtablesize, 1LL<<cachesize, 1LL<<maxcachesize);
    sylvan_init_package();
    sylvan_set_granularity(granularity);
    sylvan_gc_hook_pregc(gc_start_CALL);
    sylvan_gc_hook_postgc(gc_end_CALL);
}

/**
 * Create a domain with object size n
 */
static vdom_t
dom_create(int vectorsize, int *_statebits, int actionbits)
{
    Warning(info, "Creating a Sylvan domain.");

    ltsmin_initialize_sylvan();
    sylvan_init_mtbdd();

    // Create data structure of domain
    vdom_t dom = (vdom_t)RTmalloc(sizeof(struct vector_domain));
    vdom_init_shared(dom, vectorsize);
    dom_set_function_pointers(dom);

    // Initialize domain
    dom->vectorsize = vectorsize;
    dom->statebits = (int*)RTmalloc(sizeof(int[vectorsize]));
    memcpy(dom->statebits, _statebits, sizeof(int[vectorsize]));
    dom->actionbits = actionbits;

    dom->totalbits = 0;
    for (int i=0; i<vectorsize; i++) dom->totalbits += _statebits[i];

    // Create state_variables and prime_variables
    uint32_t curvar = 0, k=0;
    BDDVAR state_vars[xstatebits * vectorsize];
    BDDVAR prime_vars[xstatebits * vectorsize];
    for (int i=0; i<vectorsize; i++) {
        for (int j=0; j<_statebits[i]; j++) {
            state_vars[k] = curvar++;
            prime_vars[k] = curvar++;
            k++;
        }
    }

    dom->state_variables = sylvan_set_fromarray(state_vars, k);
    sylvan_protect(&dom->state_variables);
    dom->prime_variables = sylvan_set_fromarray(prime_vars, k);
    sylvan_protect(&dom->prime_variables);

    // Create action_variables
    BDDVAR action_vars[actionbits];
    for (int i=0; i<actionbits; i++) action_vars[i] = 1000000+i;
    dom->action_variables = sylvan_set_fromarray(action_vars, actionbits);
    sylvan_protect(&dom->action_variables);

    return dom;
}

vdom_t
vdom_create_sylvan(int n)
{
    int _statebits[n];
    for (int i=0; i<n; i++) _statebits[i] = xstatebits;
    return dom_create(n, _statebits, xactionbits);
}

vdom_t
vdom_create_sylvan_from_file(FILE *f)
{
    int vectorsize;
    if (fread(&vectorsize, sizeof(int), 1, f) != 1) Abort("invalid file format");
    int statebits[vectorsize];
    if (fread(statebits, sizeof(int), vectorsize, f) != (size_t)vectorsize) Abort("invalid file format");
    int actionbits;
    if (fread(&actionbits, sizeof(int), 1, f) != 1) Abort("invalid file format");
    return dom_create(vectorsize, statebits, actionbits);
}
