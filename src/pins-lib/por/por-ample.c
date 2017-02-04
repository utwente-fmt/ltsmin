#include <hre/config.h>

#include <stdbool.h>

#include <hre/stringindex.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-internal.h>


/**
 * Ample-set search
 */

typedef struct process_s {
    ci_list            *groups;
    ci_list            *enabled;
} process_t;

struct ample_ctx_s {
    size_t              num_procs;
    process_t          *procs;
    matrix_t            gnce;
    ci_list           **gmayen;
    int                *group2proc;
};

static inline size_t
ample_emit (por_context *ctx, ci_list *list, int *src, prov_t *provctx)
{
    size_t emitted = 0;
    for (int j = 0; j < list->count; j++) {
        int group = list->data[j];
        emitted += GBgetTransitionsLong (ctx->parent, group, src, hook_cb, provctx);
    }
    return emitted;
}

int
ample_search_all (model_t self, int *src, TransitionCB cb, void *uctx)
{
    por_context *ctx = ((por_context *)GBgetContext(self));
    HREassert (ctx->exclude == NULL, "Not implemented for ample sets.");

    ample_ctx_t *ample = (ample_ctx_t *)ctx->alg;
    ci_list     *en = ctx->enabled_list;
    por_init_transitions (ctx->parent, ctx, src);

    for (size_t i = 0; i < ample->num_procs; i++)
        ci_clear (ample->procs[i].enabled);
    for (int *t = ci_begin(en); t != ci_end(en); t++) {
        process_t          *proc = &ample->procs[ample->group2proc[*t]];
        ci_add (proc->enabled, *t);
    }

    prov_t provctx = {cb, uctx, 0, 0, 0};
    for (int p = 0; p < (int)ample->num_procs; p++) {
        process_t          *proc = &ample->procs[p];

        bool                emit_p = true;
        for (int *t = ci_begin(proc->enabled); t != ci_end(proc->enabled); t++) {
            int group = *t;
            if (ctx->visible->set[group])
                emit_p = false;
            for (int j = 0; j < ctx->not_accords[group]->count && emit_p; j++) {
                int dep = ctx->not_accords[group]->data[j];
                if (ample->group2proc[dep] != p) {
                    emit_p = false;
                }
            }
        }

        if (emit_p) {
            size_t              emitted = 0;
            emitted += ample_emit (ctx, proc->enabled, src, &provctx);

            // emit more if ignoring proviso is violated
            if ( ( PINS_LTL && provctx.por_proviso_false_cnt != 0) ||
                 (!PINS_LTL && provctx.por_proviso_true_cnt  == 0) ) {
                provctx.force_proviso_true = 1;
                for (size_t o = 0; o < ample->num_procs; o++) {
                    process_t          *other = &ample->procs[o];
                    if (other == proc) continue; // already emitted
                    emitted += ample_emit (ctx, other->enabled, src, &provctx);
                }
            }
            return emitted;
        }
    }

    provctx.force_proviso_true = 1;
    return ample_emit (ctx, ctx->enabled_list, src, &provctx);
}

// Search may-enabled relation over groups that cannot be mutually enabled.
static bool
add_trans (ample_ctx_t *ample, int t)
{
    if (ample->group2proc[t] != -1)
        return false;
    ample->group2proc[t] = ample->num_procs;
    ci_add (ample->procs[ample->num_procs].groups, t);

    for (int *tt = ci_begin(ample->gmayen[t]); tt != ci_end(ample->gmayen[t]); tt++) {
        if (!dm_is_set(&ample->gnce, t, *tt)) continue;

        bool added = add_trans (ample, *tt);
        if (!added && ample->group2proc[*tt] != (int) ample->num_procs) {
            Warning (info, "Group %d already added to proc %d. Enabled by group %d.",
                     *tt, ample->group2proc[*tt], t);
        }
    }
    return true;
}

