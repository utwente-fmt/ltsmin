/**
 * \file vset_lddmc_reachdd.h
 * \brief ReachDDMdd (Alg. 6): symbolic reachability from a list of local
 *        relations, without merging them, for the Sylvan LDD back-end.
 *
 * The m-ary companion of vset_sylvan_reachdd.h.  Alg. 6 is the form the
 * correctness proof is actually carried out for; Alg. 4, the Boolean case, is
 * its corollary.  The operation descends the state diagram and all k local
 * relations simultaneously and splits every relation at every level of its
 * span into the m x m block matrix its own projections dictate (Def. 2 /
 * Eq. 7), instead of applying it whole at its entry level the way saturation
 * does.
 *
 * This header is the interface between vset_lddmc.c, which owns
 * struct vector_relation, and vset_lddmc_reachdd.c, which owns the operation.
 *
 * WHAT A PROJECTION IS, FOR LDDs.  Not a variable set: LTSmin's LDD relations
 * carry a POSITIONAL "meta" description, a chain over
 *
 *     0 = skip, 1 = read, 2 = write, 3 = only-read, 4 = only-write,
 *     5 = action label, (uint32_t)-1 = end,
 *
 * one entry per state level except a read-and-written level, which takes the
 * two entries 1,2.  So Rd_rho and W_rho are not passed separately: the meta IS
 * the pair, the entry at a level IS the level's class, and the suffix of the
 * meta from level l on IS the residual projection <Rd cap [l,n), W cap [l,n)>.
 * Because lddmc_cube builds the chain bottom-up out of the hash-consing
 * lddmc_makenode, two relations with the same residual projection have the
 * BIT-IDENTICAL suffix handle, which interns the residual projection for free
 * and doubles as lddmc_relprod's meta argument.
 *
 * READ-CLOSURE (Eq. 5) IS POSITIONAL AND AUTOMATIC.  An only-read level is one
 * meta entry and one relation level that serves as read and write at once:
 * lddmc_relprod's only-read branch emits lddmc_makenode(value(n_set), ...),
 * i.e. it writes back the value it read.  A non-identity at an only-read level
 * is not expressible.  So Th. image applies and
 * Image(S, <p,R>) = lddmc_relprod(S, R, meta(p)) exactly, with no new
 * operation and no cur_is_next conjunct of the kind the BDD back-end needs.
 *
 * THE COPY-NODE CONVENTION (the paper's stated open problem), enforced by
 * rlm_ctx_create:
 *
 *     A copy node may occur ONLY at a level the relation WRITES -- a meta
 *     entry of 2 (the write half of a read-and-written pair) or 4 (only
 *     write).  It is forbidden at meta 1 (the read half), meta 3 (only-read)
 *     and meta 5 (the action level).
 *
 * {2,4} is the maximal set on which all three existing consumers already
 * agree: vset_lddmc.c's rel_add_act builds the strict subset {4} (it drops the
 * caller's copy flag at meta 2 and says "not supported yet for read levels" at
 * meta 1 and 3), lddmc_relprod accepts the superset {1,2,3,4}, and
 * src/ldd2bdd/ldd2bdd.c accepts exactly {2,4} while asserting against
 * {1,3,5}.  Adopting it therefore changes no existing code and keeps the
 * LDD-to-BDD bridge working.  Denotationally a copy at a written level forces
 * the column index to equal the row index, so it becomes a purely DIAGONAL
 * cell -- work moved out of Image and into the recursive call.  A copy at a
 * read level would instead widen the row index, which is the case this
 * convention forbids: cross residuals go to lddmc_relprod (which handles
 * copies at 1,2,3,4) while diagonal residuals go to the split, so admitting a
 * read-level copy would make the same relation node mean one thing on one path
 * and another on another.  rlm_ctx_create walks every relation against its
 * meta and Aborts, naming the relation, the state level and the meta class.
 */
#ifndef VSET_LDDMC_REACHDD_H
#define VSET_LDDMC_REACHDD_H

#include <stdint.h>

#include <sylvan.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One local relation, as ReachDDMdd needs it.
 *
 * Everything is read off the meta: the projections, the entry level, the class
 * of every level, the retirement level and lddmc_relprod's argument.  There is
 * no bit set and no r_proj/w_proj anywhere in the operation.
 */
typedef struct {
    /**
     * Pointer to the relation's LIVE root, not a copy of it.  The slot must be
     * lddmc_protect()ed by its owner for the lifetime of the context, which is
     * what makes it safe to read at any moment: vset_lddmc.c's rel_update
     * publishes a grown diagram there under a compare-and-swap, so the value
     * only ever gets bigger and is rooted from the moment it lands.
     */
    MDD    *mdd;
    MDD     meta;      /**< rel->meta: the meta chain from state level 0     */
    MDD     topmeta;   /**< rel->topmeta: the same chain from rel->topvar    */

    /**
     * On-the-fly transition learning, or NULL when R_rho is already complete.
     *
     * ReachDDMdd calls learn(learn_self, S) at rho's ENTRY level (min Dom_rho)
     * on the level node it is about to descend, and then re-reads *mdd.  That
     * is the same place, and the same argument, as saturation's own expand
     * call in lddmc_go_sat: Rd_rho is a subset of Dom_rho, so every read level
     * lies at or below the entry level and lddmc_project(S, topread) is exact
     * there.
     *
     * CONTRACT: when learn returns, *mdd must contain every transition of the
     * true relation whose source projects into pi_Rd(S), and those sources
     * must be recorded as explored, monotonically, for the rest of the run.
     * This is exactly LTSmin's expand_cb contract.  See the "Learning inside
     * the operation" section of vset_sylvan_reachdd.c for why the operation
     * cache needs no invalidation; the argument is the same here, and the one
     * hole it names (Alg. 4 line 5, returning an unexpanded full cube) does
     * not even exist for LDDs -- see rlm_reach.
     *
     * When learn != NULL, R_rho = 0 is not a permanent property, so Reduce at
     * the root does not drop the relation.
     */
    void  (*learn)(void *self, MDD S);
    void   *learn_self;
} rlm_relspec_t;

