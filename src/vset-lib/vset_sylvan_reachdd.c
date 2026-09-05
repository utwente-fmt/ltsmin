/**
 * \file vset_sylvan_reachdd.c
 * \brief ReachDD (Alg. 4) for the Sylvan BDD back-end.
 *
 * ReachDD(S, L, l) computes S.[[L]]^* from a *list* L of local relations
 * (Def. 1), by descending the state diagram and all k relations in lockstep
 * and splitting every relation at every level of its span into the block
 * matrix its own projections dictate (Def. 2 / Eq. 7).  Saturation, by
 * contrast, applies a whole relation with one image at its entry level;
 * Cor. th:sat-special says the two are settings of one operation, and
 * rl_set_pin_all() selects saturation's.
 *
 * The transcription of Alg. 4 is rl_reach() below, with the paper's line
 * numbers in the comments.
 *
 * A residual is a POSITION, not a modified relation.  Eq. 7's diagram
 * component R_rho[ij] is a node that already exists inside R_rho, reached by
 * pointer chasing from its root, or the constant 0; its projection component
 * is just the integer sets Dom_rho\{l} and W_rho\{l}.  No relation diagram is
 * ever rebuilt, no conjunction is ever formed and no relations are ever
 * merged.  What *is* allocated is the residual list, the synchronisation
 * vector of the k relations, and it must be content-addressed: identifying a
 * residual by the relation it came from makes the operation exponentially
 * worse than merged ReachBdd on k local guards writing one shared flag
 * (Sec. 4.4).
 *
 * Representation (App. B.6).  The residual projections <Dom cap [l,n),
 * W cap [l,n)> are interned up front into a dense table of at most
 * 1 + sum_m |Dom_m| entries; the hot path then needs only three integers per
 * id (dmin, wmin, succ) plus one variable cube derived from the relation's own
 * all_variables.  A residual list is a hash-consed chain of Sylvan nodes in a
 * reserved variable range (var = RL_CELLVAR_BASE + projection id, low = tail,
 * high = residual diagram), built with _mtbdd_makenode so nothing folds,
 * canonical because Sylvan's llmsset is a hash-cons, and collected by Sylvan's
 * own mtbdd_gc_mark_rec for free.  The relation index is dropped, so residuals
 * equal as relations are equal as cells.  The operation cache key is
 * <S, chain, l>, and l must be in it: relations not yet entered are
 * transparent and are not in the chain, so the chain represents L *minus* the
 * not-yet-entered suffix and l is that suffix's identity.
 */

#ifndef RL_NO_HRE            /* the standalone unit test supplies its own shims */
#include <hre/config.h>
#include <hre/user.h>
#endif

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sylvan.h>
#include <sylvan_int.h>

#include <vset-lib/vset_sylvan_reachdd.h>

/**
 * Knobs.  Both terminals below are optimisations, not correctness
 * requirements, and both are the fastest way to localise a wrong answer.
 */
#ifndef RL_HAVOC_TERMINAL
#define RL_HAVOC_TERMINAL 1   /* Alg. 4 line 6                                */
#endif
#ifndef RL_ADVANCE
#define RL_ADVANCE 1          /* App. B.6 level skipping; 0 = walk every level */
#endif

/* Operation cache identifiers.  Sylvan itself uses 0..93, LTSmin uses 200
 * (vset_sylvan.c saturation) and 201 (vset_lddmc.c saturation). */
#define RL_OPID (210LL<<40)

/**
 * ------------------------------------------------------------------------
 * Level bit sets
 * ------------------------------------------------------------------------
 */
#define RL_WORDS(n) (((n) + 63) / 64)

static inline int
rl_tst(const uint64_t *b, int i)
{
    return (b[i >> 6] >> (i & 63)) & 1;
}

static inline void
rl_setbit(uint64_t *b, int i)
{
    b[i >> 6] |= 1ULL << (i & 63);
}

/**
 * ------------------------------------------------------------------------
 * Residual list cells (App. B.6)
 * ------------------------------------------------------------------------
 *
 * A cell is an ordinary MTBDD node made with _mtbdd_makenode (NOT
 * mtbdd_makenode, which folds when low == high) in a reserved variable range
 * above every state, prime and action variable.  Sylvan's unique table is a
 * hash-cons and _mtbdd_makenode's complement normalisation is injective, so
 * equal (pid, R, tail) gives the identical 64-bit handle; by induction from
 * the empty list, which is sylvan_false and therefore unmarked, no chain
 * handle ever carries a complement mark, so getlow/gethigh are exact.
 */
#define RL_CELLVAR_BASE 0x800000u   /* 2^23: Sylvan's variable field is 24 bits */
#define RL_MAX_PIDS     0x800000u
#define RL_EPS          sylvan_false

static inline MTBDD
rl_cons(uint32_t pid, MTBDD R, MTBDD tail)
{
    return _mtbdd_makenode(RL_CELLVAR_BASE + pid, tail, R);
}

static inline uint32_t rl_pid (MTBDD c) { return mtbdd_getvar(c) - RL_CELLVAR_BASE; }
static inline MTBDD    rl_res (MTBDD c) { return mtbdd_gethigh(c); }
static inline MTBDD    rl_tail(MTBDD c) { return mtbdd_getlow(c); }

static inline int
rl_len(MTBDD c)
{
    int m = 0;
    for (; c != RL_EPS; c = rl_tail(c)) m++;
    return m;
}

