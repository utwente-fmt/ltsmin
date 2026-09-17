/**
 * \file vset_lddmc_reachdd.c
 * \brief ReachDDMdd (Alg. 6) for the Sylvan LDD back-end.
 *
 * The m-ary companion of vset_sylvan_reachdd.c, and the form the correctness
 * proof is actually carried out for -- Alg. 4, the Boolean case, is its
 * corollary.  ReachDDMdd(S, L, l) computes S.[[L]]^* from a *list* L of local
 * relations (Def. 1), by descending the state diagram and all k relations in
 * lockstep and splitting every relation at every level of its span into the
 * m x m block matrix its own projections dictate (Def. 2 / Eq. 7).
 * Saturation, by contrast, applies a whole relation with one image at its
 * entry level; Cor. th:sat-special says the two are settings of one operation,
 * and rlm_set_pin_all() selects saturation's.
 *
 * The transcription of Alg. 6 is rlm_reach() and rlm_walk() below, with the
 * paper's line labels in the comments.
 *
 * READ vset_sylvan_reachdd.c FIRST.  The two files are a pair and share their
 * whole shape: interned residual projections; a residual list as a hash-consed
 * chain of decision diagram nodes with the relation index dropped, so that
 * residuals equal as relations are equal as cells; Reduce and dedup before
 * consing; an operation cache keyed on <S, chain, level>; the pending-suffix
 * invariant, where "L = eps" means chain-empty AND nothing-pending; and
 * learning at a relation's entry level inside the level fixpoint.  What is
 * written out at length below is only what genuinely differs.
 *
 * ------------------------------------------------------------------------
 * The five structural differences from the Boolean case
 * ------------------------------------------------------------------------
 *
 * 1. THE PROJECTION IS THE META, AND IT IS POSITIONAL.  An LDD relation in
 *    LTSmin carries a "meta" chain over {0 skip, 1 read, 2 write, 3 only-read,
 *    4 only-write, 5 action, -1 end}, one entry per state level except a
 *    read-and-written level, which takes the two entries 1,2.  So the number
 *    of relation levels a state level consumes is 0 (skip), 1 (only-read,
 *    only-write) or 2 (read-and-written) and CANNOT be read off the diagram --
 *    a depth-4 relation LDD may be three single-class levels or one
 *    read-and-written level plus one other, and no node distinguishes them.
 *    This case analysis is rlm_split_diag()'s switch (with rlm_cross()'s
 *    counterpart for the off-diagonal blocks) and is the whole reason the LDD
 *    back-end could not simply reuse the Boolean split.
 *
 *    The compensation is that the meta suffix from level l on IS the residual
 *    projection <Rd cap [l,n), W cap [l,n)>, and lddmc_cube builds the chain
 *    bottom-up out of the hash-consing lddmc_makenode, so two relations with
 *    the same residual projection have the BIT-IDENTICAL suffix handle.
 *    Interning is therefore a hash on <first touched level, suffix handle>
 *    with no bit sets anywhere, and the same handle is lddmc_relprod's meta
 *    argument for free.
 *
 * 2. NO LEVEL SKIPPING.  lddmc_test_ismdd asserts that every down of a node
 *    has the same depth, so an LDD over an n-slot domain has depth exactly n
 *    on every path: it is quasi-reduced, which is precisely the hypothesis of
 *    the correctness proof.  The Boolean version's rl_advance() has no LDD
 *    analogue and is deliberately absent rather than ported -- which also
 *    removes its subtlest hazard, carrying an unresolved node into an image
 *    whose quantification no longer covers it.
 *
 * 3. READ-CLOSURE (Eq. 5) IS POSITIONAL AND AUTOMATIC.  An only-read level is
 *    one meta entry and one relation level serving as read and write at once:
 *    lddmc_relprod's only-read branch emits lddmc_makenode(value(n_set), ...),
 *    i.e. it writes back the value it read, so a non-identity there is not
 *    expressible.  Th. image therefore applies with no cur_is_next conjunct of
 *    the kind the Boolean back-end has to build, and
 *    Image(S, <p,R>) = lddmc_relprod(S, R, meta(p)) exactly.
 *
 * 4. THE HAVOC TERMINAL IS VACUOUS AND IS NOT IMPLEMENTED.  R_rho = 1 means
 *    the residual constrains nothing below.  An LDD node enumerates the values
 *    present and has no notion of "any value" except a copy node, which is the
 *    IDENTITY, not havoc; and the relation diagram's depth is pinned to its
 *    meta, so R_rho = lddmc_true can only occur once the remaining meta is the
 *    terminator, i.e. once Dom cap [l,n) -- hence W cap [l,n) -- is empty, at
 *    which point Reduce has already retired the cell and the eps terminal
 *    covers it.  lddmc_relprod asserts the same thing.  The paper calls the
 *    rule an optimisation and not a correctness requirement, so omitting it is
 *    answer-preserving; and it removes the Boolean version's single worst trap
 *    (returning the whole state space for a sound-looking over-approximation)
 *    for this back-end.  Two asserts record the claim: rlm_reach's
 *    S = lddmc_true guard, and rlm_cursor_init's "a live cell's class is 1, 3
 *    or 4".
 *
 * 5. THE BLOCKS ARE ENUMERATED BY WALKING TWO VALUE CHAINS, never by looping
 *    over D x D.  A skipped relation contributes m pass-throughs and no
 *    images; an only-read relation m diagonal residuals and no images; an
 *    only-written relation has m EQUAL rows, so its m^2 blocks collapse to one
 *    image per written value over the union of the sources; and only a
 *    genuinely read-and-written level can produce m^2, of which only the
 *    (read, write) pairs actually present in the diagram are non-zero.
 *    rlm_blocks_seen() counts what was actually enumerated.
 *
 * ------------------------------------------------------------------------
 * Retirement (Th. retire): the exact test, not the free one
 * ------------------------------------------------------------------------
 *
 * The meta's terminator gives a retirement test for free, but a CONSERVATIVE
 * one: it sits at max Dom, whereas Th. retire fires at max W, so a relation
 * with reads below its last written level would be carried further than it
 * needs to be and the eps terminal would fire late.  rlm_proj_t::wmin is the
 * exact test and costs one int32 per INTERNED SUFFIX -- not per relation, and
 * the suffix table is one the implementation builds anyway.  Consequence, and
 * an assertable invariant: a live cell's class is always one of 1, 3, 4 (or
 * "skip"), never 5 or -1.
 *
 * ------------------------------------------------------------------------
 * Cells
 * ------------------------------------------------------------------------
 *
 * A residual list cell is an ordinary LDD node built with a raw
 * llmsset_lookup: value = the 32-bit projection id, down = the residual
 * diagram, right = the tail.  mddnode_make packs (32, 47, 47) bits, so the
 * triple fits one 16-byte node with the copy bit left clear; llmsset is a
 * hash-cons, so equal triples give the identical handle; and
 * lddmc_gc_mark_rec follows down and right, so pushing one chain head roots
 * every cell AND every residual diagram in it, for free.
 *
 * lddmc_makenode itself CANNOT be used, for two independent reasons: it folds
 * when ifeq == lddmc_false, and it asserts strictly increasing values along
 * right.  Duplicate pids in one chain are normal here -- two relations with
 * the same residual projection share a pid, and an only-written level carrying
 * both a copy node and an explicit write of the same value yields two diagonal
 * cells with the same pid and different diagrams.  This is the exact LDD
 * analogue of the Boolean version's use of _mtbdd_makenode over mtbdd_makenode.
 * (Th. retire would license FUSING two equal-pid cells into R1 or R2; that is
 * deliberately not done, because it would build a diagram and destroy "a
 * residual is a position".  It remains available as a later knob.)
 *
 * Unlike the Boolean version there is no reserved variable range, so a cell
 * may be bit-identical to a genuine state or relation node and share its
 * handle.  That is harmless -- nodes are immutable, marking is identical, and
 * our operation cache id is private -- but it does mean a chain must never be
 * handed to a generic lddmc_* operation, and that printing one with
 * lddmc_fprintdot produces nonsense.
 */