ample_ctx_t *
ample_create_context (por_context *ctx, bool all)
{
    model_t model = ctx->parent;
    HREassert (!all, "Not implemented");

    ample_ctx_t *ample = RTmalloc(sizeof(ample_ctx_t));
    ample->procs = RTmalloc(sizeof(process_t[ctx->ngroups])); // enough

    matrix_t *label_mce_matrix = GBgetGuardCoEnabledInfo(model);
    matrix_t *guard_group_not_coen = NULL;
    int id = GBgetMatrixID(model, LTSMIN_GUARD_GROUP_NOT_COEN);
    if (id != SI_INDEX_FAILED) {
        guard_group_not_coen = GBgetMatrix(model, id);
        HREassert (dm_nrows(guard_group_not_coen) >= ctx->nguards &&
                   dm_ncols(guard_group_not_coen) == ctx->ngroups);
    }
    if (label_mce_matrix == NULL && guard_group_not_coen == NULL) {
        Abort ("No maybe-coenabled matrix found. Ample sets not supported.");
    }
    HREassert (dm_nrows(label_mce_matrix) >= ctx->nguards &&
               dm_ncols(label_mce_matrix) >= ctx->nguards);

    // GROUP COEN
    dm_create(&ample->gnce, ctx->ngroups, ctx->ngroups);
    for (int g = 0; g < ctx->nguards; g++) {
        if (guard_group_not_coen != NULL) {
            for (int tt = 0; tt < ctx->ngroups; tt++) {
                if (!dm_is_set(guard_group_not_coen, g, tt)) continue;

                for (int t = 0; t < ctx->guard2group[g]->count; t++) {
                    dm_set(&ample->gnce, ctx->guard2group[g]->data[t], tt);
                }
            }
            continue;
        }

        for (int gg = 0; gg < ctx->nguards; gg++) {
            if (dm_is_set(label_mce_matrix, g, gg)) continue;

            for (int t = 0; t < ctx->guard2group[g]->count; t++) {
                for (int tt = 0; tt < ctx->guard2group[gg]->count; tt++) {
                    dm_set(&ample->gnce, ctx->guard2group[g]->data[t], ctx->guard2group[gg]->data[tt]);
                }
            }
        }
    }

    id = GBgetMatrixID(model, LTSMIN_MUST_ENABLE_MATRIX);
    ci_list **musten_list = NULL;
    if (id != SI_INDEX_FAILED) {
        matrix_t *musten = GBgetMatrix(model, id);
        HREassert (dm_nrows(musten) >= ctx->nguards &&
                   dm_ncols(musten) == ctx->ngroups);
        musten_list = (ci_list**) dm_rows_to_idx_table (musten);
    }

    // GROUP MAY ENABLE
    matrix_t    gmayen;
    ci_list **nes = musten_list != NULL ? musten_list : ctx->label_nes;
    ci_list **g2g = ctx->guard2group;
    dm_create(&gmayen, ctx->ngroups, ctx->ngroups);
    for (int g = 0; g < ctx->nguards; g++) {
        for (int *t = ci_begin(nes[g]); t != ci_end(nes[g]); t++) {
            for (int *tt = ci_begin(g2g[g]); tt != ci_end(g2g[g]); tt++) {
                dm_set (&gmayen, *t, *tt);
            }
        }
    }
    ample->gmayen = (ci_list **) dm_rows_to_idx_table(&gmayen);

    // find processes:
    ample->num_procs = 0;
    ample->group2proc = RTmallocZero (sizeof(int[ctx->ngroups]));
    for (int i = 0; i < ctx->ngroups; i++)
        ample->group2proc[i] = -1;
    ample->procs[0].groups = ci_create (ctx->ngroups);
    ample->procs[0].enabled = ci_create (ctx->ngroups);
    for (int i = 0; i < ctx->ngroups; i++) {
        bool added = add_trans (ample, i);
        if (added) {
            ample->num_procs++;
            process_t          *proc = &ample->procs[ample->num_procs];
            proc->groups = ci_create (ctx->ngroups);
            proc->enabled = ci_create (ctx->ngroups);
        }
    }
    for (int i = 0; i < ctx->ngroups; i++) {
        Printf (info, "%2d, ", i);
    }
    Printf (info, "\n");
    for (int i = 0; i < ctx->ngroups; i++) {
        Printf (info, "%2d, ", ample->group2proc[i]);
    }
    Printf (info, "\n");

    return ample;
}

bool
ample_is_stubborn (por_context *ctx, int group)
{
    HREassert (false, "ample set incompletely implemented");
    return 1;
    (void) ctx; (void) group;
}
