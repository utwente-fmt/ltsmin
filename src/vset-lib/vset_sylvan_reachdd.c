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
 *
 * ------------------------------------------------------------------------
 * Learning inside the operation
 * ------------------------------------------------------------------------
 *
 * A relation with an rl_relspec_t::learn callback is expanded at its ENTRY
 * level, on the recombined node makenode(2l, S0, S1), inside that level's
 * fixpoint -- which is the same place saturation expands, for the same reason:
 * entry(rho) = min Dom_rho and Rd_rho is a subset of Dom_rho, so every read
 * level lies at or below l and sylvan_project(S, Rd_rho) is exact there.  The
 * prefix the recursion has forgotten is outside Dom_rho and cannot change the
 * projection; below the entry level the read bits are gone and no other level
 * would do.
 *
 * WHY THE OPERATION CACHE NEEDS NO INVALIDATION.  Learning changes the root of
 * R_rho and NOTHING else: the projections, the entry levels, first_at, the
 * interned table and every derived cube are functions of Rd_rho and W_rho,
 * which are fixed at rel_create_rw.  Two consequences.
 *
 * (a) The residual chain self-invalidates.  A cell is hash-consed on the
 *     residual HANDLE, so a changed root yields a different chain, hence a
 *     different cache key; and where a cofactor happens to be the identical
 *     node, the relation content at that position really is identical and
 *     reuse is exact.
 *
 * (b) A cached answer stays correct even though the pending (not-yet-entered)
 *     relations, which the key does not mention, keep growing.  Invariant (E):
 *     for every X this operation returns at level l and every relation m with
 *     entry(m) >= l, and every state x in X, either pi_Rd(x) has been recorded
 *     as explored for m, or every state that agrees with x outside W_m is
 *     already in X.  (E) is established at each return point -- vacuously at
 *     the terminals that have no pending relation, by the learn call at the
 *     main return for relations entering here, by induction on S0 and S1 for
 *     relations entering deeper, and by monotonicity of the explored sets for
 *     a cache hit.  (E) implies X is closed under the relation's FINAL,
 *     completely learned diagram: a transition learned later has, by the
 *     expand contract, a source that was not explored when X was computed and
 *     therefore -- explored sets only grow -- is not in X's first disjunct,
 *     while the second disjunct already contains every state such a transition
 *     could reach.  Since X also only ever grew by applying real transitions,
 *     X is still exactly the least fixpoint above S, whatever has been learned
 *     since.  This is the same property that lets saturation's go_sat cache on
 *     <set, rels, id> with no relation content in the key.
 *
 * The one hole is Alg. 4 line 5 (S = 1 -> return 1), which returns without
 * expanding.  The ANSWER is unaffected -- a full cube over levels [l,n) is
 * closed under any relation whose domain is inside [l,n), which is exactly
 * (E)'s second disjunct -- but the states inside it are never handed to the
 * front end, so pi(result) can be a strict subset of what has been explored.
 * Falling through instead would ask the front end to enumerate the entire
 * short-vector space.  Clients that need the explored sets to cover the answer
 * -- deadlock detection -- must therefore still make an expand pass of their
 * own; reach_dd() in pins2lts-sym/alg/sat.c keeps one.
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
    BDD       bdd;          /* protected; refreshed from every learn call    */
    BDD       all_variables;
    uint64_t *dombits;
    uint64_t *wbits;
    BDD     (*learn)(void *self, BDD S);   /* NULL: R_rho is already complete */
    void     *learn_self;
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
    int         learning;       /* some kept relation has a learn callback    */
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

/* Diagnostics only, but atomic: under the parallel schedule of Alg. 5 every
 * worker runs rl_reach, so a plain increment would be a data race and would
 * also lose counts.  They are bumped a few hundred thousand times per run,
 * which is far too rarely for the contention to be measurable. */
