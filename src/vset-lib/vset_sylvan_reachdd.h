/**
 * \file vset_sylvan_reachdd.h
 * \brief ReachDD (Alg. 4): symbolic reachability from a list of local
 *        relations, without merging them, for the Sylvan BDD back-end.
 *
 * The operation descends the state diagram and all k local relations
 * simultaneously, splitting every relation at every level of its span into the
 * block matrix its own projections dictate (Def. 2 / Eq. 7), instead of
 * applying it whole at its entry level the way saturation does.
 *
 * This header is the interface between vset_sylvan.c, which owns
 * struct vector_relation, and vset_sylvan_reachdd.c, which owns the
 * operation.  A local relation (Def. 1) is a triple <R_rho, Rd_rho, W_rho>;
 * everything the operation needs of it is in rl_relspec_t.
 *
 * Levels are BDD *state bit* levels 0..n-1 with n = dom->totalbits; the
 * unprimed variable of level i is 2i and the primed one 2i+1.
 */
#ifndef VSET_SYLVAN_REACHLIST_H
#define VSET_SYLVAN_REACHLIST_H

#include <stdint.h>

#include <sylvan.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One local relation, as ReachDD needs it.
 *
 * dombits and wbits are level bit sets over 0..n-1, with (n+63)/64 words each:
 * Dom_rho = Rd_rho | W_rho (LTSmin's a_proj) and W_rho (LTSmin's w_proj),
 * expanded from state-vector slots to state bits.  Rd_rho itself is never
 * needed: Eq. 7 tests W, retirement tests W, the havoc terminal quantifies W,
 * and by Lemma th:image the image of a *read-closed* relation quantifies and
 * renames over Dom -- which is exactly what sylvan_relnext(.,.,all_variables)
 * does.  LTSmin's relations are read-closed: rel_add_cpy and rel_add_act
 * conjoin rel->cur_is_next = AND_{i in Rd\W}(x_i <-> x_i') into every
 * transition they add.
 */
typedef struct {
    BDD             bdd;            /**< R_rho                                */
    BDD             all_variables;  /**< the (x,x') cube over Dom_rho         */
    const uint64_t *dombits;        /**< Dom_rho as a level bit set           */
    const uint64_t *wbits;          /**< W_rho as a level bit set             */

    /**
     * On-the-fly transition learning, or NULL when R_rho is already complete.
     *
     * ReachDD calls learn(learn_self, S) at rho's ENTRY level, on the node it
     * is about to return there, and takes the returned handle as the new
     * R_rho.  entry(rho) = min Dom_rho and Rd_rho is a subset of Dom_rho, so
     * every read level lies at or below the entry level and the projection of
     * S onto Rd_rho is exact -- the prefix the recursion has forgotten is
     * outside Dom_rho and cannot change it.  That is the same place, and the
     * same argument, as saturation's own expand call.
     *
     * CONTRACT, on which the soundness of the whole schedule rests: when
     * learn returns, R_rho must contain EVERY transition of the true relation
     * whose source projects into pi_Rd(S), and those sources must be recorded
     * as explored, monotonically, for the rest of the run.  This is exactly
     * LTSmin's expand_cb contract.  See the "Learning inside the operation"
     * section of vset_sylvan_reachdd.c for why nothing else is needed -- in
     * particular why the operation cache needs no invalidation.
     *
     * When learn != NULL, R_rho = 0 is not a permanent property, so Reduce at
     * the root does not drop the relation.
     */
    BDD           (*learn)(void *self, BDD S);
    void           *learn_self;
} rl_relspec_t;

typedef struct rl_ctx rl_ctx_t;

/**
 * \brief Intern the residual projections of the given relations (App. B.6).
 *
 * Relations with an empty write projection are dropped here, and so are
 * relations with an empty diagram that cannot grow (Alg. 4 line 3, Reduce, at
 * the root).  Returns NULL when nothing is left, in which case the fixpoint is
 * the identity.  A relation with a learn callback is NEVER dropped for being
 * empty: under learning every relation starts out empty.
 *
 * The returned context must not outlive the relations it was built from.
 */
extern rl_ctx_t *rl_ctx_create(int n, const rl_relspec_t *specs, int k);

extern void rl_ctx_destroy(rl_ctx_t *ctx);

/**
 * \brief Run ReachDD: returns src . [[L]]^*.
 *
 * Must be called from outside a Lace task (it starts one).
 *
 * rl_run uses the sequential schedule of Alg. 4; rl_run_par uses the parallel
 * schedule of Alg. 5, which spawns the two recursions, then the two cross
 * ImageAlls, then the two unions, and computes each ImageAll as a balanced
 * reduction over its writer list.  Alg. 5 reorders Alg. 4's loop body and
 * changes nothing else: both are chaotic iterations of the same monotone
 * statements with the same exit test, so both converge to the same least
 * fixpoint, and both write the same operation cache with the same values.
 * Only the number of rounds can differ.
 *
 * Both are correct with any number of Lace workers; the Sylvan sub-operations
 * of the SEQUENTIAL schedule already fan out over every worker, so rl_run is
 * not single threaded either.
 */
extern BDD rl_run(rl_ctx_t *ctx, BDD src);

/** \brief Run ReachDD under the parallel schedule of Alg. 5. */
extern BDD rl_run_par(rl_ctx_t *ctx, BDD src);

/**
 * \brief Install the pre/post garbage collection hooks of App. B.6.
 *
 * Idempotent.  The hooks measure what a collection recovered and, when the
 * unique table is at its maximum size and a collection twice running frees
 * less than an eighth of it, pin every relation to the apply-whole schedule of
 * Cor. th:sat-special for the rest of the run -- which is saturation, computed
 * by the same operation, and which stops allocating residual cells.
 */
extern void rl_gc_hooks_install(void);

/**
 * \brief Force (1) or unforce (0) the apply-whole schedule of Cor.
 *        th:sat-special for every relation.  For debugging: with everything
 *        pinned Alg. 4 performs exactly saturation's relation applications.
 */
extern void rl_set_pin_all(int pin);

/**
 * \brief Number of residual cells consed so far.
 */
extern uint64_t rl_cells_created(void);

/**
 * \brief Number of ReachDD recursion entries so far (diagnostic).
 */
extern uint64_t rl_calls_made(void);

/**
 * \brief Number of those answered from the operation cache (diagnostic).
 */
extern uint64_t rl_cache_hits(void);

/**
 * \brief How often Alg. 4 line 5 returned a full cube it had not expanded,
 *        with a learning relation still pending.  Zero means the expand
 *        callbacks saw every state of the answer.
 */
extern uint64_t rl_cube_waivers(void);

/**
 * \brief How many times the level-fixpoint loop (line 11) has run its body,
 *        summed over every level (diagnostic).  Alg. 5's Jacobi schedule can
 *        need more rounds per level than Alg. 4's Gauss-Seidel one; this is
 *        where that shows.
 */
extern uint64_t rl_rounds_run(void);

/**
 * \brief How many calls reached that loop, i.e. how many level fixpoints were
 *        run (diagnostic).  rl_rounds_run() / rl_fixpoints_run() is the mean
 *        number of rounds a level fixpoint needs; the round total on its own
 *        is pinned to the call total, one rl_run satisfying
 *        rl_calls_made() == 1 + 2 * rl_rounds_run() exactly.
 */
extern uint64_t rl_fixpoints_run(void);

/**
 * \brief Histogram of |A|, the number of relations handed to one ImageAll,
 *        over every ImageAll of lines 13 and 15 (diagnostic).  Buckets 0..4
 *        are |A| = 0..4 exactly, 5 is 5-8, 6 is 9-16, 7 is 17 or more.
 *
 * The balanced spawn tree of rl_imageall only branches at |A| >= 2, so this
 * says whether the parallelism Sec. 4 claims saturation cannot expose is
 * present in a given model at all.  Exact at one worker, indicative above.
 */
extern uint64_t rl_imageall_arity(int bucket);

/**
 * \brief Number of interned residual projections in this context.
 */
extern uint32_t rl_ctx_projections(const rl_ctx_t *ctx);

/**
 * \brief Number of relations left after Reduce at the root.
 */
extern int rl_ctx_relations(const rl_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // VSET_SYLVAN_REACHLIST_H