#ifndef RLM_NO_HRE           /* the standalone unit test supplies its own shims */
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

#include <vset-lib/vset_lddmc_reachdd.h>

/**
 * Knob.  With RLM_CROSS_WHOLE the off-diagonal phase applies each writing cell
 * WHOLE at this level, with one lddmc_relprod(cur, R, meta) -- which is
 * lddmc_relprod's own block enumeration over exactly the same relation, and
 * therefore re-applies that cell's diagonal blocks as well.  That is sound
 * (the recursive call already covers the diagonal, and the loop runs to
 * convergence, so an inflationary duplicate changes nothing) and it is the
 * sharpest available check on the hand-written split: at the fixpoint the two
 * settings must agree on every model.  It is NOT Alg. 6 as printed, so it is
 * off by default and any measurement reported as "Alg. 6" must be the 0 run.
 */
#ifndef RLM_CROSS_WHOLE
#define RLM_CROSS_WHOLE 0
#endif

/* Operation cache identifiers.  Sylvan itself uses 0..93; LTSmin uses 200
 * (vset_sylvan.c saturation), 201 (vset_lddmc.c saturation) and 210
 * (vset_sylvan_reachdd.c); cache_next_opid() starts at 512<<40. */
#define RLM_OPID       (211LL<<40)
#define RLM_CHECK_OPID (212LL<<40)

/* Meta classes, as lddmc_relprod reads them. */
#define RLM_M_SKIP   0u
#define RLM_M_READ   1u   /* first half of a read-and-written level           */
#define RLM_M_WRITE  2u   /* second half of a read-and-written level          */
#define RLM_M_ONLYR  3u
#define RLM_M_ONLYW  4u
#define RLM_M_ACTION 5u
#define RLM_M_END    ((uint32_t)-1)

#define RLM_EPS lddmc_false
#define RLM_INF INT32_MAX

/**
 * ------------------------------------------------------------------------
 * Residual list cells
 * ------------------------------------------------------------------------
 */

static MDD
rlm_cons(uint32_t pid, MDD R, MDD tail)
{
    assert(R != lddmc_false);                 /* Reduce runs before consing   */
    struct mddnode n;
    mddnode_make(&n, pid, tail, R);           /* (value, right, down)         */

    int created;
    uint64_t index = llmsset_lookup(nodes, n.a, n.b, &created);
    if (index == 0) {
        /* Exactly lddmc_makenode's retry: root both children across the
         * collection, then look up again. */
        lddmc_refs_push(R);
        lddmc_refs_push(tail);
        RUN(sylvan_gc);
        lddmc_refs_pop(2);
        index = llmsset_lookup(nodes, n.a, n.b, &created);
        if (index == 0) {
            Abort("reachdd/lddmc: unique table full while consing a residual "
                  "cell; use --saturation=sat or a larger --sylvan-sizes");
        }
    }
    return (MDD)index;
}

static inline uint32_t rlm_pid (MDD c) { return mddnode_getvalue(LDD_GETNODE(c)); }
static inline MDD      rlm_res (MDD c) { return mddnode_getdown (LDD_GETNODE(c)); }
static inline MDD      rlm_tail(MDD c) { return mddnode_getright(LDD_GETNODE(c)); }

static inline int
rlm_len(MDD c)
{
    int m = 0;
    for (; c != RLM_EPS; c = rlm_tail(c)) m++;
    return m;
}

/**
 * ------------------------------------------------------------------------
 * Interned residual projections
 * ------------------------------------------------------------------------
 *
 * Interned is the pair <first touched level, meta suffix handle>.  The level
 * must be in the key because the meta sequence is relative where the level
 * sets are absolute ("only-read at 5" and "only-read at 7" share a suffix),
 * and interning at the FIRST TOUCHED level rather than at the current one is
 * what makes Th. split's identical-handle clause hold: at a level the residual
 * does not touch, the id -- hence the cell, hence the whole chain -- is
 * unchanged and nothing is re-consed.  Every per-level test then collapses to
 * an integer compare:
 *
 *   l in Dom(p) <=>  dmin(p) == l          (and dmin(p) > l means "skip")
 *   l in W(p)   <=>  wmin(p) == l
 *   successor   =    succ(p) if dmin(p) == l else p
 *   retired     <=>  wmin(p) == RLM_INF    (EXACT: max W, not max Dom)
 */
typedef struct {
    int32_t  dmin;      /* first touched state level, or RLM_INF              */
    int32_t  wmin;      /* first written state level, or RLM_INF (= retired)  */
    uint32_t cls;       /* meta class at dmin: 1, 3 or 4 (0 for the dead id)  */
    uint32_t succ;      /* id of the residual after consuming level dmin      */
    MDD      meta;      /* meta suffix at dmin; also the intern key           */
    MDD      nextmeta;  /* meta suffix at dmin+1: the cross image's argument  */
} rlm_proj_t;

typedef struct {
    MDD     *mdd;       /* the relation's live, externally protected root     */
    MDD      meta;      /* rel->meta, protected by its owner                  */
    MDD      topmeta;   /* rel->topmeta; only asserted against, never used    */
    void   (*learn)(void *self, MDD S);
    void    *learn_self;
} rlm_rel_t;

struct rlm_ctx {
    int         n;              /* number of state levels (vector length)     */
    int         k;              /* number of relations after Reduce           */
    rlm_rel_t  *rel;            /* k relations, ascending by entry level      */
    int        *entry;          /* entry[m] = min Dom(rel m) = rel->topvar    */
    uint32_t   *entry_pid;      /* projection id of rel m at its entry level  */
    int        *first_at;       /* first_at[l] = first m with entry[m] >= l   */
    rlm_proj_t *pj;
    uint32_t    npids;
    uint32_t    maxpids;
    uint32_t   *itab;           /* intern table, open addressing, stores p+1  */
    size_t      itab_size;
    uint32_t    id;             /* invocation id, third word of the cache key */
    int         learning;       /* some kept relation has a learn callback    */
};

/** One entry of a residual list under construction; no relation index. */
typedef struct { uint32_t pid; MDD R; } rlm_entry_t;

/**
 * The state of one cell during rlm_walk's pass over a level's value chain.
 *
 * R is the cell's residual for a cell that does not touch this level, and a
 * CURSOR into the relation's value chain at this level for one that does.  The
 * cursor is only ever advanced, so the pass over the level is a merge-join
 * against the state's own chain and costs O(m + |chain|), never O(m * |chain|)
 * and never O(m^2).
 */
typedef struct {
    uint32_t pid;       /* the cell's id (unchanged when it does not touch l) */
    uint32_t spid;      /* pj[pid].succ                                       */
    uint32_t cls;       /* pj[pid].cls, or RLM_M_SKIP when dmin > l           */
    MDD      R;         /* residual, or the walk cursor                       */
    MDD      cpy;       /* only-write: followcopy(R), else lddmc_false        */
} rlm_cursor_t;

/** Everything rlm_walk needs that is constant along one pass. */
typedef struct {
    rlm_cursor_t *cs;
    int           nc;
    rlm_entry_t  *buf;  /* scratch for one value's diagonal list              */
} rlm_walk_env_t;

/**
 * ------------------------------------------------------------------------
 * The safety valve (App. B.6)
 * ------------------------------------------------------------------------
 */
static volatile int      rlm_pin_all      = 0;   /* apply-whole schedule      */
static volatile int      rlm_pin_forced   = 0;   /* set by rlm_set_pin_all()  */
static volatile int      rlm_active       = 0;   /* only measure while running*/
static volatile int      rlm_starved      = 0;
static volatile size_t   rlm_gc_occupied  = 0;
static volatile uint64_t rlm_cells_global = 0;