/**
 * ------------------------------------------------------------------------
 * Interned residual projections
 * ------------------------------------------------------------------------
 *
 * Interned is the pair <Dom cap [l,n), W cap [l,n)>, not <Rd, W>: the
 * algorithm needs Rd only through Dom (Eq. 7 tests W; retirement tests W; the
 * havoc terminal quantifies W; the image of a read-closed relation quantifies
 * Dom by Lemma th:image), and Dom is the coarser equivalence, so it shares
 * more.  Every element of an interned pair is >= l, which is what collapses
 * the per-level set tests to integer comparisons:
 *
 *   l in W(p)   <=>  wmin(p) == l
 *   l in Dom(p) <=>  dmin(p) == l
 *   successor   =    succ(p) if dmin(p) == l else p
 *   retired     <=>  wmin(p) == RL_INF
 */
#define RL_INF  INT32_MAX
#define RL_DEAD 0u          /* the empty pair <emptyset, emptyset>            */

typedef struct {
    int32_t  dmin;          /* min Dom(p), or RL_INF                          */
    int32_t  wmin;          /* min W(p),   or RL_INF  (RL_INF <=> retired)    */
    uint32_t succ;          /* id of <Dom\{dmin}, W\{dmin}>                   */
    MTBDD    imgvars;       /* {2i,2i+1 | i in Dom(p)}: relnext's vars        */
    uint32_t repr_m;        /* representative relation, for the intern table  */
    int32_t  repr_l;        /* representative level,    for the intern table  */
} rl_proj_t;

typedef struct {
    BDD       bdd;
    BDD       all_variables;
    uint64_t *dombits;
    uint64_t *wbits;
} rl_rel_t;

struct rl_ctx {
    int         n;              /* number of levels = dom->totalbits          */
    int         k;              /* number of relations after Reduce           */
    int         words;          /* words per level bit set                    */
    rl_rel_t   *rel;            /* k relations, ascending by entry level      */
    int        *entry;          /* entry[m] = min Dom(rel m)                  */
    uint32_t   *entry_pid;      /* projection id of rel m at its entry level  */
    int        *first_at;       /* first_at[l] = first m with entry[m] >= l   */
    rl_proj_t  *pj;
    uint32_t    npids;
    uint32_t    maxpids;
    uint32_t   *itab;           /* intern table, open addressing, stores p+1  */
    size_t      itab_size;
    uint32_t    id;             /* invocation id, third word of the cache key */
};

/**
 * One entry of a residual list under construction: the cell content, with the
 * relation index dropped (App. B.6).
 */
typedef struct { uint32_t pid; BDD R; } rl_entry_t;

/**
 * ------------------------------------------------------------------------
 * The safety valve (App. B.6)
 * ------------------------------------------------------------------------
 */
static volatile int      rl_pin_all      = 0;   /* apply-whole schedule       */
static volatile int      rl_pin_forced   = 0;   /* set by rl_set_pin_all()    */
static volatile int      rl_active       = 0;   /* only measure while running */
static volatile int      rl_starved      = 0;
static volatile size_t   rl_gc_occupied  = 0;
static volatile uint64_t rl_cells_global = 0;

static inline int
rl_pinning(void)
{
    return rl_pin_all || rl_pin_forced;
}

void
rl_set_pin_all(int pin)
{
    rl_pin_forced = pin ? 1 : 0;
}

uint64_t
rl_cells_created(void)
{
    return rl_cells_global;
}

uint32_t
rl_ctx_projections(const rl_ctx_t *ctx)
{
    return ctx == NULL ? 0 : ctx->npids;
}

int
rl_ctx_relations(const rl_ctx_t *ctx)
{
    return ctx == NULL ? 0 : ctx->k;
}

VOID_TASK_0(rl_pregc)
{
    if (!rl_active) return;
    /* llmsset_count_marked is O(table size) and the counts are only used at the
     * maximum table size, where the valve can fire; below it a collection that
     * recovers little is answered by growing the table, not by us. */
    if (llmsset_get_size(nodes) != llmsset_get_max_size(nodes)) {
        rl_gc_occupied = 0;
        return;
    }
    /* At pregc, llmsset's second bitmap still says "bucket contains data". */
    rl_gc_occupied = CALL(llmsset_count_marked, nodes);
}

VOID_TASK_0(rl_postgc)
{
    if (!rl_active) return;
    int at_max = llmsset_get_size(nodes) == llmsset_get_max_size(nodes);
    /* rl_gc_occupied == 0 means pregc did not measure: either we were not at the
     * maximum size, or the table grew during this collection. Either way there
     * is nothing to judge starvation on. */
    if (!at_max || rl_gc_occupied == 0) {
        rl_starved = 0;
        return;
    }
    /* At postgc it says "survived the collection". */
    size_t before = rl_gc_occupied;
    size_t after  = CALL(llmsset_count_marked, nodes);
    size_t freed  = before > after ? before - after : 0;

    if (freed * 8 < before) {
        if (++rl_starved >= 2 && !rl_pin_all) {
            rl_pin_all = 1;
#ifndef RL_NO_HRE
            Warning(info, "vset_sylvan/reachdd: collection recovered %zu of %zu nodes "
                          "at the maximum table size (about %"PRIu64" residual cells consed); "
                          "pinning all relations to the apply-whole (saturation) schedule "
                          "for the rest of this run.",
                    freed, before, rl_cells_global);
#endif
        }
    } else {
        rl_starved = 0;
    }
}

