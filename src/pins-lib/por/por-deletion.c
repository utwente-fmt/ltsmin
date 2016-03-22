#include <hre/config.h>

#include <stdlib.h>

#include <dm/dm.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/por/por-deletion.h>
#include <pins-lib/por/por-internal.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/pins-util.h>
#include <util-lib/bitmultiset.h>
#include <util-lib/util.h>


/**
 * DELETION algorithm.
 */

/**
 * Sets in deletion algorithm
 * Some are maintained only as stack or as set with cardinality counter
 * A "stack set" can both be iterated over and performed inclusion tests on,
 * however it does not support element removal (as it messes up the stack).
 */
typedef enum {
    DEL_N,  // set
    DEL_K,  // count set (stack content may be corrupted)
    DEL_E,  // set (EMITTED)
    DEL_Z,  // stack set
    DEL_R,  // set
    DEL_V,
    DEL_COUNT
} del_t;

struct del_ctx_s {
    bms_t              *del;
    ci_list            *Kprime;
    ci_list            *Nprime;
    ci_list            *Dprime;
    int                *nes_score;
    int                *group_score;
    int                 invisible_enabled;
    bool                has_invisible;
    bool                del_v;
};

del_ctx_t *
del_create (por_context* ctx)
{
    del_ctx_t *delctx = RTmalloc (sizeof(del_ctx_t));
    delctx->del = bms_create (ctx->ngroups, DEL_COUNT);
    delctx->Kprime = ci_create(ctx->ngroups);
    delctx->Nprime = ci_create(ctx->ngroups);
    delctx->Dprime = ci_create(ctx->ngroups);
    delctx->group_score  = RTmallocZero(ctx->ngroups * sizeof(int));
    delctx->nes_score    = RTmallocZero(NS_SIZE(ctx) * sizeof(int));
    return delctx;
}

static inline bool
del_enabled (por_context* ctx, int u)
{
    return ctx->group_status[u] == GS_ENABLED;
}

static void
deletion_setup (model_t model, por_context* ctx, int* src, bool reset)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->alg;
    bms_t           *del = delctx->del;
    por_init_transitions (model, ctx, src);
    if (ctx->enabled_list->count == 0) return;

    // use -1 for deactivated NESs
    for (int ns = 0; ns < ctx->nguards; ns++) { // guard should be false
        delctx->nes_score[ns] = 0 - (ctx->label_status[ns] != 0); // 0 for false!
    }
    for (int ns = ctx->nguards; ns < NS_SIZE(ctx); ns++) { // guard should be true
        delctx->nes_score[ns] = 0 - (ctx->label_status[ns - ctx->nguards] == 0); // 0 for true!
    }

    // initially all active ns's are in:
    for (int t = 0; t < ctx->ngroups; t++) {
        delctx->group_score[t] = 0;
        if (del_enabled(ctx,t)) continue;
        for (int i = 0; i < ctx->group_has[t]->count; i++) {
            int ns = ctx->group_has[t]->data[i];
            delctx->group_score[t] += delctx->nes_score[ns] >= 0;
        }
    }

    // K = {}; Z := {}; KP := {}; TP := {}; R := {}
    // N := T
    bms_clear_lists (del);
    if (reset) {
        bms_set_all (del, DEL_N);
    } else {
        bms_and_or_all (del, DEL_R, DEL_E, DEL_N); // save emitted and revert
    }
    ci_clear (delctx->Kprime);
    ci_clear (delctx->Nprime);
    ci_clear (delctx->Dprime);

    //  K := A
    delctx->invisible_enabled = 0;
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int group = ctx->enabled_list->data[i];
        bms_push_new (del, DEL_K, group);

        bool v = is_visible(ctx, group);
        bms_push_if (del, DEL_V, group, v);
        delctx->invisible_enabled += !v;
    }
    delctx->has_invisible = delctx->invisible_enabled != 0;
    Debug ("Deletion init |en| = %d \t|R| = %d",
             ctx->enabled_list->count, bms_count(del, DEL_R));
}

/**
 * del_nes and del_nds indicate whether the visibles have already been deleted
 */