/* Diagnostics only, but atomic, because the Sylvan sub-operations this
 * schedule calls fan out over every Lace worker and a learn callback can
 * therefore run on more than one of them. */
static volatile uint64_t rlm_calls_global  = 0;  /* rlm_reach entries         */
static volatile uint64_t rlm_hits_global   = 0;  /* of those, cache hits      */
static volatile uint64_t rlm_rounds_global = 0;  /* level fixpoint iterations */
static volatile uint64_t rlm_fixp_global   = 0;  /* level fixpoints entered   */
static volatile uint64_t rlm_blocks_global = 0;  /* off-diagonal blocks seen  */
/* Copy nodes accepted by the convention check, i.e. found at a WRITTEN level
 * (meta 2 or 4).  Counted because the whole copy-node question is empirical:
 * without regrouping LTSmin sets no copy flag at all, so a run reporting 0
 * here has not exercised the convention, and one reporting more than 0 has.
 * Nodes are counted once per <node, meta> pair, memoised like the walk. */
static volatile uint64_t rlm_copies_global = 0;

static inline int
rlm_pinning(void)
{
    return rlm_pin_all || rlm_pin_forced;
}

void     rlm_set_pin_all(int pin) { rlm_pin_forced = pin ? 1 : 0; }
uint64_t rlm_cells_created(void)  { return rlm_cells_global; }
uint64_t rlm_calls_made(void)     { return rlm_calls_global; }
uint64_t rlm_cache_hits(void)     { return rlm_hits_global; }
uint64_t rlm_rounds_run(void)     { return rlm_rounds_global; }
uint64_t rlm_fixpoints_run(void)  { return rlm_fixp_global; }
uint64_t rlm_blocks_seen(void)    { return rlm_blocks_global; }
uint64_t rlm_copy_nodes(void)     { return rlm_copies_global; }

uint32_t
rlm_ctx_projections(const rlm_ctx_t *ctx)
{
    return ctx == NULL ? 0 : ctx->npids;
}

int
rlm_ctx_relations(const rlm_ctx_t *ctx)
{
    return ctx == NULL ? 0 : ctx->k;
}

VOID_TASK_0(rlm_pregc)
{
    if (!rlm_active) return;
    /* llmsset_count_marked is O(table size) and the counts are only used at
     * the maximum table size, where the valve can fire; below it a collection
     * that recovers little is answered by growing the table, not by us. */
    if (llmsset_get_size(nodes) != llmsset_get_max_size(nodes)) {
        rlm_gc_occupied = 0;
        return;
    }
    rlm_gc_occupied = CALL(llmsset_count_marked, nodes);
}

VOID_TASK_0(rlm_postgc)
{
    if (!rlm_active) return;
    int at_max = llmsset_get_size(nodes) == llmsset_get_max_size(nodes);
    if (!at_max || rlm_gc_occupied == 0) {
        rlm_starved = 0;
        return;
    }
    size_t before = rlm_gc_occupied;
    size_t after  = CALL(llmsset_count_marked, nodes);
    size_t freed  = before > after ? before - after : 0;

    if (freed * 8 < before) {
        if (++rlm_starved >= 2 && !rlm_pin_all) {
            rlm_pin_all = 1;
#ifndef RLM_NO_HRE
            Warning(info, "vset_lddmc/reachdd: collection recovered %zu of %zu "
                          "nodes at the maximum table size (about %"PRIu64" "
                          "residual cells consed); pinning all relations to the "
                          "apply-whole (saturation) schedule for the rest of "
                          "this run.", freed, before, rlm_cells_global);
#endif
        }
    } else {
        rlm_starved = 0;
    }
}

void
rlm_gc_hooks_install(void)
{
    static int installed = 0;
    if (installed) return;
    installed = 1;
    /* sylvan_gc_hook_pregc/postgc append to a LIST, so installing these
     * alongside the BDD back-end's hooks is safe: both run, and each returns
     * immediately unless its own rlm_active/rl_active flag is set. */
    sylvan_gc_hook_pregc(rlm_pregc_CALL);
    sylvan_gc_hook_postgc(rlm_postgc_CALL);
}

/**
 * ------------------------------------------------------------------------
 * The copy-node convention, checked
 * ------------------------------------------------------------------------
 *
 * A copy node has value 0 -- mddnode_makecopy writes right<<1 into a and
 * ((down<<1)|1)<<16 into b, and mddnode_getvalue reads the four bytes at
 * offset 6, which are zero in both -- so a copy node read as an ordinary node
 * at an only-read level is SILENTLY reinterpreted as "the relation reads value
 * 0".  That is an under-approximation with no symptom, which is exactly the
 * failure mode to make loud.
 *
 * The walk is memoised in Sylvan's operation cache on <node, meta>, so
 * re-validating what learning has added to a relation costs only the new
 * nodes.  It also has real teeth on rel_load, which reconstructs a relation
 * from a file another tool may have written.
 */
VOID_TASK_4(rlm_check_copy, MDD, R, MDD, meta, int, lvl, int, m)
{
    if (R == lddmc_false || R == lddmc_true) return;
    if (meta == lddmc_true) return;

    const uint32_t mv = lddmc_getvalue(meta);
    if (mv == RLM_M_END) return;
    if (mv == RLM_M_SKIP) {                 /* the meta advances, R does not  */
        CALL(rlm_check_copy, R, lddmc_getdown(meta), lvl + 1, m);
        return;
    }

    uint64_t seen;
    if (cache_get3(RLM_CHECK_OPID, R, meta, 0, &seen)) return;

    if (lddmc_iscopy(R) && mv != RLM_M_WRITE && mv != RLM_M_ONLYW) {
        static const char *name[6] = { "skip", "read", "write", "only-read",
                                       "only-write", "action" };
        Abort("reachdd/lddmc: relation %d has a copy node at state level %d, "
              "whose meta class is %s (%u).  ReachDDMdd admits copy nodes only "
              "at WRITTEN levels (meta 2 and 4), where they mean "
              "'column = row'; see the copy-node convention in "
              "vset_lddmc_reachdd.h.  Use --saturation=sat.",
              m, lvl, mv < 6 ? name[mv] : "?", mv);
    }

    if (lddmc_iscopy(R)) __sync_fetch_and_add(&rlm_copies_global, 1u);

    /* lddmc_test_ismdd's own invariant: at most one copy node per chain, and
     * always at its head.  rlm_walk relies on it (lddmc_followcopy only
     * inspects the head), so it is checked rather than assumed. */
    const MDD dmeta = lddmc_getdown(meta);
    const int down_lvl = (mv == RLM_M_READ || mv == RLM_M_ACTION) ? lvl : lvl + 1;
    int first = 1;
    for (MDD r = R; r != lddmc_false; r = lddmc_getright(r)) {
        if (lddmc_iscopy(r) && !first) {
            Abort("reachdd/lddmc: relation %d has a copy node that is not the "
                  "head of its value chain at state level %d (meta %u); this "
                  "violates Sylvan's own LDD invariant.", m, lvl, mv);
        }
        first = 0;
        CALL(rlm_check_copy, lddmc_getdown(r), dmeta, down_lvl, m);
    }

    cache_put3(RLM_CHECK_OPID, R, meta, 0, 1);
}

/**
 * ------------------------------------------------------------------------
 * Building the context
 * ------------------------------------------------------------------------
 */

static uint64_t
rlm_keyhash(int32_t dmin, MDD meta)
{
    uint64_t h = 14695981039346656037ULL;
    h = (h ^ (uint64_t)(uint32_t)dmin) * 1099511628211ULL;
    h = (h ^ (uint64_t)meta)           * 1099511628211ULL;
    return h;
}