void
rl_gc_hooks_install(void)
{
    static int installed = 0;
    if (installed) return;
    installed = 1;
    sylvan_gc_hook_pregc(rl_pregc_CALL);
    sylvan_gc_hook_postgc(rl_postgc_CALL);
}

/**
 * ------------------------------------------------------------------------
 * Building the context: interning the residual projections
 * ------------------------------------------------------------------------
 */

/**
 * The suffix of a Sylvan variable cube from firstvar on.  A variable set is a
 * chain terminated by mtbdd_true whose "next" is gethigh, so a suffix is
 * itself a valid cube AND an existing node: deriving relnext's vars argument
 * for a residual costs no allocation.  Two ids denoting the same restriction
 * of Dom get the same handle by Sylvan's own canonicity, even when they are
 * suffixes of different relations' cubes.
 */
static MTBDD
rl_suffix_cube(MTBDD cube, uint32_t firstvar)
{
    while (!sylvan_set_isempty(cube) && sylvan_set_first(cube) < firstvar) {
        cube = sylvan_set_next(cube);
    }
    return cube;
}

/**
 * Hash and compare the *derived bit sets*, never the (relation, level) pair
 * that indexes them: interning by relation reintroduces Sec. 4.4's 2^k
 * blow-up in miniature, with no symptom other than slowness.
 */
static uint64_t
rl_pairhash(const rl_ctx_t *ctx, uint32_t m, int l)
{
    uint64_t h = 14695981039346656037ULL;
    const uint64_t *d = ctx->rel[m].dombits;
    const uint64_t *w = ctx->rel[m].wbits;
    const int w0 = l >> 6;
    for (int i = 0; i < ctx->words; i++) {
        uint64_t msk = (i > w0) ? ~0ULL : (i < w0 ? 0ULL : (~0ULL << (l & 63)));
        h = (h ^ (d[i] & msk)) * 1099511628211ULL;
        h = (h ^ (w[i] & msk)) * 1099511628211ULL;
    }
    return h;
}

static int
rl_paireq(const rl_ctx_t *ctx, uint32_t m1, int l1, uint32_t m2, int l2)
{
    const uint64_t *d1 = ctx->rel[m1].dombits, *w1 = ctx->rel[m1].wbits;
    const uint64_t *d2 = ctx->rel[m2].dombits, *w2 = ctx->rel[m2].wbits;
    int lo = l1 < l2 ? l1 : l2;
    for (int i = lo >> 6; i < ctx->words; i++) {
        uint64_t m_1 = (i == (l1 >> 6)) ? (~0ULL << (l1 & 63)) : (i > (l1 >> 6) ? ~0ULL : 0ULL);
        uint64_t m_2 = (i == (l2 >> 6)) ? (~0ULL << (l2 & 63)) : (i > (l2 >> 6) ? ~0ULL : 0ULL);
        if ((d1[i] & m_1) != (d2[i] & m_2)) return 0;
        if ((w1[i] & m_1) != (w2[i] & m_2)) return 0;
    }
    return 1;
}

static uint32_t
rl_intern(rl_ctx_t *ctx, uint32_t m, int l, int *is_new)
{
    size_t mask = ctx->itab_size - 1;
    size_t i = (size_t)rl_pairhash(ctx, m, l) & mask;
    for (;;) {
        uint32_t v = ctx->itab[i];
        if (v == 0) {
            if (ctx->npids >= ctx->maxpids || ctx->npids >= RL_MAX_PIDS) {
                Abort("reachdd: %u residual projections exceed the reserved range; "
                      "use --saturation=sat", ctx->npids);
            }
            uint32_t p = ctx->npids++;
            ctx->itab[i] = p + 1;
            ctx->pj[p].repr_m = m;
            ctx->pj[p].repr_l = l;
            *is_new = 1;
            return p;
        }
        uint32_t p = v - 1;
        if (rl_paireq(ctx, m, l, ctx->pj[p].repr_m, ctx->pj[p].repr_l)) {
            *is_new = 0;
            return p;
        }
        i = (i + 1) & mask;
    }
}

static size_t
rl_pow2ge(size_t x)
{
    size_t s = 16;
    while (s < x) s <<= 1;
    return s;
}