static inline bool
deletion_delete (por_context* ctx)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->alg;
    bms_t           *del = delctx->del;

    // search the deletion space:
    while (bms_count(del, DEL_Z) != 0 && bms_count(del, DEL_K) != 0) {
        int z = bms_pop (del, DEL_Z);
        Debug ("Checking z = %d", z);

        if (bms_has(del,DEL_R,z)) return true;

        // First, the enabled transitions x that are still stubborn and
        // belong to DNA_u, need to be removed from key stubborn and put to Z.
        for (int i = 0; i < ctx->not_accords[z]->count && bms_count(del, DEL_K) > 0; i++) {
            int x = ctx->not_accords[z]->data[i];
            if (bms_has(del,DEL_K,x)) {
                if (!bms_has(del,DEL_N,x)) {
                    if (bms_has(del,DEL_R,x)) return true;
                    bms_push_new (del, DEL_Z, x);
                }
                if (bms_rem(del, DEL_K, x)) ci_add (delctx->Kprime, x);
                delctx->invisible_enabled -= !is_visible(ctx, x);
                if (delctx->has_invisible && delctx->invisible_enabled == 0 && !NO_V) {
                    return true;
                }
            }
        }
        if (bms_count(del, DEL_K) == 0) return true;

        // Second, the enabled transitions x that are still stubborn and
        // belong to DNB_u need to be removed from other stubborn and put to Z.
        for (int i = 0; i < ctx->not_left_accordsn[z]->count; i++) {
            int x = ctx->not_left_accordsn[z]->data[i];
            if (del_enabled(ctx,x) && bms_has(del,DEL_N,x)) {
                if (!bms_has(del,DEL_K,x)) {
                    if (bms_has(del,DEL_R,x)) return true;
                    bms_push_new (del, DEL_Z, x);
                }
                if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
            }
        }

        // Third, if a visible is deleted, then remove all enabled visible
        if ((SAFETY || PINS_LTL) && (NO_V ? del_enabled(ctx,z) : is_visible(ctx,z))) {
            for (int i = 0; i < del->lists[DEL_V]->count; i++) {
                int x = del->lists[DEL_V]->data[i];
                if (bms_has(del, DEL_N, x) || bms_has(del, DEL_K, x)) {
                    if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
                    if (bms_rem(del, DEL_K, x)) {
                        ci_add (delctx->Kprime, x);
                        if (bms_count(del, DEL_K) == 0) return true;
                        delctx->invisible_enabled -= !is_visible(ctx, x);
                        if (delctx->has_invisible && delctx->invisible_enabled == 0) return true;
                    }
                    if (bms_has(del,DEL_R,x)) return true; // enabled
                    bms_push_new (del, DEL_Z, x);
                }
            }
            delctx->del_v = true;
        }

        ci_add (delctx->Dprime, z);
        // Fourth, the disabled transitions x, whose NES was stubborn
        // before removal of z, need to be put to Z.
        bool inR = false;
        for (int i = 0; i < ctx->group2ns[z]->count; i++) {
            int ns = ctx->group2ns[z]->data[i];
            if (delctx->nes_score[ns] == -1) continue; // -1 is inactive!

            // not the first transition removed from NES?
            int score = delctx->nes_score[ns]++;
            if (score != 0) continue;

            for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                int x = ctx->group_hasn[ns]->data[i];
                if (!del_enabled(ctx,x) ||
                        (POR_WEAK==WEAK_VALMARI && !bms_has(del,DEL_K,x))) {
                    delctx->group_score[x]--;
                    HREassert (delctx->group_score[x] >= 0, "Wrong counting!");
                    if (delctx->group_score[x] == 0 && bms_has(del,DEL_N,x)) {
                        bms_push_new (del, DEL_Z, x);
                        HREassert (!bms_has(del, DEL_K, x)); // x is disabled
                        if (bms_rem(del, DEL_N, x)) ci_add (delctx->Nprime, x);
                        inR |= bms_has(del, DEL_R, x);
                    }
                }
            }
        }
        if (inR) return true; // revert
    }
    return bms_count(del, DEL_K) == 0;
}