static uint32_t
rlm_intern(rlm_ctx_t *ctx, int32_t dmin, MDD meta, int *is_new)
{
    const size_t mask = ctx->itab_size - 1;
    size_t i = (size_t)rlm_keyhash(dmin, meta) & mask;
    for (;;) {
        const uint32_t v = ctx->itab[i];
        if (v == 0) {
            if (ctx->npids >= ctx->maxpids) {
                Abort("reachdd/lddmc: more residual projections than the "
                      "bound sum_m |Dom_m| + k + 1 allows; use "
                      "--saturation=sat");
            }
            const uint32_t p = ctx->npids++;
            ctx->itab[i] = p + 1;
            memset(&ctx->pj[p], 0, sizeof(rlm_proj_t));
            ctx->pj[p].dmin = dmin;               /* the key, set eagerly     */
            ctx->pj[p].meta = meta;
            *is_new = 1;
            return p;
        }
        const uint32_t p = v - 1;
        if (ctx->pj[p].dmin == dmin && ctx->pj[p].meta == meta) {
            *is_new = 0;
            return p;
        }
        i = (i + 1) & mask;
    }
}

static size_t
rlm_pow2ge(size_t x)
{
    size_t s = 16;
    while (s < x) s <<= 1;
    return s;
}

/**
 * Decompose one relation's meta into a per-state-level class and suffix, and
 * report how many state levels it covers.  lev_meta[d] is the handle of the
 * meta positioned at state level d -- which for d = topvar is, by hash
 * consing, the identical node to rel->topmeta.
 */
static int
rlm_scan_meta(MDD meta, int n, MDD *lev_meta, uint32_t *lev_cls, MDD *endmeta)
{
    int d = 0;
    for (;;) {
        if (meta == lddmc_true) { *endmeta = meta; return d; }
        const uint32_t v = lddmc_getvalue(meta);
        if (v == RLM_M_ACTION || v == RLM_M_END) { *endmeta = meta; return d; }
        if (d >= n) {
            Abort("reachdd/lddmc: relation meta describes more than %d state "
                  "levels", n);
        }
        lev_meta[d] = meta;
        lev_cls[d]  = v;
        if (v == RLM_M_READ) {
            meta = lddmc_getdown(meta);
            if (meta == lddmc_true || lddmc_getvalue(meta) != RLM_M_WRITE) {
                Abort("reachdd/lddmc: malformed relation meta: a read entry "
                      "at state level %d is not followed by a write entry", d);
            }
        } else if (v == RLM_M_WRITE) {
            Abort("reachdd/lddmc: malformed relation meta: a write entry at "
                  "state level %d without a preceding read entry", d);
        } else if (v != RLM_M_SKIP && v != RLM_M_ONLYR && v != RLM_M_ONLYW) {
            Abort("reachdd/lddmc: unknown relation meta entry %u", v);
        }
        meta = lddmc_getdown(meta);
        d++;
    }
}

rlm_ctx_t *
rlm_ctx_create(int n, const rlm_relspec_t *specs, int k)
{
    rlm_ctx_t *ctx = (rlm_ctx_t*)RTmalloc(sizeof(rlm_ctx_t));
    memset(ctx, 0, sizeof(rlm_ctx_t));
    ctx->n = n;

    ctx->rel       = (rlm_rel_t*)RTmalloc(sizeof(rlm_rel_t) * (k > 0 ? k : 1));
    ctx->entry     = (int*)      RTmalloc(sizeof(int)       * (k > 0 ? k : 1));
    ctx->entry_pid = (uint32_t*) RTmalloc(sizeof(uint32_t)  * (k > 0 ? k : 1));

    MDD      lev_meta[n + 1];
    uint32_t lev_cls[n + 1];

    /* Alg. 6's Reduce at the root: drop R_rho = 0 and W_rho = empty.  A
     * relation that writes nothing is the identity on everything below it
     * (Eq. 4) and contributes no transition at all.  R_rho = 0 is dropped only
     * when the relation CANNOT grow: under learning every relation starts
     * empty, and dropping them here would make the whole operation a no-op
     * that still terminates and still prints plausible numbers. */
    size_t domtotal = 0;
    int kept = 0, learning = 0;
    for (int m = 0; m < k; m++) {
        if (*specs[m].mdd == lddmc_false && specs[m].learn == NULL) continue;
        MDD endmeta;
        const int d = rlm_scan_meta(specs[m].meta, n, lev_meta, lev_cls, &endmeta);
        int any_w = 0, ndom = 0;
        for (int i = 0; i < d; i++) {
            if (lev_cls[i] != RLM_M_SKIP) ndom++;
            if (lev_cls[i] == RLM_M_READ || lev_cls[i] == RLM_M_ONLYW) any_w = 1;
        }
        if (!any_w) continue;             /* Reduce: drop W_rho = emptyset    */

        /* The copy-node convention, before anything is interned. */
        RUN(rlm_check_copy, *specs[m].mdd, specs[m].meta, 0, m);

        ctx->rel[kept].mdd        = specs[m].mdd;
        ctx->rel[kept].meta       = specs[m].meta;
        ctx->rel[kept].topmeta    = specs[m].topmeta;
        ctx->rel[kept].learn      = specs[m].learn;
        ctx->rel[kept].learn_self = specs[m].learn_self;
        if (specs[m].learn != NULL) learning = 1;
        domtotal += (size_t)ndom;
        kept++;
    }
    ctx->k        = kept;
    ctx->learning = learning;

    if (kept == 0) {
        rlm_ctx_destroy(ctx);
        return NULL;
    }

    /* At most sum_m |Dom_m| pairs with a non-empty domain (App. B.6's k(n+1)
     * bound), plus the fully consumed residual.  That last one is normally ONE
     * id for every relation, because lddmc_cube gives them all the identical
     * [5,-1] tail; the + kept covers a hypothetical relation whose meta ends
     * differently, so that the bound is a fact and not an assumption. */
    const size_t maxp = domtotal + (size_t)kept + 1;
    ctx->maxpids   = (uint32_t)maxp;
    ctx->pj        = (rlm_proj_t*)RTmalloc(sizeof(rlm_proj_t) * ctx->maxpids);
    ctx->itab_size = rlm_pow2ge(2 * maxp);
    ctx->itab      = (uint32_t*)RTmalloc(sizeof(uint32_t) * ctx->itab_size);
    memset(ctx->itab, 0, sizeof(uint32_t) * ctx->itab_size);
    ctx->npids     = 0;

    /* Intern each relation's residual projections, deepest suffix first, so
     * that succ is already interned when the pair above it is created.  The
     * chain is NOT truncated at max W: a cross residual is kept even after
     * retirement -- App. B's <1,{},{}> is a transition, not the identity --
     * and its image must still walk the reads and the action level below. */
    for (int m = 0; m < kept; m++) {
        MDD endmeta;
        const int d = rlm_scan_meta(ctx->rel[m].meta, n, lev_meta, lev_cls, &endmeta);

        int is_new;
        /* The fully consumed residual.  endmeta is the shared [5,-1] tail that
         * lddmc_cube gives every relation, so this is one id for all of them. */
        uint32_t cur = rlm_intern(ctx, RLM_INF, endmeta, &is_new);
        if (is_new) {
            ctx->pj[cur].wmin     = RLM_INF;
            ctx->pj[cur].cls      = RLM_M_SKIP;
            ctx->pj[cur].succ     = cur;
            ctx->pj[cur].nextmeta = endmeta;
        }

        for (int l = d - 1; l >= 0; l--) {
            if (lev_cls[l] == RLM_M_SKIP) continue;    /* Th. split: same id  */
            MDD nm = lddmc_getdown(lev_meta[l]);
            if (lev_cls[l] == RLM_M_READ) nm = lddmc_getdown(nm);  /* TWO     */
            const int32_t wm = (lev_cls[l] == RLM_M_READ ||
                                lev_cls[l] == RLM_M_ONLYW)
                             ? (int32_t)l : ctx->pj[cur].wmin;
            const uint32_t p = rlm_intern(ctx, (int32_t)l, lev_meta[l], &is_new);
            if (is_new) {
                ctx->pj[p].wmin     = wm;
                ctx->pj[p].cls      = lev_cls[l];
                ctx->pj[p].succ     = cur;
                ctx->pj[p].nextmeta = nm;
            } else {
                /* dmin and meta are the key; the rest must be functions of it. */
                assert(ctx->pj[p].wmin     == wm);
                assert(ctx->pj[p].cls      == lev_cls[l]);
                assert(ctx->pj[p].succ     == cur);
                assert(ctx->pj[p].nextmeta == nm);
            }
            cur = p;
        }
        assert(ctx->pj[cur].dmin != RLM_INF);       /* W non-empty => Dom too */
        ctx->entry_pid[m] = cur;
        ctx->entry[m]     = ctx->pj[cur].dmin;
        /* rel->topmeta is built by its own lddmc_cube call, but hash consing
         * makes it the IDENTICAL node to the meta suffix at topvar.  The whole
         * interning scheme rests on that, so it is checked rather than argued;
         * and the pinned branch below hands ctx->pj[entry_pid].meta straight to
         * lddmc_relprod_union, where saturation hands rel->topmeta. */
        if (ctx->rel[m].topmeta != lddmc_false &&
            ctx->rel[m].topmeta != ctx->pj[cur].meta) {
            Abort("reachdd/lddmc: relation %d's topmeta is not the meta suffix "
                  "at its entry level; the LDD meta chain is not hash consed "
                  "as assumed", m);
        }
    }

    /* Sort ascending on entry level, so that the relations not yet reached at
     * level l are exactly the suffix beyond first_at[l] -- which is precisely
     * saturation's own representation, and init_least_fixpoint's. */
    for (int i = 1; i < kept; i++) {
        rlm_rel_t r = ctx->rel[i];
        uint32_t  p = ctx->entry_pid[i];
        int       e = ctx->entry[i];
        int j = i - 1;
        while (j >= 0 && ctx->entry[j] > e) {
            ctx->rel[j+1]       = ctx->rel[j];
            ctx->entry[j+1]     = ctx->entry[j];
            ctx->entry_pid[j+1] = ctx->entry_pid[j];
            j--;
        }
        ctx->rel[j+1]       = r;
        ctx->entry[j+1]     = e;
        ctx->entry_pid[j+1] = p;
    }

    ctx->first_at = (int*)RTmalloc(sizeof(int) * (n + 2));
    {
        int m = 0;
        for (int l = 0; l <= n + 1; l++) {
            while (m < kept && ctx->entry[m] < l) m++;
            ctx->first_at[l] = m;
        }
    }

    static volatile uint32_t rlm_next_id = 0;
    ctx->id = __sync_fetch_and_add(&rlm_next_id, 1u);

    return ctx;
}

