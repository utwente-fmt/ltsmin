#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-guards.h>

#define          USE_GUARDS_EVAL_OPTION "pins-guard-eval"

int              PINS_GUARD_EVAL = 0;

struct poptOption guards_options[]={
    { USE_GUARDS_EVAL_OPTION, 0, POPT_ARG_VAL|POPT_ARGFLAG_DOC_HIDDEN, &PINS_GUARD_EVAL, 1,
      "use guards in combination with the long next-state function to speed up the next-state function" , NULL},
    POPT_TABLEEND
};

typedef struct guard_ctx_s {
    int                *guard_status;
} guard_ctx_t;

int
guards_all (model_t model, int *src, TransitionCB cb, void *context)
{
    model_t         parent = GBgetParent (model);
    guard_ctx_t    *ctx = GBgetContext (model);

    // fill guard status, request all guard values
    GBgetStateLabelsAll (parent, src, ctx->guard_status);

    guard_t **guards = GBgetGuardsInfo (parent);
    int res = 0;
    for (size_t i = 0; i < pins_get_group_count(model); i++) {
        guard_t *gt = guards[i];
        int enabled = 1;
        for (int j = 0; j < gt->count && enabled; j++) {
            enabled &= ctx->guard_status[gt->guard[j]] != 0;
        }
        if (enabled) {
            res += GBgetActionsLong (parent, i, src, cb, context);
        }
    }

    return res;
}

model_t
GBaddGuards (model_t model)
{
    HREassert (model != NULL, "No model");

    if (!PINS_GUARD_EVAL) return model;

    /* check if the implementation actually supports and exports guards */
    sl_group_t *guards = GBgetStateLabelGroupInfo (model, GB_SL_GUARDS);
    if (guards == NULL || guards->count == 0) {
        Abort ("No long next-state function implemented for this language module (--"USE_GUARDS_EVAL_OPTION").");
    }

    model_t             guarded = GBcreateBase ();

    guard_ctx_t        *ctx = RTmalloc (sizeof(guard_ctx_t));
    ctx->guard_status = RTmalloc(sizeof(int[pins_get_state_label_count(model)]));

    GBsetContext (guarded, ctx);
    GBinitModelDefaults (&guarded, model);
    GBsetNextStateAll (guarded, guards_all);

    return guarded;
}

