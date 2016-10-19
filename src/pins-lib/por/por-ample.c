#include <hre/config.h>

#include <stdbool.h>

#include <pins-lib/por/pins2pins-por.h>
#include <pins-lib/por/por-ample.h>
#include <pins-lib/por/por-internal.h>


/**
 * Ample-set search
 */

typedef struct process_s {
    int                 first_group;
    int                 last_group;
    ci_list            *enabled;
} process_t;

struct ample_ctx_s {
    size_t              num_procs;
    process_t          *procs;
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
    por_init_transitions (ctx->parent, ctx, src);

    size_t              p = 0;
    process_t          *proc = &ample->procs[p];
    ci_clear (proc->enabled);
    for (int i = 0; i < ctx->enabled_list->count; i++) {
        int             group = ctx->enabled_list->data[i];
        if (proc->last_group < group) {
            proc = &ample->procs[++p];
            ci_clear (proc->enabled);
        }
        ci_add (proc->enabled, group);
    }
    HREassert (p <= ample->num_procs);

    prov_t provctx = {cb, uctx, 0, 0, 0};
    for (size_t p = 0; p < ample->num_procs; p++) {
        process_t          *proc = &ample->procs[p];

        bool                emit_p = true;
        for (int j = 0; j < proc->enabled->count && emit_p; j++) {
            int group = proc->enabled->data[j];

            if (ctx->visible->set[group])
                emit_p = false;
            for (int j = 0; j < ctx->not_accords[group]->count && emit_p; j++) {
                int dep = ctx->not_accords[group]->data[j];
                if (dep < proc->first_group || dep > proc->last_group ) {
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


ample_ctx_t *
ample_create_context (por_context *ctx, bool all)
{
    HREassert (!all, "Not implemented");

    ample_ctx_t *ample = RTmalloc(sizeof(ample_ctx_t));
    ample->procs = RTmalloc(sizeof(process_t[ctx->ngroups])); // enough

    matrix_t group_deps;
    matrix_t *deps = GBgetDMInfo (ctx->parent);
    dm_create(&group_deps, ctx->ngroups, ctx->ngroups);
    for (int i = 0; i < ctx->ngroups; i++) {
        for (int j = 0; j < ctx->nslots; j++) {
            if (dm_is_set(deps, i, j)) {
                for (int h = 0; h <= i; h++) {
                    if (dm_is_set(deps, h, j)) {
                        dm_set (&group_deps, i, h);
                        dm_set (&group_deps, h, i);
                    }
                }
            }
        }
    }
    if (log_active(infoLong) && HREme(HREglobal()) == 0) dm_print (stdout, &group_deps);

    // find processes:
    size_t pbegin = 0;
    ample->num_procs = 0;
    for (int i = pbegin; i <= ctx->ngroups; i++) {
        size_t num_set = 0;
        if (i != ctx->ngroups) {
            HREassert (dm_is_set(&group_deps, i, i), "Cannot detect processes for ample set POR in incomplete gorup/slot dependency matrix (diagonal not set in derived group/group relation).");
            for (int j = pbegin; j <= i; j++) {
                num_set += dm_is_set(&group_deps, i, j);
            }
        }
        if (num_set < (i - pbegin + 1) / 2) { // heuristic
            // end of process
            size_t pend = i - 1;
            ample->procs[ample->num_procs].first_group = pbegin;
            ample->procs[ample->num_procs].last_group = pend;
            ample->num_procs++;
            Print1 (info, "Ample: Process %zu, starts from group %zu and ends with group %zu.", ample->num_procs, pbegin, pend);

            // remove dependencies with other groups to avoid confusion in detection
            for (size_t j = pbegin; j <= pend; j++) {
                for (int h = 0; h < ctx->ngroups; h++) {
                    dm_unset (&group_deps, j, h);
                    dm_unset (&group_deps, h, j);
                }
            }
            pbegin = pend + 1;
        }
    }

    for (size_t i = 0; i < ample->num_procs; i++) {
        process_t          *proc = &ample->procs[i];
        proc->enabled = ci_create (proc->last_group - proc->first_group + 1);
    }

    if (!dm_is_empty(&group_deps) && HREme(HREglobal()) == 0) {
        dm_print (stdout, &group_deps);
        Abort ("Ample set heuristic for process identification failed, please verify the results with --labels.")
    }

    return ample;
}

bool
ample_is_stubborn (por_context *ctx, int group)
{
    HREassert (false, "ample set incompletely implemented");
    return 1;
    (void) ctx; (void) group;
}