rl_ctx_t *
rl_ctx_create(int n, const rl_relspec_t *specs, int k)
{
    const int words = RL_WORDS(n < 1 ? 1 : n);

    if (2u * (unsigned)n + 1u >= RL_CELLVAR_BASE) {
        Abort("reachdd: state vector of %d bits is too wide for the reserved "
              "cell variable range; use --saturation=sat", n);
    }

    rl_ctx_t *ctx = (rl_ctx_t*)RTmalloc(sizeof(rl_ctx_t));
    memset(ctx, 0, sizeof(rl_ctx_t));
    ctx->n     = n;
    ctx->words = words;

    /* Alg. 4 line 3 (Reduce) at the root: drop R_rho = 0 and W_rho = empty.
     * A relation that writes nothing is the identity on everything below it
     * (Eq. 4) and contributes no transition at all. */
    ctx->rel       = (rl_rel_t*) RTmalloc(sizeof(rl_rel_t)  * (k > 0 ? k : 1));
    ctx->entry     = (int*)      RTmalloc(sizeof(int)       * (k > 0 ? k : 1));
    ctx->entry_pid = (uint32_t*) RTmalloc(sizeof(uint32_t)  * (k > 0 ? k : 1));

    size_t domtotal = 0;
    int kept = 0;
    for (int m = 0; m < k; m++) {
        if (specs[m].bdd == sylvan_false) continue;
        int any_w = 0;
        for (int i = 0; i < words; i++) {
            if ((specs[m].wbits[i] & ~specs[m].dombits[i]) != 0) {
                Abort("reachdd: relation %d writes a level outside its domain", m);
            }
            if (specs[m].wbits[i] != 0) any_w = 1;
        }
        if (!any_w) continue;              /* Reduce: drop W_rho = emptyset    */
        ctx->rel[kept].bdd           = specs[m].bdd;
        ctx->rel[kept].all_variables = specs[m].all_variables;
        ctx->rel[kept].dombits = (uint64_t*)RTmalloc(sizeof(uint64_t) * words);
        ctx->rel[kept].wbits   = (uint64_t*)RTmalloc(sizeof(uint64_t) * words);
        memcpy(ctx->rel[kept].dombits, specs[m].dombits, sizeof(uint64_t) * words);
        memcpy(ctx->rel[kept].wbits,   specs[m].wbits,   sizeof(uint64_t) * words);
        for (int i = 0; i < n; i++) if (rl_tst(ctx->rel[kept].dombits, i)) domtotal++;
        kept++;
    }
    ctx->k = kept;

    if (kept == 0) {
        rl_ctx_destroy(ctx);
        return NULL;
    }

    /* At most 1 + sum_m |Dom_m| distinct pairs (App. B.6's k(n+1) bound). */
    ctx->maxpids   = (uint32_t)(domtotal + 1);
    ctx->pj        = (rl_proj_t*)RTmalloc(sizeof(rl_proj_t) * ctx->maxpids);
    ctx->itab_size = rl_pow2ge(2 * (domtotal + 1));
    ctx->itab      = (uint32_t*)RTmalloc(sizeof(uint32_t) * ctx->itab_size);
    memset(ctx->itab, 0, sizeof(uint32_t) * ctx->itab_size);

    /* RL_DEAD is the empty pair; it is reserved and never interned, so it can
     * never be confused with a pair interned at a level of its own domain. */
    ctx->npids = 1;
    ctx->pj[RL_DEAD].dmin    = RL_INF;
    ctx->pj[RL_DEAD].wmin    = RL_INF;
    ctx->pj[RL_DEAD].succ    = RL_DEAD;
    ctx->pj[RL_DEAD].imgvars = sylvan_set_empty();
    ctx->pj[RL_DEAD].repr_m  = 0;
    ctx->pj[RL_DEAD].repr_l  = n;

    /* Sort ascending on entry level, so that the relations not yet reached at
     * level l are exactly the suffix beyond first_at[l] -- which is precisely
     * saturation's own representation (App. B.6).  The entry level is a state
     * level read off the projections (min Dom), never the top variable of the
     * relation diagram, which is legitimately a *primed* one when a relation's
     * first level is written but not read.  Sorting happens before interning,
     * so that a projection's representative row does not move afterwards. */
    for (int m = 0; m < kept; m++) {
        int e = n;
        for (int d = 0; d < n; d++) if (rl_tst(ctx->rel[m].dombits, d)) { e = d; break; }
        /* W non-empty and W subset of Dom, so Dom is non-empty. */
        if (e == n) Abort("reachdd: relation %d has an empty domain", m);
        ctx->entry[m] = e;
    }
    for (int i = 1; i < kept; i++) {
        rl_rel_t r = ctx->rel[i];
        int      e = ctx->entry[i];
        int j = i - 1;
        while (j >= 0 && ctx->entry[j] > e) {
            ctx->rel[j+1]   = ctx->rel[j];
            ctx->entry[j+1] = ctx->entry[j];
            j--;
        }
        ctx->rel[j+1] = r;
        ctx->entry[j+1] = e;
    }

    /* Keep the relation diagrams and their variable cubes rooted for the
     * lifetime of the context.  They are already protected by the vrel_t they
     * came from; making it explicit here means the operation does not depend
     * on that. */
    for (int m = 0; m < kept; m++) {
        sylvan_protect(&ctx->rel[m].bdd);
        sylvan_protect(&ctx->rel[m].all_variables);
    }

    /* Intern each relation's residual projections, deepest suffix first, so
     * that succ is already interned when the pair above it is created.  The
     * chain is NOT truncated at max W: a cross residual is kept even after
     * retirement (App. B: <1,{},{}> is a transition, not the identity), and
     * its image must still quantify the reads below.  dmin, wmin, succ and
     * imgvars are all functions of the pair, which is what the assertion in
     * the already-interned branch checks. */
    for (int m = 0; m < kept; m++) {
        uint32_t cur = RL_DEAD;
        for (int d = n - 1; d >= 0; d--) {
            if (!rl_tst(ctx->rel[m].dombits, d)) continue;
            int is_new;
            uint32_t p = rl_intern(ctx, (uint32_t)m, d, &is_new);
            if (is_new) {
                ctx->pj[p].dmin    = d;
                ctx->pj[p].wmin    = rl_tst(ctx->rel[m].wbits, d) ? d : ctx->pj[cur].wmin;
                ctx->pj[p].succ    = cur;
                ctx->pj[p].imgvars = rl_suffix_cube(ctx->rel[m].all_variables,
                                                    2u * (uint32_t)d);
            } else {
                assert(ctx->pj[p].dmin == d);
                assert(ctx->pj[p].succ == cur);
                assert(ctx->pj[p].wmin ==
                       (rl_tst(ctx->rel[m].wbits, d) ? d : ctx->pj[cur].wmin));
                assert(ctx->pj[p].imgvars ==
                       rl_suffix_cube(ctx->rel[m].all_variables, 2u * (uint32_t)d));
            }
            cur = p;
        }
        assert(cur != RL_DEAD);
        ctx->entry_pid[m] = cur;
        assert(ctx->entry[m] == ctx->pj[cur].dmin);
    }

    /* first_at[l] = index of the first relation with entry >= l. */
    ctx->first_at = (int*)RTmalloc(sizeof(int) * (n + 2));
    {
        int m = 0;
        for (int l = 0; l <= n + 1; l++) {
            while (m < kept && ctx->entry[m] < l) m++;
            ctx->first_at[l] = m;
        }
    }

    static volatile uint32_t rl_next_id = 0;
    ctx->id = __sync_fetch_and_add(&rl_next_id, 1u);

    return ctx;
}