void
rlm_ctx_destroy(rlm_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->rel       != NULL) RTfree(ctx->rel);
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

static inline int
rlm_lt(const rlm_entry_t *a, const rlm_entry_t *b)
{
    if (a->pid != b->pid) return a->pid < b->pid;
    return a->R < b->R;
}

/**
 * Reduce, including its identification of duplicates, applied *before*
 * consing, so that logically equal lists are physically equal.  m is small
 * (the number of simultaneously live relations), so insertion sort.
 */
static int
rlm_sort_dedup(rlm_entry_t *e, int m)
{
    for (int i = 1; i < m; i++) {
        rlm_entry_t t = e[i];
        int j = i - 1;
        while (j >= 0 && rlm_lt(&t, &e[j])) { e[j+1] = e[j]; j--; }
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
 * cofactors of a chain that is on the refs stack, or roots of relations whose
 * slot the caller has pushed.
 * POSTCONDITION: the caller roots the returned handle immediately.
 */
static MDD
rlm_build(const rlm_entry_t *e, int m)
{
    MDD acc = RLM_EPS;
    lddmc_refs_pushptr(&acc);
    for (int i = m - 1; i >= 0; i--) acc = rlm_cons(e[i].pid, e[i].R, acc);
    lddmc_refs_popptr(1);
    if (m > 0) __sync_fetch_and_add(&rlm_cells_global, (uint64_t)m);
    return acc;
}

/**
 * Sort, drop duplicates, FUSE cells with equal projections, and cons the
 * chain.
 *
 * Equal pid means equal residual projection, so by th:retire
 *   [[<R1,Rd,W>]] v [[<R2,Rd,W>]] = [[<R1 v R2,Rd,W>]]
 * and fusing the two loses nothing.  This is not a tuning choice.  At a
 * written level rlm_split_diag emits the copy node's down AND the explicit
 * write's down under the same pid; when the two differ, dedup (which needs
 * both pid and diagram to match) keeps both and the chain DOUBLES.  It doubles
 * again at the next such level, so without fusion the chain is O(2^span) in a
 * relation's span -- and the per-frame scratch arrays are sized by it.
 * Measured before fusing: span 12 gave a 2048-cell chain, span 16 gave 32768
 * and a 2.1 MB stack frame, and span 18 was a SIGBUS on Lace's 16 MB worker
 * stack with no message.  Copy nodes at written levels are exactly the input
 * class this back-end admits, so the bound matters.
 *
 * A fused diagram is a new node rooted by nobody, so it is pushed and released
 * only once rlm_build has consed it into the chain.
 */
static MDD
rlm_fuse_build(rlm_entry_t *e, int m)
{
    int o = rlm_sort_dedup(e, m);
    int pushed = 0;
    int w = 0;
    for (int i = 0; i < o; i++) {
        if (w > 0 && e[w-1].pid == e[i].pid) {
            /* Nothing allocates between the union and the push. */
            e[w-1].R = lddmc_union(e[w-1].R, e[i].R);
            lddmc_refs_push(e[w-1].R);
            pushed++;
            continue;
        }
        e[w++] = e[i];
    }
    MDD res = rlm_build(e, w);
    if (pushed) lddmc_refs_pop(pushed);
    return res;
}

/**
 * The relations entering at this level join the chain (relations not yet
 * reached are transparent and are the suffix of the entry-sorted array beyond
 * first_at[l]).  Reduce, including dedup, runs here, before consing.
 */
static MDD
rlm_insert_entering(MDD L, const rlm_ctx_t *ctx, int from, int to,
                    rlm_entry_t *buf, MDD *built)
{
    int cnt = 0, pushed = 0;
    for (MDD c = L; c != RLM_EPS; c = rlm_tail(c)) {
        buf[cnt].pid = rlm_pid(c);
        buf[cnt].R   = rlm_res(c);          /* rooted by the caller's L       */
        cnt++;
    }
    for (int i = from; i < to; i++) {
        /* Record exactly the root that goes into the chain, not one read a
         * moment earlier: the caller decides later whether its split is stale
         * by comparing these. */
        const MDD R = *ctx->rel[i].mdd;
        built[i - from] = R;
        if (R == lddmc_false) continue;     /* Reduce, applied dynamically    */
        /* The owner's slot is what roots R, and a learn callback may republish
         * it (with a bigger diagram) at any moment -- after which this
         * snapshot is rooted by nobody and rlm_build's allocation could
         * collect it.  There is no allocation between the read and this push. */
        lddmc_refs_push(R);
        pushed++;
        buf[cnt].pid = ctx->entry_pid[i];
        buf[cnt].R   = R;
        cnt++;
    }
    MDD res = rlm_fuse_build(buf, cnt);
    lddmc_refs_pop(pushed);                 /* no allocation before the caller
                                             * roots the result               */
    return res;
}

/**
 * ------------------------------------------------------------------------
 * The level split (Def. 2 / Eq. 7), m-ary
 * ------------------------------------------------------------------------
 *
 * Set up one cursor per cell for a pass over the level's value chain.  Nothing
 * is built here: every R below is a node that already exists inside a relation
 * diagram, and every cursor is a right-pointer walk inside one.  They need no
 * reference of their own -- each is reachable from a cell of the caller's Lx,
 * which is on the pointer stack, and Sylvan's collector rehashes in place
 * instead of relocating data buckets.
 */
static int
rlm_cursor_init(MDD Lx, const rlm_ctx_t *ctx, uint32_t lvl, rlm_cursor_t *cs)
{
    int nc = 0;
    for (MDD c = Lx; c != RLM_EPS; c = rlm_tail(c)) {
        const uint32_t p = rlm_pid(c);
        const rlm_proj_t *pj = &ctx->pj[p];
        cs[nc].pid = p;
        cs[nc].R   = rlm_res(c);
        cs[nc].cpy = lddmc_false;
        if (pj->dmin != (int32_t)lvl) {
            /* Th. split, last clause: rho|ii = rho, the identical triple AND
             * the identical handle.  Consumes zero relation levels. */
            assert(pj->dmin > (int32_t)lvl);
            cs[nc].cls  = RLM_M_SKIP;
            cs[nc].spid = p;
        } else {
            cs[nc].cls  = pj->cls;
            cs[nc].spid = pj->succ;
            /* A live cell is never at the action level or past the end: those
             * imply an empty remaining W, which Reduce has already retired. */
            assert(pj->cls == RLM_M_READ || pj->cls == RLM_M_ONLYR ||
                   pj->cls == RLM_M_ONLYW);
            if (pj->cls == RLM_M_ONLYW) {
                /* The copy node is the head of the chain and there is at most
                 * one (rlm_check_copy enforces both).  It is diagonal at every
                 * value, so it is lifted out of the walk here. */
                cs[nc].cpy = lddmc_followcopy(cs[nc].R);
                if (cs[nc].cpy != lddmc_false) cs[nc].R = lddmc_getright(cs[nc].R);
            } else if (lddmc_iscopy(cs[nc].R)) {
                /* The static rlm_check_copy walk cannot see what learning adds
                 * after the context was built, so the convention is also
                 * enforced here -- once per cell per round, off the per-value
                 * path.  Every relation node the split ever inspects becomes a
                 * cursor at its own level, so this is a complete dynamic check
                 * of the diagonal path. */
                Abort("reachdd/lddmc: a copy node appeared at state level %u of "
                      "a relation whose meta class there is %s; ReachDDMdd "
                      "admits copy nodes only at WRITTEN levels (meta 2 and 4). "
                      "Use --saturation=sat.", lvl,
                      pj->cls == RLM_M_READ ? "read (1)" : "only-read (3)");
            }
        }
        nc++;
    }
    return nc;
}

/** Advance a value-chain cursor to the first node with value >= v. */
static inline MDD
rlm_seek(MDD r, uint32_t v)
{
    while (r != lddmc_false && lddmc_getvalue(r) < v) r = lddmc_getright(r);
    return r;
}

/**
 * The diagonal of Eq. 7 at value v, for every cell, with the cursors advanced
 * past v as a side effect.  Returns the number of entries written to buf.
 *
 *   skip (l not in Dom) : rho|vv = rho, the identical handle; no cross.
 *   only-read (meta 3)  : rho|vv = <succ, down(node_v)> when v is in the read
 *                         chain, and the cell dies for this value otherwise;
 *                         no cross, by read-closure.
 *   only-write (meta 4) : m EQUAL rows.  Diagonal at v is the explicit write
 *                         of v, if any, plus the copy node's down, if any.
 *   read-write (meta 1) : match v against the read chain; the matched node's
 *                         down is the WRITE chain at the same state level.
 *                         Diagonal is its write of value v, if any, plus its
 *                         copy node's down, if any.
 */
static int
rlm_split_diag(rlm_walk_env_t *env, const rlm_ctx_t *ctx, uint32_t v)
{
    rlm_cursor_t *cs = env->cs;
    rlm_entry_t  *e  = env->buf;
    int cnt = 0;

    for (int i = 0; i < env->nc; i++) {
        const uint32_t sp = cs[i].spid;
        switch (cs[i].cls) {
        case RLM_M_SKIP:
            e[cnt].pid = cs[i].pid;
            e[cnt].R   = cs[i].R;
            cnt++;
            continue;                       /* never retired, never zero      */

        case RLM_M_ONLYR: {
            cs[i].R = rlm_seek(cs[i].R, v);
            if (cs[i].R == lddmc_false || lddmc_getvalue(cs[i].R) != v) break;
            const MDD Rd = lddmc_getdown(cs[i].R);
            cs[i].R = lddmc_getright(cs[i].R);
            if (Rd == lddmc_false) break;
            if (ctx->pj[sp].wmin == RLM_INF) break;   /* Th. retire, EXACT    */
            e[cnt].pid = sp; e[cnt].R = Rd; cnt++;
            break;
        }

        case RLM_M_ONLYW: {
            const int live = ctx->pj[sp].wmin != RLM_INF;
            if (live && cs[i].cpy != lddmc_false) {
                e[cnt].pid = sp; e[cnt].R = cs[i].cpy; cnt++;
            }
            cs[i].R = rlm_seek(cs[i].R, v);
            if (cs[i].R == lddmc_false || lddmc_getvalue(cs[i].R) != v) break;
            const MDD Rd = lddmc_getdown(cs[i].R);
            cs[i].R = lddmc_getright(cs[i].R);
            if (!live || Rd == lddmc_false) break;
            e[cnt].pid = sp; e[cnt].R = Rd; cnt++;
            break;
        }

        case RLM_M_READ: {
            cs[i].R = rlm_seek(cs[i].R, v);
            if (cs[i].R == lddmc_false || lddmc_getvalue(cs[i].R) != v) break;
            const MDD W = lddmc_getdown(cs[i].R);   /* the WRITE chain, same
                                                     * state level            */
            cs[i].R = lddmc_getright(cs[i].R);
            if (ctx->pj[sp].wmin == RLM_INF) break; /* Th. retire, EXACT      */
            const MDD c = lddmc_followcopy(W);      /* copy: column = row     */
            if (c != lddmc_false) { e[cnt].pid = sp; e[cnt].R = c; cnt++; }
            const MDD w = lddmc_follow(W, v);       /* the explicit write of v */
            if (w != lddmc_false) { e[cnt].pid = sp; e[cnt].R = w; cnt++; }
            break;
        }

        default:
            assert(0);
        }
    }
    return cnt;   /* the caller sorts, fuses and conses: rlm_fuse_build */
}

/**
 * ------------------------------------------------------------------------
 * ReachDDMdd (Alg. 6)
 * ------------------------------------------------------------------------
 */
TASK_DECL_4(MDD, rlm_reach, MDD, MDD, uint32_t, rlm_ctx_t*);

/**
 * Alg. 6 lines lm-diag/lm-rebuild: the pass over one level's value chain.
 *
 * s is a node of the level's chain, env->cs the per-cell cursors positioned at
 * or before value(s), and lvl the state level.  For each value in ascending
 * order this builds the diagonal residual list and recurses one level down;
 * the result chain is rebuilt on the way out, which is why the walk is a
 * recursion over `right` rather than a loop -- lddmc_makenode needs its tail
 * first.  It is the same shape as lddmc_relprod's and lddmc_go_sat's chain
 * recursions, and it is CALLed, never SPAWNed, so the single mutable cursor
 * array is visited strictly left to right, exactly once per value.
 *
 * REFERENCES.  down must be pushed before the right-recursion, which
 * allocates; L_v must be pushed across the recursive rlm_reach.  Every R that
 * goes into L_v is a cofactor of a cell of the caller's Lx, which the caller
 * keeps on the pointer stack for the whole pass, so nothing else needs rooting.
 */
TASK_4(MDD, rlm_walk, MDD, s, rlm_walk_env_t*, env, uint32_t, lvl, rlm_ctx_t*, ctx)
{
    if (s == lddmc_false) return lddmc_false;
    /* An LDD is quasi-reduced, so a level's chain never ends in lddmc_true. */
    assert(s != lddmc_true);

    const uint32_t v = lddmc_getvalue(s);
    /* No copy nodes in a state set, ever. */
    assert(!lddmc_iscopy(s));

    MDD L_v = rlm_fuse_build(env->buf, rlm_split_diag(env, ctx, v));
    lddmc_refs_push(L_v);
    MDD down = CALL(rlm_reach, lddmc_getdown(s), L_v, lvl + 1, ctx);
    lddmc_refs_pop(1);                                  /* L_v               */

    lddmc_refs_push(down);
    MDD right = CALL(rlm_walk, lddmc_getright(s), env, lvl, ctx);
    lddmc_refs_pop(1);                                  /* down              */

    return lddmc_makenode(v, down, right);
}

/** The union of a level node's down-edges: the source of an only-write row. */
TASK_1(MDD, rlm_downunion, MDD, s)
{
    MDD acc = lddmc_false;
    lddmc_refs_pushptr(&acc);
    for (; s != lddmc_false; s = lddmc_getright(s)) {
        acc = CALL(lddmc_union, acc, lddmc_getdown(s));
    }
    lddmc_refs_popptr(1);
    return acc;
}

/**
 * Alg. 6 line lm-cross: the off-diagonal blocks of every WRITING cell, as one
 * delta to be unioned into the level.
 *
 * Image is Eq. 6, and on a read-closed relation Th. image makes it exactly
 * lddmc_relprod over the residual's own meta suffix: levels outside it keep
 * their value (the identity Eq. 4 demands, supplied by relprod's m_val == -1
 * terminal), levels inside are matched, written and quantified, and the action
 * level is unioned away for free.
 *
 * The two writing classes are enumerated differently, and that is the whole
 * point of the m-ary case:
 *
 *   only-write : the m rows are EQUAL, so the m^2 blocks collapse to ONE image
 *                per written value, over the union of all the sources.  Using
 *                all sources rather than the i != j ones is sound because the
 *                extra term is Image(<S>_j, rho|jj), which the recursive call
 *                at value j already adds -- the paper's own justification for
 *                this reorganisation.
 *   read-write : merge-join the read chain against the state's chain, then for
 *                each matched read walk its write chain.  Only the (read,
 *                write) pairs actually present in the diagram are visited, and
 *                the diagonal ones are left to the recursive call.
 *
 * REFERENCES.  cur and Lx are the caller's pushptr'd variables and are not
 * reassigned while this runs, so every raw walk handle below (s, c, r, w and
 * their downs) is reachable from a rooted root.  base, delta, img and piece
 * are new nodes and each has its own slot.
 */
TASK_4(MDD, rlm_cross, MDD, cur, MDD, Lx, uint32_t, lvl, rlm_ctx_t*, ctx)
{
    MDD delta = lddmc_false, base = lddmc_false, img = lddmc_false;
    MDD piece = lddmc_false;
    lddmc_refs_pushptr(&delta);
    lddmc_refs_pushptr(&base);
    lddmc_refs_pushptr(&img);
    lddmc_refs_pushptr(&piece);

    uint64_t blocks = 0;

    for (MDD c = Lx; c != RLM_EPS; c = rlm_tail(c)) {
        const rlm_proj_t *pj = &ctx->pj[rlm_pid(c)];
        /* A = <rho in L | l in W_rho>.  THE GUARD: by Eq. 7 case 3 a
         * non-writer's off-diagonal blocks are zero, and without this test an
         * only-read relation would be allowed to change x_l for free -- a
         * sound-looking over-approximation. */
        if (pj->wmin != (int32_t)lvl) continue;
        assert(pj->dmin == (int32_t)lvl);            /* W is a subset of Dom  */
        const MDD R  = rlm_res(c);
        const MDD nm = pj->nextmeta;

#if RLM_CROSS_WHOLE
        /* Debug setting: let lddmc_relprod enumerate this cell's whole block
         * matrix at this level, diagonal included.  Sound, redundant, and not
         * Alg. 6 as printed. */
        (void)nm;
        img   = CALL(lddmc_relprod, cur, R, pj->meta);
        delta = CALL(lddmc_union, delta, img);
        img   = lddmc_false;
        blocks++;
#else
        if (pj->cls == RLM_M_ONLYW) {
            if (base == lddmc_false) base = CALL(rlm_downunion, cur);
            for (MDD r = R; r != lddmc_false; r = lddmc_getright(r)) {
                if (lddmc_iscopy(r)) continue;       /* diagonal; see the walk */
                const uint32_t j = lddmc_getvalue(r);
                img   = CALL(lddmc_relprod, base, lddmc_getdown(r), nm);
                piece = lddmc_makenode(j, img, lddmc_false);
                img   = lddmc_false;
                delta = CALL(lddmc_union, delta, piece);
                piece = lddmc_false;
                blocks++;
            }
        } else {
            assert(pj->cls == RLM_M_READ);
            MDD s = cur, r = R;
            while (s != lddmc_false && r != lddmc_false) {
                const uint32_t vs = lddmc_getvalue(s), vr = lddmc_getvalue(r);
                if (vs < vr) { s = lddmc_getright(s); continue; }
                if (vs > vr) { r = lddmc_getright(r); continue; }
                const MDD src = lddmc_getdown(s);
                for (MDD w = lddmc_getdown(r); w != lddmc_false;
                     w = lddmc_getright(w)) {
                    if (lddmc_iscopy(w)) continue;   /* copy: column = row     */
                    const uint32_t j = lddmc_getvalue(w);
                    if (j == vs) continue;           /* diagonal               */
                    img   = CALL(lddmc_relprod, src, lddmc_getdown(w), nm);
                    piece = lddmc_makenode(j, img, lddmc_false);
                    img   = lddmc_false;
                    delta = CALL(lddmc_union, delta, piece);
                    piece = lddmc_false;
                    blocks++;
                }
                s = lddmc_getright(s);
                r = lddmc_getright(r);
            }
        }
#endif
    }

    if (blocks != 0) __sync_fetch_and_add(&rlm_blocks_global, blocks);
    lddmc_refs_popptr(4);          /* piece, img, base, delta                 */
    return delta;
}

/**
 * Expand every relation entering at this level over cur.  The callback reads
 * the level node, projects it onto the relation's read projection and calls
 * LTSmin's expand_cb, which republishes the relation's root under its own
 * compare-and-swap; we simply re-read the slot afterwards.  See
 * vset_sylvan_reachdd.c for why this is the right level and why the operation
 * cache needs no invalidation.
 */
static void
rlm_learn_level(rlm_ctx_t *ctx, int from, int to, MDD cur)
{
    for (int i = from; i < to; i++) {
        if (ctx->rel[i].learn == NULL) continue;
        ctx->rel[i].learn(ctx->rel[i].learn_self, cur);
    }
}

/**
 * S is a quasi-reduced LDD over state levels lvl..n-1, L the chain of the
 * residuals of the relations that have already entered, and lvl the level.
 * The relations that have NOT entered are transparent and are the suffix of
 * ctx->rel beyond ctx->first_at[lvl].
 *
 * PHASE SEPARATION.  Alg. 6 interleaves its diagonal and cross statements in
 * one i,j loop (Gauss-Seidel); this runs all the diagonals, then all the
 * crosses against the post-diagonal level, then one union.  Both are chaotic
 * iterations of the same monotone statements with the same exit test over a
 * finite lattice, so both reach the same least fixpoint above the same start;
 * only the round count can differ.  That is the same argument
 * vset_sylvan_reachdd.c makes for Alg. 5 against Alg. 4, and it is stated
 * rather than assumed.
 */
TASK_IMPL_4(MDD, rlm_reach, MDD, S, MDD, L, uint32_t, lvl, rlm_ctx_t*, ctx)
{
    __sync_fetch_and_add(&rlm_calls_global, 1u);       /* diagnostic only */

    /* line lm-zero */
    if (S == lddmc_false) return lddmc_false;

    /* Reduce is performed by the caller, before consing L. */

    const int pending = ctx->first_at[lvl];

    /* line lm-empty.  L = eps is "the chain is empty AND nothing is pending";
     * testing only the chain returns S unchanged at any level in a gap between
     * one relation's retirement and the next one's entry, and silently loses
     * every transition below. */
    if (L == RLM_EPS && pending == ctx->k) return S;

    /* An LDD is quasi-reduced, so lddmc_true occurs only at lvl == n, where
     * every relation has retired and the terminal above has already fired.
     * Kept as a guard, not as a shortcut -- and the Boolean version's
     * "returned a full cube it never expanded for" hole does not exist here,
     * which is why there is no rlm_cube_waivers(). */
    if (S == lddmc_true) {
        assert(lvl == (uint32_t)ctx->n && L == RLM_EPS);
        return lddmc_true;
    }

    /* Alg. 6's havoc terminal is vacuous for LDDs and is deliberately absent;
     * see point 4 in the file header. */

    MDD res;
    const uint64_t key3 = ((uint64_t)ctx->id << 32) | (uint64_t)lvl;
    if (cache_get3(RLM_OPID, S, L, key3, &res)) {
        __sync_fetch_and_add(&rlm_hits_global, 1u);    /* diagnostic only */
        return res;
    }

    /* Both key components stay rooted all the way to cache_put3: if the
     * argument chain were collected before the put, its bucket could be reused
     * and we would store a key naming an unrelated node. */
    lddmc_refs_push(S);                                            /* (1) */
    lddmc_refs_push(L);                                            /* (2) */

    /* Cooperate with a collection another worker has started, as every Sylvan
     * operation does at the same point.  Both arguments are rooted above, and
     * nothing else of this frame is live yet.  It is defensive rather than
     * necessary under the sequential schedule -- which never SPAWNs, so the
     * only worker that can start a collection while this frame is live is this
     * one, from inside rlm_cons or a sub-operation -- but it costs one
     * predictable branch and it is what makes the file safe to SPAWN from
     * later. */
    sylvan_gc_test();

    /* Cor. th:sat-special: a pinned relation is applied whole at its entry
     * level, inside this level's fixpoint -- which is exactly what
     * lddmc_go_sat does.  Pinning is App. B.6's safety valve and stops
     * residual cells being allocated at all. */
    const int pin       = rlm_pinning();
    const int enter_end = ctx->first_at[lvl + 1];
    const int nenter    = enter_end - pending;
    const int npin      = pin ? nenter : 0;
    const int nent      = pin ? 0 : nenter;
    const int learn_here = ctx->learning && nent != 0;

    /* One fixed bound for every scratch array, because the split may be built
     * more than once.  Reduce and dedup only ever shorten the chain, so it
     * never exceeds |L| + nent; one cell contributes at most two diagonal
     * entries at a value (an explicit write plus a copy node).
     *
     * THESE STAY ON THE C STACK, for the reason measured and written out in
     * vset_sylvan_reachdd.c: Lace's per-worker scratch arena cannot hold
     * anything that must survive a decision diagram operation. */
    const int cap = rlm_len(L) + nent;
    rlm_cursor_t cursors[cap + 1];
    rlm_entry_t  ebuf[2 * cap + 2];
    MDD          built[nenter + 1];

    MDD Lx = L, cur = S, prev = lddmc_false, delta = lddmc_false;
    lddmc_refs_pushptr(&Lx);                                       /* (3) */
    lddmc_refs_pushptr(&cur);                                      /* (4) */
    lddmc_refs_pushptr(&prev);                                     /* (5) */
    lddmc_refs_pushptr(&delta);                                    /* (6) */

    rlm_walk_env_t env = { cursors, 0, ebuf };
    int nwriters = 0, stale = 1;

    __sync_fetch_and_add(&rlm_fixp_global, 1u);        /* diagnostic only */
    for (;;) {
        __sync_fetch_and_add(&rlm_rounds_global, 1u);  /* diagnostic only */

        if (learn_here) {
            /* On-the-fly learning, at the entry level, inside the level
             * fixpoint, BEFORE the split is (re)built -- so that the node this
             * call finally returns is one every entering relation has been
             * expanded over.  That is invariant (E). */
            rlm_learn_level(ctx, pending, enter_end, cur);
            for (int i = 0; !stale && i < nent; i++) {
                if (built[i] != *ctx->rel[pending + i].mdd) stale = 1;
            }
        }
        if (stale) {
            Lx = (nent == 0) ? L
                             : rlm_insert_entering(L, ctx, pending, enter_end,
                                                   ebuf, built);
            nwriters = 0;
            for (MDD c = Lx; c != RLM_EPS; c = rlm_tail(c)) {
                if (ctx->pj[rlm_pid(c)].wmin == (int32_t)lvl) nwriters++;
            }
            stale = 0;
        }

        if (!learn_here && nwriters == 0 && npin == 0) {
            /* lines lm-transparent: A = eps.  The locality shortcut: no loop,
             * no image.  Every value's subtree evolves independently, so one
             * pass is the fixpoint. */
            env.nc = rlm_cursor_init(Lx, ctx, lvl, cursors);
            cur = CALL(rlm_walk, cur, &env, lvl, ctx);
            break;
        }

        prev = cur;

        env.nc = rlm_cursor_init(Lx, ctx, lvl, cursors);
        cur = CALL(rlm_walk, cur, &env, lvl, ctx);             /* lm-diag  */

        if (nwriters != 0) {
            delta = CALL(rlm_cross, cur, Lx, lvl, ctx);        /* lm-cross */
            cur   = CALL(lddmc_union, cur, delta);
            delta = lddmc_false;                    /* release, for gc       */
        }

        if (npin != 0) {
            /* Cor. th:sat-special: learn, then apply whole, per relation --
             * which is lddmc_go_sat's inner loop, statement for statement.
             * The relation root is copied into a local and pushed: the owner's
             * slot is what roots it, and a callback may republish that slot
             * while lddmc_relprod_union runs. */
            for (int i = pending; i < enter_end; i++) {
                if (ctx->rel[i].learn != NULL) {
                    ctx->rel[i].learn(ctx->rel[i].learn_self, cur);
                }
                const MDD Ri = *ctx->rel[i].mdd;
                lddmc_refs_push(Ri);
                cur = CALL(lddmc_relprod_union, cur, Ri,
                           ctx->pj[ctx->entry_pid[i]].meta, cur);
                lddmc_refs_pop(1);                  /* Ri                    */
            }
        }

        if (cur == prev) break;
    }

    res = cur;
    cache_put3(RLM_OPID, S, L, key3, res);

    lddmc_refs_popptr(4);       /* delta, prev, cur, Lx                       */
    lddmc_refs_pop(2);          /* L, S                                       */
    return res;
}

MDD
rlm_run(rlm_ctx_t *ctx, MDD src)
{
    if (ctx == NULL) return src;
    rlm_active = 1;
    MDD res = RUN(rlm_reach, src, RLM_EPS, 0, ctx);
    rlm_active = 0;
    return res;
}