static volatile uint64_t rl_calls_global = 0;   /* rl_reach entries           */
static volatile uint64_t rl_hits_global  = 0;   /* of those, cache hits       */
/* Alg. 4 line 5 taken with a relation still pending and still learning: the
 * one place the operation returns states it has not expanded for.  Zero means
 * the explored sets provably cover the answer. */
static volatile uint64_t rl_waive_global = 0;

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

uint64_t
rl_calls_made(void)
{
    return rl_calls_global;
}

uint64_t
rl_cache_hits(void)
{
    return rl_hits_global;
}

uint64_t
rl_cube_waivers(void)
{
    return rl_waive_global;
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
     * (Eq. 4) and contributes no transition at all.
     *
     * R_rho = 0 is dropped only when the relation CANNOT grow.  Under learning
     * every relation starts empty, and dropping them here would make the whole
     * operation a no-op that still terminates and still prints plausible
     * numbers.  W_rho = empty stays unconditional: w_proj is fixed at
     * rel_create_rw and no callback can widen it. */
    ctx->rel       = (rl_rel_t*) RTmalloc(sizeof(rl_rel_t)  * (k > 0 ? k : 1));
    ctx->entry     = (int*)      RTmalloc(sizeof(int)       * (k > 0 ? k : 1));
    ctx->entry_pid = (uint32_t*) RTmalloc(sizeof(uint32_t)  * (k > 0 ? k : 1));

    size_t domtotal = 0;
    int kept = 0;
    int learning = 0;
    for (int m = 0; m < k; m++) {
        if (specs[m].bdd == sylvan_false && specs[m].learn == NULL) continue;
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
        ctx->rel[kept].learn         = specs[m].learn;
        ctx->rel[kept].learn_self    = specs[m].learn_self;
        if (specs[m].learn != NULL) learning = 1;
        ctx->rel[kept].dombits = (uint64_t*)RTmalloc(sizeof(uint64_t) * words);
        ctx->rel[kept].wbits   = (uint64_t*)RTmalloc(sizeof(uint64_t) * words);
        memcpy(ctx->rel[kept].dombits, specs[m].dombits, sizeof(uint64_t) * words);
        memcpy(ctx->rel[kept].wbits,   specs[m].wbits,   sizeof(uint64_t) * words);
        for (int i = 0; i < n; i++) if (rl_tst(ctx->rel[kept].dombits, i)) domtotal++;
        kept++;
    }
    ctx->k        = kept;
    ctx->learning = learning;

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
     * on that -- and it is what makes the write in rl_learn_level() safe: the
     * new root is rooted by this protection from the moment it is stored, and
     * the old one is still rooted by the vrel_t. */
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
    /* Diagnostic only, but atomic: Alg. 5 conses from every worker. */
    if (m > 0) __sync_fetch_and_add(&rl_cells_global, (uint64_t)m);
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
 * Sec. 4 "Parallelism": the |A| applications inside one ImageAll are mutually
 * independent by linearity of Image, so with par != 0 this is a balanced
 * reduction -- split A in half, spawn one half, call the other directly, union
 * the results -- of depth O(log |A|), with the union folded into every
 * internal node.  This is the third source of parallelism, the one saturation
 * cannot expose, its per-level body applying the relations in sequence with
 * each consuming the previous one's output.  With |A| = 2, the realistic size
 * on the high-locality models ReachDD targets, the tree degenerates to exactly
 * "spawn one image, call the other".
 *
 * Three terminals, not one.  S = 0 is linearity and is the common case where
 * one branch of the level is empty; na = 0 lets Alg. 5 spawn phase B
 * unconditionally (see rl_reach) instead of guarding a spawn and its sync on
 * two textually separate copies of one condition; na = 1 is the leaf that does
 * the work, one relnext and no union.
 *
 * REFERENCES.  S is rooted by the caller (its s0/s1 pushptr, which also roots
 * it across the whole spawn window); a[i].R is rooted by the caller's chain Lx,
 * of whose cells' residual diagrams they are cofactors, and a[i].pid's cube is
 * a suffix of a protected relation cube.  A thief inherits all of that: Sylvan
 * marks with TOGETHER, so every worker marks its own stacks and the victim's
 * roots are live for the thief too.
 */
TASK_5(BDD, rl_imageall, BDD, S, rl_entry_t*, a, int, na, rl_ctx_t*, ctx, int, par)
{
    if (S == sylvan_false) return sylvan_false;
    if (na == 0) return sylvan_false;                    /* union of nothing  */
    if (na == 1) {
        return CALL(sylvan_relnext, S, a[0].R, ctx->pj[a[0].pid].imgvars, 0);
    }

    if (!par) {
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

    const int h = na / 2;
    mtbdd_refs_spawn(SPAWN(rl_imageall, S, a, h, ctx, par));
    /* hi must be pushed BEFORE the sync: the sync may execute the sibling
     * inline on this worker, and that allocates. */
    const BDD hi = mtbdd_refs_push(CALL(rl_imageall, S, a + h, na - h, ctx, par));
    const BDD lo = mtbdd_refs_sync(SYNC(rl_imageall));
    /* And lo must be pushed BEFORE the union: mtbdd_refs_sync has just removed
     * its only root, and sylvan_and (which sylvan_or is written in terms of)
     * calls sylvan_gc_test() with its arguments only in registers. */
    mtbdd_refs_push(lo);
    const BDD res = sylvan_or(lo, hi);
    mtbdd_refs_pop(2);                    /* lo, hi                          */
    return res;
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
 *
 * A still-empty relation is skipped rather than consed: under learning
 * R_rho = 0 survives the root Reduce, and the empty relation contributes
 * nothing anyway.  This is Reduce applied dynamically instead of once at the
 * root, and it is also what keeps rl_build's precondition true.
 */
static MTBDD
rl_insert_entering(MTBDD L, const rl_ctx_t *ctx, int from, int to,
                   rl_entry_t *buf, BDD *built)
{
    int cnt = 0, pushed = 0;
    for (MTBDD c = L; c != RL_EPS; c = rl_tail(c)) {
        buf[cnt].pid = rl_pid(c);
        buf[cnt].R   = rl_res(c);
        cnt++;                       /* rooted by the caller's L              */
    }
    for (int i = from; i < to; i++) {
        const BDD R = ctx->rel[i].bdd;
        /* Record exactly the root that goes into the chain, not one read a
         * moment earlier: the caller decides later whether its split is stale
         * by comparing these, and every value recorded here is either
         * sylvan_false or a residual of the returned chain -- which the caller
         * keeps on its refs stack, so none of them can be collected and have
         * its handle reused underneath the comparison. */
        built[i - from] = R;
        if (R == sylvan_false) continue;                 /* Reduce            */
        /* The mirror is what roots R, and under Alg. 5 another worker may
         * republish it (with a bigger diagram) at any moment -- after which
         * this snapshot is rooted by nobody and rl_build's allocation could
         * collect it.  Root it here, for as long as it is only in a C array.
         * There is no allocation between the read and this push, and a
         * collection needs every worker at a sylvan_gc_test(), so the window
         * is closed. */
        mtbdd_refs_push(R);
        pushed++;
        buf[cnt].pid = ctx->entry_pid[i];
        buf[cnt].R   = R;
        cnt++;
    }
    cnt = rl_sort_dedup(buf, cnt);
    MTBDD res = rl_build(buf, cnt);
    mtbdd_refs_pop(pushed);          /* no allocation before the caller roots */
    return res;
}

/**
 * Expand relation i over cur and publish the result in ctx->rel[i].bdd, which
 * rl_ctx_create protects; returns the published diagram, rooted by that
 * protection from the moment it lands there.  Whether the relation grew is
 * then an O(1), exact test on the root handle.
 *
 * PRECONDITION: cur is rooted by the caller; the callback allocates.
 *
 * THE PUBLICATION MUST BE MONOTONE.  Under Alg. 5 two workers can be at the
 * same relation's entry level at the same time, each with its own cur, so both
 * call the callback and both publish.  LTSmin's own relation grows under a
 * compare-and-swap loop, so the values the callback returns form a chain, each
 * a superset of the last; a plain store could put an OLDER link of that chain
 * back into the mirror.  That would break invariant (E): the sources of the
 * transitions in the lost link have already been recorded as explored, so (E)'s
 * "a transition learned later has an unexplored source" argument would no
 * longer cover them, and the operation could memoise a set that is not closed.
 * The compare-and-swap below keeps the mirror monotone.  In the sequential
 * schedule it succeeds on the first try with old == before and costs one
 * atomic and no decision-diagram operation.
 */
static BDD
rl_learn_one(rl_ctx_t *ctx, int i, BDD cur)
{
    const BDD before = ctx->rel[i].bdd;
    BDD nw = ctx->rel[i].learn(ctx->rel[i].learn_self, cur);
    if (nw == before) return nw;

    mtbdd_refs_push(nw);
    BDD expect = before;
    for (;;) {
        const BDD old = __sync_val_compare_and_swap(&ctx->rel[i].bdd, expect, nw);
        if (old == expect || old == nw) break;
        /* Another worker published `old` in between.  Both are snapshots of
         * the same monotonically growing diagram, so their union is simply
         * the newer of the two -- and is in any case a superset of both,
         * which is all monotonicity needs. */
        mtbdd_refs_push(old);
        const BDD merged = sylvan_or(old, nw);
        mtbdd_refs_pop(2);                          /* old, nw               */
        nw = mtbdd_refs_push(merged);               /* no allocation between */
        expect = old;
    }
    mtbdd_refs_pop(1);
    return ctx->rel[i].bdd;
}

/**
 * Expand every relation entering at this level over cur.
 *
 * WHAT THIS DOES NOT REPORT, AND WHY.  It is tempting to return "did my
 * callback change the diagram?" and rebuild the level's split only then.  That
 * is wrong under Alg. 5, and silently: another worker at this same entry level
 * can publish a bigger diagram between our previous split build and now, and
 * our own callback then reports NO change (there is nothing left to learn for
 * our sources) while the split we are still holding was built from the older
 * diagram.  The expand contract attaches to the value in the mirror AT THE
 * MOMENT THE CALLBACK RETURNS, so only a read taken after it carries the
 * guarantee.  The caller therefore decides staleness by comparing the roots
 * the split was built from against the mirrors as they are now -- which in the
 * sequential schedule is exactly "my callback changed something", and under
 * Alg. 5 is the honest test.
 *
 * The caller must do ALL of a level's learning before reading any root into a
 * scratch buffer, and must not learn between that read and rl_build's cons: a
 * root read out of ctx->rel[].bdd is rooted only while ctx->rel[].bdd still
 * holds it (rl_insert_entering pushes what it reads for exactly that reason).
 */
static void
rl_learn_level(rl_ctx_t *ctx, int from, int to, BDD cur)
{
    for (int i = from; i < to; i++) {
        if (ctx->rel[i].learn == NULL) continue;
        rl_learn_one(ctx, i, cur);
    }
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
 * ReachDD (Alg. 4) and ReachDDPar (Alg. 5)
 * ------------------------------------------------------------------------
 *
 * S is a quasi-ROBDD over levels lvl..n-1, L the chain of the residuals of the
 * relations that have already entered, and lvl the level.  The relations that
 * have NOT entered are transparent and are the suffix of ctx->rel beyond
 * ctx->first_at[lvl].
 *
 * par selects the schedule of the loop body, per level.  Alg. 4 is
 * Gauss-Seidel -- every statement reads what its predecessor wrote -- and
 * Alg. 5 breaks exactly one of those edges, line 14's dependency on line 13,
 * so that the two recursions run on the pre-round pair, the two cross images
 * on the post-recursion pair, and the two unions simultaneously:
 *
 *   A   spawn ReachDD(S0,L00) || call ReachDD(S1,L11); sync   (lines 12,14)
 *   B   spawn ImageAll(S1,A10) || call ImageAll(S0,A01); sync (lines 13,15)
 *   C   spawn Union(S0,T0)     || call Union(S1,T1); sync
 *
 * Every statement of Alg. 5's body is a statement of Alg. 4's over the same
 * operands, each monotone and each sound, and the exit test is the same "a
 * whole round changed nothing" -- at which point S0 = ReachDD(S0,L00),
 * S1 = ReachDD(S1,L11), S0 contains ImageAll(S1,A10) and S1 contains
 * ImageAll(S0,A01), which is precisely what Alg. 4's exit establishes.  Two
 * chaotic iterations of the same monotone statement set with the same exit
 * condition over a finite lattice reach the same least fixpoint above the same
 * start, so ALG. 5 CANNOT CHANGE THE RESULT, only the number of rounds.  Both
 * schedules therefore also write the same operation cache with the same
 * values, and may freely serve each other's entries.
 *
 * ONE SCHEDULE IS NOT UNIFORMLY BETTER.  Alg. 5's round propagates less than
 * Alg. 4's (S1's recursion no longer sees this round's cross flow out of S0),
 * so a level whose fixpoint is deep can need more rounds under Alg. 5.
 *
 * REFERENCE COUNTING ACROSS A SPAWN.  Sylvan operations do not root their own
 * arguments (sylvan_and calls sylvan_gc_test() with a and b only in
 * registers), and mtbdd_refs_mark_s_par marks a spawned task's RESULT, never
 * its arguments -- so every argument of a spawned or called operation must be
 * on this worker's refs stacks for the whole spawn window.  Here that is: S
 * and L on the values stack, and Lx, L00, L11, cur, s0, s1, p0, p1, t0, t1 on
 * the pointer stack.  Lx is the one to watch: a01[] and a10[] are plain C
 * arrays of raw handles with no refs entry of their own, and are live only
 * because their entries are cofactors of Lx's cells and because Sylvan's
 * collector rehashes in place instead of relocating.  Lx must not be popped
 * before the last SYNC of the last round.  The push and pop counts do not
 * depend on any runtime branch: a spawn and a sync guarded by two textually
 * separate copies of one condition is how a task stack silently desynchronises,
 * so phases B and C spawn unconditionally and let rl_imageall's na = 0 and
 * sylvan_and's terminal absorb the empty case.
 */
TASK_5(BDD, rl_reach, BDD, S, MTBDD, L, uint32_t, lvl, rl_ctx_t*, ctx, int, par)
{
    __sync_fetch_and_add(&rl_calls_global, 1u);        /* diagnostic only */

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

    /* line 5.  Returns without expanding, and that is deliberate: falling
     * through would hand the front end the whole short-vector space.  The
     * answer is right anyway -- see the note on this line in "Learning inside
     * the operation" at the top of this file -- but a client that needs the
     * explored sets to cover the answer must make its own expand pass. */
    if (S == sylvan_true) {
        if (ctx->learning && pending < ctx->k) __sync_fetch_and_add(&rl_waive_global, 1u);
        return sylvan_true;
    }

    /* line 6, with the same pending guard. */
#if RL_HAVOC_TERMINAL
    if (pending == ctx->k && rl_all_one(L)) {
        return CALL(rl_cylindrify, S, L, lvl, ctx);
    }
#endif

    BDD res;
    const uint64_t key3 = ((uint64_t)ctx->id << 32) | (uint64_t)lvl;
    /* No generation stamp, and no invalidation when a relation grows: by
     * invariant (E) a memoised answer was already closed under the relations'
     * final content when it was stored.  Learning changes only a relation's
     * root, so a chain built from a changed relation is a different chain and
     * the key changes by itself where it must. */
    if (cache_get3(RL_OPID, S, L, key3, &res)) {
        __sync_fetch_and_add(&rl_hits_global, 1u);     /* diagnostic only */
        return res;
    }

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
    const int nenter    = enter_end - pending;
    const int npin      = pin ? nenter : 0;
    const int nent      = pin ? 0 : nenter;

    /* On-the-fly learning, at the entry level, inside the level fixpoint.  It
     * runs before the split is (re)built and before the body, so that the node
     * this call finally returns is one every entering relation has been
     * expanded over -- that is invariant (E), and it is what makes the
     * operation cache sound without a stamp.  The price is the A = eps
     * shortcut at entry levels: the result of a transparent entry level is
     * still bigger than S, so it must be looped over and expanded for.  Pinned
     * levels learn in their own branch below instead, immediately before each
     * image, statement for statement as go_sat does. */
    const int learn_here = ctx->learning && nent != 0;

    /* One fixed bound for every scratch array, because the split may be built
     * more than once.  Reduce and dedup only ever shorten the chain, so it
     * never exceeds |L| + nent; a writer contributes at most one entry to each
     * cross list, and the diagonal at most one per cell.
     *
     * THESE STAY ON THE C STACK.  Lace 1.7 offers a per-worker scratch arena
     * that looks like the right home for them -- the frame is ~64(|L|+nent)
     * bytes and the recursion is n deep -- but it CANNOT hold anything that
     * must survive a decision diagram operation.  Sylvan starts a collection
     * with Lace's NEWFRAME, and lace_wrap_newframe hands every OTHER worker
     * lace_steal_loop as the new frame's root task, which those workers run on
     * top of their live task frames.  lace_steal_loop's idle path assumes it
     * only ever runs with the stack unwound: on entering deep backoff it treats
     * a non-empty arena as a leaked allocation, prints "worker N entered idle
     * with M bytes of leaked scratch" and sets scratch_top back to
     * scratch_base -- handing this frame's a01/a10 out again to the next
     * allocation.  Measured: with the arrays in the arena, bakery.3.dve at
     * --sylvan-sizes=18,18,18,18 and two workers over-approximated in 16 runs
     * out of 20, and never once with them here. */
    const int cap = rl_len(L) + nent;
    rl_entry_t ebuf[cap + 1], scratch[3 * cap + 1];
    rl_entry_t *a01 = scratch, *a10 = scratch + cap, *tmp = scratch + 2 * cap;
    /* The diagram roots the current split was built from, one per relation
     * entering here; rl_insert_entering fills them in as it conses.  They need
     * no reference of their own: each is either sylvan_false or a residual of
     * Lx, which is on the refs stack, so none of them can be collected and
     * have its handle reused underneath the comparison. */
    BDD built[nent + 1];

    MTBDD Lx = L, L00 = RL_EPS, L11 = RL_EPS;
    BDD   cur = sylvan_false;
    mtbdd_refs_pushptr(&Lx);                                       /* (3) */
    mtbdd_refs_pushptr(&L00);                                      /* (4) */
    mtbdd_refs_pushptr(&L11);                                      /* (5) */
    mtbdd_refs_pushptr(&cur);                                      /* (6) */

    /* p0/p1 are the round snapshot and t0/t1 Alg. 5's two ImageAll results,
     * which must both survive from phase B to phase C.  All four are pushed on
     * BOTH schedules: a pushptr count that depends on a runtime branch cannot
     * be audited, and the sequential body pays two unused pointer slots. */
    BDD s0, s1, p0 = sylvan_false, p1 = sylvan_false;
    BDD t0 = sylvan_false, t1 = sylvan_false;
    rl_partition_state(S, 2u * lvl, &s0, &s1);
    mtbdd_refs_pushptr(&s0);                                       /* (7) */
    mtbdd_refs_pushptr(&s1);                                       /* (8) */
    mtbdd_refs_pushptr(&p0);                                       /* (9) */
    mtbdd_refs_pushptr(&p1);                                       /* (10) */
    mtbdd_refs_pushptr(&t0);                                       /* (11) */
    mtbdd_refs_pushptr(&t1);                                       /* (12) */

    /* Cor. th:sat-special is an ordering claim as much as a scheduling one --
     * a pinned relation is applied whole, in sequence, exactly as saturation
     * applies it -- so a level with pinned relations entering runs Alg. 4's
     * body whatever the caller asked for.  The GC safety valve can turn
     * pinning on in the middle of a run, long after the top-level schedule was
     * chosen, which is why this is decided per level and not once. */
    const int par_here = par && npin == 0;

    int na01 = 0, na10 = 0, stale = 1;
    for (;;) {                                                     /* line 11 */
        if (learn_here) {
            cur = sylvan_makenode(2u * lvl, s0, s1);
            rl_learn_level(ctx, pending, enter_end, cur);
            cur = sylvan_false;                     /* release, for gc        */
            /* Rebuild whenever the diagrams are not the ones the split was
             * built from -- ours having grown, or another worker's having
             * grown under us.  See rl_learn_level. */
            for (int i = 0; !stale && i < nent; i++) {
                if (built[i] != ctx->rel[pending + i].bdd) stale = 1;
            }
        }
        if (stale) {
            /* All of this level's learning is done for this pass, so a root
             * read out of ctx->rel[].bdd here stays rooted until rl_build has
             * consed it.  rl_insert_entering also records what it consed in
             * built[], which is what the staleness test above compares. */
            Lx = (nent == 0) ? L
                             : rl_insert_entering(L, ctx, pending, enter_end,
                                                  ebuf, built);
            rl_split_cross(Lx, lvl, ctx, a01, &na01, a10, &na10);  /* line 7  */
            L00 = rl_build(tmp, rl_split_diag(Lx, lvl, ctx, 0, tmp));
            L11 = rl_build(tmp, rl_split_diag(Lx, lvl, ctx, 1, tmp));
            stale = 0;
        }
        if (!learn_here && na01 == 0 && na10 == 0 && npin == 0) {
            /* lines 8-10: A = eps.  The locality shortcut: no loop, no image.
             * The two recursions are independent, so this is also where
             * ReachDDPar spawns what parallel saturation spawns at a level it
             * skips -- parity, and on a high-locality model most of the tree. */
            if (par_here) {
                mtbdd_refs_spawn(SPAWN(rl_reach, s0, L00, lvl + 1, ctx, par));
                s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx, par);
                s0 = mtbdd_refs_sync(SYNC(rl_reach));
            } else {
                s0 = CALL(rl_reach, s0, L00, lvl + 1, ctx, par);
                s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx, par);
            }
            break;
        }
        p0 = s0;
        p1 = s1;
        if (par_here) {
            /* Alg. 5 phase A: both recursions on the pre-round pair.  s0 and
             * L00 are the spawned task's arguments and neither is touched
             * before the sync; the direct call's value lands in s1, an
             * address-taken slot, before the sync can allocate. */
            mtbdd_refs_spawn(SPAWN(rl_reach, s0, L00, lvl + 1, ctx, par));
            s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx, par);       /* line 14 */
            s0 = mtbdd_refs_sync(SYNC(rl_reach));                  /* line 12 */

            /* Alg. 5 phase B: both cross images on the post-recursion pair.
             * Spawned unconditionally; na10 == 0 is a terminal of rl_imageall. */
            mtbdd_refs_spawn(SPAWN(rl_imageall, s1, a10, na10, ctx, par));
            t1 = CALL(rl_imageall, s0, a01, na01, ctx, par);       /* line 13 */
            t0 = mtbdd_refs_sync(SYNC(rl_imageall));               /* line 15 */

            /* Alg. 5 phase C: the two unions simultaneously.  sylvan_or is a
             * macro over sylvan_and, and sylvan_and inside a task body is an
             * inline call, so the fork has to be written on sylvan_and itself.
             * sylvan_not only flips the complement bit, which the collector
             * strips, so all four operands are rooted by s0, s1, t0, t1; and
             * an empty T is sylvan_true after the negation, which sylvan_and
             * answers from its own terminal. */
            mtbdd_refs_spawn(SPAWN(sylvan_and, sylvan_not(s0), sylvan_not(t0), 0));
            s1 = sylvan_not(CALL(sylvan_and, sylvan_not(s1), sylvan_not(t1), 0));
            s0 = sylvan_not(mtbdd_refs_sync(SYNC(sylvan_and)));
            t0 = sylvan_false;                      /* release, for gc        */
            t1 = sylvan_false;
        } else {
            s0 = CALL(rl_reach, s0, L00, lvl + 1, ctx, par);       /* line 12 */
            if (na01 != 0) {                                       /* line 13 */
                BDD t = mtbdd_refs_push(CALL(rl_imageall, s0, a01, na01, ctx, par));
                s1 = sylvan_or(s1, t);
                mtbdd_refs_pop(1);
            }
            s1 = CALL(rl_reach, s1, L11, lvl + 1, ctx, par);       /* line 14 */
            if (na10 != 0) {                                       /* line 15 */
                BDD t = mtbdd_refs_push(CALL(rl_imageall, s1, a10, na10, ctx, par));
                s0 = sylvan_or(s0, t);
                mtbdd_refs_pop(1);
            }
        }
        if (npin != 0) {
            /* Cor. th:sat-special, on the recombined node: learn, then apply
             * whole, per relation -- which is go_sat's inner loop exactly.
             * The relation diagram is copied into a local and rooted there:
             * the mirror is what roots it, and another worker at this same
             * entry level may republish the mirror while sylvan_relnext runs. */
            cur = sylvan_makenode(2u * lvl, s0, s1);
            for (int i = pending; i < enter_end; i++) {
                BDD Ri = ctx->rel[i].learn != NULL ? rl_learn_one(ctx, i, cur)
                                                   : ctx->rel[i].bdd;
                mtbdd_refs_push(Ri);
                BDD step = mtbdd_refs_push(CALL(sylvan_relnext, cur, Ri,
                                                ctx->rel[i].all_variables, 0));
                cur = sylvan_or(cur, step);
                mtbdd_refs_pop(2);                  /* step, Ri              */
            }
            rl_partition_state(cur, 2u * lvl, &s0, &s1);
            cur = sylvan_false;                     /* release, for gc        */
        }
        if (s0 == p0 && s1 == p1) break;
    }

    res = sylvan_makenode(2u * lvl, s0, s1);                       /* line 16 */

    cache_put3(RL_OPID, S, L, key3, res);

    mtbdd_refs_popptr(6);       /* t1, t0, p1, p0, s1, s0                     */
    mtbdd_refs_popptr(4);       /* cur, L11, L00, Lx                          */
    mtbdd_refs_pop(2);          /* L, S                                       */
    return res;
}

BDD
rl_run(rl_ctx_t *ctx, BDD src)
{
    if (ctx == NULL) return src;
    rl_active = 1;
    BDD res = RUN(rl_reach, src, RL_EPS, 0, ctx, 0);
    rl_active = 0;
    return res;
}

BDD
rl_run_par(rl_ctx_t *ctx, BDD src)
{
    if (ctx == NULL) return src;
    rl_active = 1;
    BDD res = RUN(rl_reach, src, RL_EPS, 0, ctx, 1);
    rl_active = 0;
    return res;
}