void
rl_ctx_destroy(rl_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->rel != NULL) {
        for (int m = 0; m < ctx->k; m++) {
            sylvan_unprotect(&ctx->rel[m].bdd);
            sylvan_unprotect(&ctx->rel[m].all_variables);
            RTfree(ctx->rel[m].dombits);
            RTfree(ctx->rel[m].wbits);
        }
        RTfree(ctx->rel);
    }
    if (ctx->entry     != NULL) RTfree(ctx->entry);
    if (ctx->entry_pid != NULL) RTfree(ctx->entry_pid);
    if (ctx->first_at  != NULL) RTfree(ctx->first_at);
    if (ctx->pj        != NULL) RTfree(ctx->pj);
    if (ctx->itab      != NULL) RTfree(ctx->itab);
    RTfree(ctx);
}

/**
 * ------------------------------------------------------------------------
 * Consing a residual list
 * ------------------------------------------------------------------------
 */

/** Total order on residuals; the relation index is not part of it. */
static inline int
rl_lt(const rl_entry_t *a, const rl_entry_t *b)
{
    if (a->pid != b->pid) return a->pid < b->pid;
    return a->R < b->R;
}

/**
 * Reduce, including its identification of duplicates, applied *before*
 * consing, so that logically equal lists are physically equal (App. B.6).
 * m is small (the number of simultaneously live relations), so insertion sort.
 */
static int
rl_sort_dedup(rl_entry_t *e, int m)
{
    for (int i = 1; i < m; i++) {
        rl_entry_t t = e[i];
        int j = i - 1;
        while (j >= 0 && rl_lt(&t, &e[j])) { e[j+1] = e[j]; j--; }
        e[j+1] = t;
    }
    int o = 0;
    for (int i = 0; i < m; i++) {
        if (o > 0 && e[o-1].pid == e[i].pid && e[o-1].R == e[i].R) continue;
        e[o++] = e[i];
    }
    return o;
}

/**
 * PRECONDITION: every e[i].R is already rooted by the caller -- they are
 * cofactors of a chain that is on the refs stack, or diagrams of relations
 * this context protects.
 * POSTCONDITION: the caller roots the returned handle immediately.
 *
 * acc is pushptr'd defensively, not out of necessity: rl_cons reaches
 * _mtbdd_makenode, which roots both of its own arguments across the collection
 * it may trigger, and the prefix built so far is exactly its low argument. The
 * pushptr keeps the invariant local instead of resting on a callee's
 * discipline, and costs nothing.
 */
static MTBDD
rl_build(const rl_entry_t *e, int m)
{
    MTBDD acc = RL_EPS;
    mtbdd_refs_pushptr(&acc);
    for (int i = m - 1; i >= 0; i--) {
        assert(e[i].R != sylvan_false);
        acc = rl_cons(e[i].pid, e[i].R, acc);
    }
    mtbdd_refs_popptr(1);
    if (m > 0) rl_cells_global += (uint64_t)m;   /* diagnostic only */
    return acc;
}

/**
 * ------------------------------------------------------------------------
 * Cofactors
 * ------------------------------------------------------------------------
 */

/**
 * Partition the residual diagram R at level l into its four blocks.  R is a
 * node of the original relation diagram (or a constant) branching on no
 * variable below 2*l; no node is created here, these are pointer derefs.
 *
 * Sylvan is fully reduced, and the two possible skips mean different things
 * (App. B, "The four block shapes"): a missing x_l means the source value is
 * irrelevant, a missing x_l' that the written value is *arbitrary* -- havoc,
 * not the identity.
 */
static inline void
rl_partition_rel(BDD r, BDDVAR topvar, BDD *r00, BDD *r01, BDD *r10, BDD *r11)
{
    BDD r0, r1;
    if (!sylvan_isconst(r) && bddnode_getvariable(MTBDD_GETNODE(r)) == topvar) {
        bddnode_t n = MTBDD_GETNODE(r);
        r0 = node_low(r, n);
        r1 = node_high(r, n);
    } else {
        r0 = r1 = r;
    }
    if (!sylvan_isconst(r0) && bddnode_getvariable(MTBDD_GETNODE(r0)) == topvar + 1) {
        bddnode_t n0 = MTBDD_GETNODE(r0);
        *r00 = node_low(r0, n0);
        *r01 = node_high(r0, n0);
    } else {
        *r00 = *r01 = r0;
    }
    if (!sylvan_isconst(r1) && bddnode_getvariable(MTBDD_GETNODE(r1)) == topvar + 1) {
        bddnode_t n1 = MTBDD_GETNODE(r1);
        *r10 = node_low(r1, n1);
        *r11 = node_high(r1, n1);
    } else {
        *r10 = *r11 = r1;
    }
}