static inline void
deletion_analyze (por_context *ctx, ci_list *delete)
{
    if (delete->count == 0) return;
    del_ctx_t          *delctx = (del_ctx_t *) ctx->alg;
    bms_t              *del = delctx->del;
    delctx->del_v = false;

    for (int i = 0; i < delete->count && bms_count(del, DEL_K) > 1; i++) {
        int v = delete->data[i];
        if (bms_has(del, DEL_R, v)) continue;

        if (bms_rem(del, DEL_K, v)) ci_add (delctx->Kprime, v);
        if (bms_rem(del, DEL_N, v)) ci_add (delctx->Nprime, v);
        bms_push_new (del, DEL_Z, v);

        Debug ("Deletion start from v = %d: |E| = %d \t|K| = %d", v, ctx->enabled_list->count, bms_count(del, DEL_K));

        int             del_v_old = delctx->del_v;
        bool            revert = deletion_delete (ctx);

        while (bms_count(del, DEL_Z) != 0) bms_pop (del, DEL_Z);

        // Reverting deletions if necessary
        if (revert) {
            Debug ("Deletion rollback: |T'| = %d \t|K'| = %d \t|D'| = %d",
                     ci_count(delctx->Nprime), ci_count(delctx->Kprime), ci_count(delctx->Dprime));
            bms_add (del, DEL_R, v); // fail transition!
            while (ci_count(delctx->Kprime) != 0) {
                int x = ci_pop (delctx->Kprime);
                bool seen = bms_push_new (del, DEL_K, x);
                delctx->invisible_enabled += !is_visible(ctx, x);
                HREassert (seen, "DEL_K messed up");
            }
            while (ci_count(delctx->Nprime) != 0) {
                int x = ci_pop (delctx->Nprime);
                del->set[x] = del->set[x] | (1<<DEL_N);
            }
            while (ci_count(delctx->Dprime) != 0) {
                int x = ci_pop (delctx->Dprime);
                for (int i = 0; i < ctx->group2ns[x]->count; i++) {
                    int ns = ctx->group2ns[x]->data[i];
                    delctx->nes_score[ns] -= (delctx->nes_score[ns] >= 0);
                    if (delctx->nes_score[ns] != 0) continue; // NES not readded

                    for (int i = 0; i < ctx->group_hasn[ns]->count; i++) {
                        int x = ctx->group_hasn[ns]->data[i];
                        delctx->group_score[x] += !del_enabled(ctx,x);
                    }
                }
            }
            delctx->del_v &= del_v_old; // remain only true if successfully removed before
        } else {
            ci_clear (delctx->Kprime);
            ci_clear (delctx->Nprime);
            ci_clear (delctx->Dprime);
        }
    }
}

static inline int
deletion_emit_new (por_context *ctx, prov_t *provctx, int* src)
{
    del_ctx_t       *delctx = (del_ctx_t *) ctx->alg;
    bms_t           *del = delctx->del;
    int c = 0;
    for (int z = 0; z < ctx->enabled_list->count; z++) {
        int i = ctx->enabled_list->data[z];
        if (del_is_stubborn(ctx,i) && !bms_has(del,DEL_E,i)) {
            del->set[i] |= 1<<DEL_E | 1<<DEL_R;
            c += GBgetTransitionsLong (ctx->parent, i, src, hook_cb, provctx);
        }
    }
    return c;
}

static inline bool
del_all_stubborn (por_context *ctx, ci_list *list)
{
    for (int i = 0; i < list->count; i++)
        if (!del_is_stubborn(ctx, list->data[i])) return false;
    return true;
}

static inline int
deletion_emit (model_t model, por_context *ctx, int *src, TransitionCB cb,
               void *uctx)
{
    del_ctx_t          *delctx = (del_ctx_t *) ctx->alg;
    bms_t              *del = delctx->del;
    prov_t              provctx = {cb, uctx, 0, 0, 0};

    if (PINS_LTL || SAFETY) {
        if (NO_L12) {
            provctx.force_proviso_true = del_all_stubborn(ctx,ctx->enabled_list);
        } else { // Deletion guarantees that I holds, but does V hold?
            provctx.force_proviso_true = !delctx->del_v;
            HRE_ASSERT (provctx.force_proviso_true ==
                        del_all_stubborn(ctx,ctx->visible->lists[VISIBLE]));
        }
    }

    int emitted = deletion_emit_new (ctx, &provctx, src);

    // emit more if we need to fulfill a liveness / safety proviso
    if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
         (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {
        if (NO_L12) {
            for (int i = 0; i < ctx->enabled_list->count; i++) {
                int x = ctx->enabled_list->data[i];
                del->set[x] |= (1 << DEL_N);
            }
            emitted += deletion_emit_new (ctx, &provctx, src);
        } else {
            for (int i = 0; i < del->lists[DEL_V]->count; i++) {
                int x = del->lists[DEL_V]->data[i];
                del->set[x] |= 1 << DEL_R;
            }
            deletion_setup (model, ctx, src, false);
            deletion_analyze (ctx, ctx->enabled_list);

            emitted += deletion_emit_new (ctx, &provctx, src);
        }
    }

    return emitted;
}

int
del_por_all (model_t self, int *src, TransitionCB cb, void *user_context)
{
    por_context* ctx = ((por_context*)GBgetContext(self));
    deletion_setup (self, ctx, src, true);
    if (ctx->exclude) {
        deletion_analyze (ctx, ctx->exclude);
    }
    deletion_analyze (ctx, ctx->enabled_list);
    return deletion_emit (self, ctx, src, cb, user_context);
}


bool
del_is_stubborn (por_context *ctx, int group)
{
    del_ctx_t *del_ctx = (del_ctx_t *)ctx->alg;
    bms_t* del = del_ctx->del;
    return bms_has(del, DEL_N, group) || bms_has(del, DEL_K, group);
}
