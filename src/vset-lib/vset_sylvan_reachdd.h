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
} rl_relspec_t;

typedef struct rl_ctx rl_ctx_t;

/**
 * \brief Intern the residual projections of the given relations (App. B.6).
 *
 * Relations with an empty diagram or an empty write projection are dropped
 * here (Alg. 4 line 3, Reduce, at the root).  Returns NULL when nothing is
 * left, in which case the fixpoint is the identity.
 *
 * The returned context must not outlive the relations it was built from.
 */
extern rl_ctx_t *rl_ctx_create(int n, const rl_relspec_t *specs, int k);

extern void rl_ctx_destroy(rl_ctx_t *ctx);

/**
 * \brief Run ReachDD: returns src . [[L]]^*.
 *
 * Must be called from outside a Lace task (it starts one).
 */
extern BDD rl_run(rl_ctx_t *ctx, BDD src);

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