static inline void
rl_partition_state(BDD s, BDDVAR topvar, BDD *s0, BDD *s1)
{
    if (!sylvan_isconst(s)) {
        bddnode_t n = MTBDD_GETNODE(s);
        if (bddnode_getvariable(n) == topvar) {
            *s0 = node_low(s, n);
            *s1 = node_high(s, n);
            return;
        }
        assert(bddnode_getvariable(n) > topvar);
    }
    *s0 = *s1 = s;
}

/**
 * ------------------------------------------------------------------------
 * ImageAll
 * ------------------------------------------------------------------------
 *
 * ImageAll(S, K) = union over K of Image(S, sigma), no order prescribed.
 * Image is Eq. 6, and on a read-closed relation Lemma th:image makes it
 * exactly sylvan_relnext over the residual's own Dom-pair cube: variables
 * outside the cube keep their value (which is the identity Eq. 4 demands),
 * variables inside are conjoined, quantified and renamed, and the action
 * variables, which are in neither, are quantified for free.
 *
 * S is rooted by the caller (its s0/s1 pushptr); a[i].R is rooted by the
 * caller's chain; a[i].pid's cube is a suffix of a protected relation cube.
 */
TASK_4(BDD, rl_imageall, BDD, S, rl_entry_t*, a, int, na, rl_ctx_t*, ctx)
{
    if (S == sylvan_false) return sylvan_false;
    BDD acc = sylvan_false;
    mtbdd_refs_pushptr(&acc);
    for (int i = 0; i < na; i++) {
        BDD t = mtbdd_refs_push(CALL(sylvan_relnext, S, a[i].R,
                                     ctx->pj[a[i].pid].imgvars, 0));
        acc = sylvan_or(acc, t);
        mtbdd_refs_pop(1);
    }
    mtbdd_refs_popptr(1);
    return acc;
}

/**
 * ------------------------------------------------------------------------
 * The havoc terminal (Alg. 4 line 6)
 * ------------------------------------------------------------------------
 *
 * R_rho = 1 does NOT mean "return 1", and it does not mean "drop rho": over a
 * non-empty residual write set it is havoc on W_rho and the identity
 * elsewhere, over an empty one it is the identity, and only the projections
 * tell them apart.  The answer is the cylindrification of S along the union of
 * the WRITE sets; quantifying over Dom instead is the paper's named slip and
 * is silently wrong, and quantifying over everything returns the whole state
 * space.
 */
#if RL_HAVOC_TERMINAL
static int
rl_all_one(MTBDD L)
{
    for (MTBDD c = L; c != RL_EPS; c = rl_tail(c)) {
        if (rl_res(c) != sylvan_true) return 0;
    }
    return 1;
}

TASK_4(BDD, rl_cylindrify, BDD, S, MTBDD, L, uint32_t, lvl, rl_ctx_t*, ctx)
{
    uint64_t w[ctx->words];
    memset(w, 0, sizeof(uint64_t) * ctx->words);

    mtbdd_refs_push(S);                      /* rooted across mtbdd_makenode  */

    for (MTBDD c = L; c != RL_EPS; c = rl_tail(c)) {
        const rl_proj_t *p = &ctx->pj[rl_pid(c)];
        /* W(pid) = W_repr cap [repr_l, n), so no bit set is stored per id. */
        const uint64_t *src = ctx->rel[p->repr_m].wbits;
        for (int i = p->repr_l; i < ctx->n; i++) {
            if (rl_tst(src, i)) rl_setbit(w, i);
        }
    }

    BDD cube = sylvan_set_empty();
    mtbdd_refs_pushptr(&cube);
    for (int i = ctx->n - 1; i >= (int)lvl; i--) {
        if (rl_tst(w, i)) cube = mtbdd_makenode(2u * (uint32_t)i, sylvan_false, cube);
    }
    BDD res = CALL(sylvan_exists, S, cube, 0);
    mtbdd_refs_popptr(1);
    mtbdd_refs_pop(1);
    return res;
}
#endif /* RL_HAVOC_TERMINAL */

/**
 * ------------------------------------------------------------------------
 * The level split (Def. 2 / Eq. 7)
 * ------------------------------------------------------------------------
 *
 * A residual is a POSITION: r00..r11 below are nodes that already exist inside
 * the relation diagram, or the constant 0, and cp is an interned integer.  No
 * diagram is built.  The four level classes fall out of the projections alone:
 *
 *   l not in Dom  : rho[ii] = rho, the identical triple and the identical
 *                   handle (Th. th:split); cross blocks 0.
 *   l in Rd only  : diagonal cofactors, cross blocks 0 (they are already 0 by
 *                   read-closure, but the guard is written anyway).
 *   l in W        : all four blocks, and l leaves both projections.
 */
static int
rl_split_diag(MTBDD Lx, uint32_t lvl, const rl_ctx_t *ctx, int hi, rl_entry_t *out)
{
    const BDDVAR xu = 2u * lvl;
    int cnt = 0;
    for (MTBDD c = Lx; c != RL_EPS; c = rl_tail(c)) {
        const uint32_t p = rl_pid(c);
        const BDD R = rl_res(c);
        uint32_t cp;
        BDD Rd;
        if (ctx->pj[p].dmin != (int32_t)lvl) {
            cp = p;
            Rd = R;                                  /* Th. th:split: identical */
        } else {
            BDD r00, r01, r10, r11;
            rl_partition_rel(R, xu, &r00, &r01, &r10, &r11);
            cp = ctx->pj[p].succ;
            Rd = hi ? r11 : r00;
        }
        if (Rd == sylvan_false) continue;            /* Reduce: drop R_rho = 0 */
        if (ctx->pj[cp].wmin == RL_INF) continue;    /* Reduce: retirement.
                                                      * Read off the PROJECTIONS,
                                                      * never the diagram. */
        out[cnt].pid = cp;
        out[cnt].R   = Rd;
        cnt++;
    }
    return rl_sort_dedup(out, cnt);
}