typedef struct rlm_ctx rlm_ctx_t;

/**
 * \brief Intern the residual projections of the given relations and check the
 *        copy-node convention.
 *
 * n is the length of the state vector (LDD levels 0..n-1), NOT a bit count.
 *
 * Relations with an empty write projection are dropped here, and so are
 * relations with an empty diagram that cannot grow (Alg. 6's Reduce at the
 * root).  Returns NULL when nothing is left, in which case the fixpoint is the
 * identity.  A relation with a learn callback is NEVER dropped for being
 * empty: under learning every relation starts out empty.
 *
 * Aborts if any relation violates the copy-node convention documented at the
 * top of this file.
 *
 * The returned context must not outlive the relations it was built from.
 */
extern rlm_ctx_t *rlm_ctx_create(int n, const rlm_relspec_t *specs, int k);

extern void rlm_ctx_destroy(rlm_ctx_t *ctx);

/**
 * \brief Run ReachDDMdd: returns src . [[L]]^*.
 *
 * Must be called from outside a Lace task (it starts one).
 *
 * rlm_run uses the sequential schedule of Alg. 6.  It is not single threaded:
 * the Sylvan sub-operations it calls (lddmc_relprod, lddmc_union) fan out over
 * every Lace worker by themselves.
 *
 * THERE IS DELIBERATELY NO rlm_run_par.  The parallel schedule of Alg. 5 is
 * NOT implemented for this back-end; vset_lddmc.c's set_reach_dd_par calls
 * rlm_run and says so.  What is missing is specific: Alg. 5 would spawn one
 * level's per-value recursions against each other, and rlm_walk carries a
 * single mutable cursor array per level which its pass advances strictly left
 * to right, so a spawned right-recursion would race on it.  A parallel
 * schedule therefore needs a per-value copy of the cursors, or the cursors
 * moved into the residual chain where the cache key can see them -- a design
 * change, not a scheduling flag.
 */
extern MDD rlm_run(rlm_ctx_t *ctx, MDD src);

/**
 * \brief Install the pre/post garbage collection hooks of App. B.6.
 *
 * Idempotent, and independent of the BDD back-end's hooks: Sylvan's pre/post
 * hooks are lists, so both sets run and each returns at once unless its own
 * back-end is the active one.
 *
 * The hooks measure what a collection recovered and, when the unique table is
 * at its maximum size and a collection twice running frees less than an
 * eighth, pin every relation to the apply-whole schedule of
 * Cor. th:sat-special for the rest of the run -- which is saturation, computed
 * by the same operation, and which stops allocating residual cells.
 */
extern void rlm_gc_hooks_install(void);

/**
 * \brief Force (1) or unforce (0) the apply-whole schedule of
 *        Cor. th:sat-special for every relation.  For debugging: with
 *        everything pinned Alg. 6 performs exactly the relation applications
 *        lddmc_go_sat performs.
 */
extern void rlm_set_pin_all(int pin);

/** \brief Number of residual cells consed so far (diagnostic). */
extern uint64_t rlm_cells_created(void);

/** \brief Number of ReachDDMdd recursion entries so far (diagnostic). */
extern uint64_t rlm_calls_made(void);

/** \brief Number of those answered from the operation cache (diagnostic). */
extern uint64_t rlm_cache_hits(void);

/**
 * \brief How many times the level-fixpoint loop body has run, summed over
 *        every level (diagnostic).
 */
extern uint64_t rlm_rounds_run(void);

/** \brief How many calls reached that loop, i.e. level fixpoints run. */
extern uint64_t rlm_fixpoints_run(void);

/**
 * \brief Off-diagonal blocks enumerated, summed over every round of every
 *        level (diagnostic).
 *
 * This is the m^2 term the paper claims LocalReach avoids paying in full: a
 * skipped relation contributes nothing, an only-read relation nothing, an
 * only-written relation one block per written value, and only a genuinely
 * read-and-written level can contribute more -- and then only for the
 * (read, write) pairs actually present in the diagram.
 */
extern uint64_t rlm_blocks_seen(void);

/**
 * \brief Copy nodes seen by the convention check, at written levels (meta 2
 *        and 4), counted once per <node, meta> pair (diagnostic).
 *
 * Zero means the run did not exercise copy nodes at all, which is the normal
 * state of affairs without regrouping: pins2pins-group.c is the only producer
 * of a non-NULL cpy vector in the tree.
 */
extern uint64_t rlm_copy_nodes(void);

/** \brief Number of interned residual projections in this context. */
extern uint32_t rlm_ctx_projections(const rlm_ctx_t *ctx);

/** \brief Number of relations left after Reduce at the root. */
extern int rlm_ctx_relations(const rlm_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // VSET_LDDMC_REACHDD_H