static void
rl_split_cross(MTBDD Lx, uint32_t lvl, const rl_ctx_t *ctx,
               rl_entry_t *a01, int *na01, rl_entry_t *a10, int *na10)
{
    const BDDVAR xu = 2u * lvl;
    int n01 = 0, n10 = 0;
    for (MTBDD c = Lx; c != RL_EPS; c = rl_tail(c)) {
        const uint32_t p = rl_pid(c);
        /* Alg. 4 line 7: A = <rho in L | l in W_rho>, and by Eq. 7 case 3 a
         * non-writer's off-diagonal blocks are 0.  THE GUARD.  Without it, at
         * a level a relation does not touch all four syntactic cofactors of
         * R_rho equal R_rho, and the cross image would flip x_l for free --
         * a sound-looking over-approximation. */
        if (ctx->pj[p].wmin != (int32_t)lvl) continue;
        assert(ctx->pj[p].dmin == (int32_t)lvl);      /* W is a subset of Dom  */
        BDD r00, r01, r10, r11;
        rl_partition_rel(rl_res(c), xu, &r00, &r01, &r10, &r11);
        (void)r00; (void)r11;
        const uint32_t cp = ctx->pj[p].succ;
        /* No retirement filter here: Reduce applies to the list entering a
         * call and never to a cross residual, where <1,{},{}> is the
         * transition "x_l goes from i to j and nothing below changes". */
        if (r01 != sylvan_false) { a01[n01].pid = cp; a01[n01].R = r01; n01++; }
        if (r10 != sylvan_false) { a10[n10].pid = cp; a10[n10].R = r10; n10++; }
    }
    *na01 = rl_sort_dedup(a01, n01);
    *na10 = rl_sort_dedup(a10, n10);
}

/**
 * The relations entering at this level join the chain (App. B.6: relations not
 * yet reached are transparent and are the suffix of the entry-sorted array
 * beyond l).  Reduce, including dedup, runs here, before consing.
 */
static MTBDD
rl_insert_entering(MTBDD L, const rl_ctx_t *ctx, int from, int to, rl_entry_t *buf)
{
    int cnt = 0;
    for (MTBDD c = L; c != RL_EPS; c = rl_tail(c)) {
        buf[cnt].pid = rl_pid(c);
        buf[cnt].R   = rl_res(c);
        cnt++;
    }
    for (int i = from; i < to; i++) {
        buf[cnt].pid = ctx->entry_pid[i];
        buf[cnt].R   = ctx->rel[i].bdd;
        cnt++;
    }
    cnt = rl_sort_dedup(buf, cnt);
    return rl_build(buf, cnt);
}

/**
 * Under full reduction the recursion advances to the minimum of the top level
 * of S, the smallest residual domain level among the live cells, and the entry
 * level of the pending suffix (App. B.6).  min Dom, not min W: skipping a
 * level on which a live residual still branches carries an unresolved node
 * into an image whose quantification no longer covers it.
 */
static uint32_t
rl_advance(BDD S, MTBDD L, uint32_t lvl, const rl_ctx_t *ctx)
{
#if !RL_ADVANCE
    (void)S; (void)L; (void)ctx;
    return lvl;
#else
    uint32_t best = (uint32_t)ctx->n;
    if (!sylvan_isconst(S)) {
        assert((sylvan_var(S) & 1) == 0);          /* S branches on x, not x'  */
        uint32_t v = sylvan_var(S) >> 1;
        if (v < best) best = v;
    }
    for (MTBDD c = L; c != RL_EPS; c = rl_tail(c)) {
        uint32_t d = (uint32_t)ctx->pj[rl_pid(c)].dmin;
        if (d < best) best = d;
    }
    if (ctx->first_at[lvl] < ctx->k) {
        uint32_t e = (uint32_t)ctx->entry[ctx->first_at[lvl]];
        if (e < best) best = e;
    }
    return best > lvl ? best : lvl;
#endif
}

/**
 * ------------------------------------------------------------------------
 * ReachDD (Alg. 4)
 * ------------------------------------------------------------------------
 *
 * S is a quasi-ROBDD over levels lvl..n-1, L the chain of the residuals of the
 * relations that have already entered, and lvl the level.  The relations that
 * have NOT entered are transparent and are the suffix of ctx->rel beyond
 * ctx->first_at[lvl].
 */
TASK_4(BDD, rl_reach, BDD, S, MTBDD, L, uint32_t, lvl, rl_ctx_t*, ctx)
{
    /* line 2 */
    if (S == sylvan_false) return sylvan_false;

    lvl = rl_advance(S, L, lvl, ctx);

    /* line 3 (Reduce) is performed by the caller, before consing L. */

    const int pending = ctx->first_at[lvl];

    /* line 4.  L = eps is "the chain is empty AND nothing is pending"
     * (App. B.6); testing only the chain returns S unchanged at any level in a
     * gap between one relation's retirement and the next one's entry, and
     * silently loses every transition below. */
    if (L == RL_EPS && pending == ctx->k) return S;

    /* line 5 */
    if (S == sylvan_true) return sylvan_true;

    /* line 6, with the same pending guard. */
#if RL_HAVOC_TERMINAL
    if (pending == ctx->k && rl_all_one(L)) {
        return CALL(rl_cylindrify, S, L, lvl, ctx);
    }
#endif

    BDD res;
    const uint64_t key3 = ((uint64_t)ctx->id << 32) | (uint64_t)lvl;
    if (cache_get3(RL_OPID, S, L, key3, &res)) return res;

    /* Both key components must stay rooted all the way to cache_put3: if the
     * argument chain were collected before the put, its bucket could be reused
     * and we would store a key naming an unrelated node. */
    mtbdd_refs_push(S);                                            /* (1) */
    mtbdd_refs_push(L);                                            /* (2) */

    /* Cor. th:sat-special: a pinned relation is applied whole at its entry
     * level, on the recombined node, inside this level's fixpoint -- which is
     * exactly what saturation does.  Pinning is the safety valve of App. B.6
     * and stops residual cells being allocated at all. */
    const int pin       = rl_pinning();
    const int enter_end = ctx->first_at[lvl + 1];
    const int npin      = pin ? (enter_end - pending) : 0;
    const int nent      = pin ? 0 : (enter_end - pending);

    MTBDD Lx;
    if (nent == 0) {
        Lx = L;
    } else {
        const int nl = rl_len(L);
        rl_entry_t ebuf[nl + nent];
        Lx = rl_insert_entering(L, ctx, pending, enter_end, ebuf);
    }
    mtbdd_refs_push(Lx);                                           /* (3) */

    /* Scratch for the split.  A writer contributes at most one entry to each
     * of the two cross lists, and the diagonal has at most one entry per
     * cell. */
    int m = 0, nw = 0;
    for (MTBDD c = Lx; c != RL_EPS; c = rl_tail(c)) {
        m++;
        if (ctx->pj[rl_pid(c)].wmin == (int32_t)lvl) nw++;
    }
    rl_entry_t scratch[2 * nw + m + 1];
    rl_entry_t *a01 = scratch, *a10 = scratch + nw, *tmp = scratch + 2 * nw;

    int na01 = 0, na10 = 0;
    rl_split_cross(Lx, lvl, ctx, a01, &na01, a10, &na10);          /* line 7 */

    MTBDD L00 = RL_EPS, L11 = RL_EPS;
    mtbdd_refs_pushptr(&L00);                                      /* (4) */
    mtbdd_refs_pushptr(&L11);                                      /* (5) */
    L00 = rl_build(tmp, rl_split_diag(Lx, lvl, ctx, 0, tmp));
    L11 = rl_build(tmp, rl_split_diag(Lx, lvl, ctx, 1, tmp));

    BDD s0, s1;
    rl_partition_state(S, 2u * lvl, &s0, &s1);
    mtbdd_refs_pushptr(&s0);                                       /* (6) */
    mtbdd_refs_pushptr(&s1);                                       /* (7) */

    if (na01 == 0 && na10 == 0 && npin == 0) {
        /* lines 8-10: A = eps.  The locality shortcut: no loop, no image. */
        s0 = CALL(rl_reach, s0, L00, lvl + 1, ctx);
        s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx);
    } else {
        BDD p0 = sylvan_false, p1 = sylvan_false;
        mtbdd_refs_pushptr(&p0);                                   /* (8) */
        mtbdd_refs_pushptr(&p1);                                   /* (9) */
        while (s0 != p0 || s1 != p1) {                             /* line 11 */
            p0 = s0;
            p1 = s1;
            s0 = CALL(rl_reach, s0, L00, lvl + 1, ctx);            /* line 12 */
            if (na01 != 0) {                                       /* line 13 */
                BDD t = mtbdd_refs_push(CALL(rl_imageall, s0, a01, na01, ctx));
                s1 = sylvan_or(s1, t);
                mtbdd_refs_pop(1);
            }
            s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx);            /* line 14 */
            if (na10 != 0) {                                       /* line 15 */
                BDD t = mtbdd_refs_push(CALL(rl_imageall, s1, a10, na10, ctx));
                s0 = sylvan_or(s0, t);
                mtbdd_refs_pop(1);
            }
            if (npin != 0) {
                /* Cor. th:sat-special, on the recombined node. */
                BDD cur = sylvan_makenode(2u * lvl, s0, s1);
                mtbdd_refs_pushptr(&cur);
                for (int i = pending; i < enter_end; i++) {
                    BDD step = mtbdd_refs_push(CALL(sylvan_relnext, cur,
                                                    ctx->rel[i].bdd,
                                                    ctx->rel[i].all_variables, 0));
                    cur = sylvan_or(cur, step);
                    mtbdd_refs_pop(1);
                }
                rl_partition_state(cur, 2u * lvl, &s0, &s1);
                mtbdd_refs_popptr(1);
            }
        }
        mtbdd_refs_popptr(2);
    }

    res = sylvan_makenode(2u * lvl, s0, s1);                       /* line 16 */

    cache_put3(RL_OPID, S, L, key3, res);

    mtbdd_refs_popptr(4);                       /* s1, s0, L11, L00            */
    mtbdd_refs_pop(3);                          /* Lx, L, S                    */
    return res;
}

BDD
rl_run(rl_ctx_t *ctx, BDD src)
{
    if (ctx == NULL) return src;
    rl_active = 1;
    BDD res = RUN(rl_reach, src, RL_EPS, 0, ctx);
    rl_active = 0;
    return res;
}
