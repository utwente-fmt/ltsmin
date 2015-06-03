#include <hre/config.h>

#include <stdlib.h>

#include <hre/user.h>
#include <pins-lib/pins.h>
#include <pthread.h>
#include <ltsmin-lib/ltsmin-standard.h> // for LTSMIN_EXIT_FAILURE

/**
 * Wraps a PINS interface in a mutex
 */

// Struct for the 'context' field in the greybox model
struct mutex_context
{
    pthread_mutex_t mutex;
    size_t count;
};

static int
mutex_next_short(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetTransitionsShort(GBgetParent(model), group, src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_next_long(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetTransitionsLong(GBgetParent(model), group, src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_next_all(model_t model, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetTransitionsAll(GBgetParent(model), src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_next_matching(model_t model, int label_idx, int value, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetTransitionsMatching(GBgetParent(model), label_idx, value, src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_actions_short(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetActionsShort(GBgetParent(model), group, src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_actions_long(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetActionsLong(GBgetParent(model), group, src, cb, context);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_state_labels_short(model_t model, int label, int *state)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetStateLabelShort(GBgetParent(model), label, state);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static int
mutex_state_labels_long(model_t model, int label, int *state)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBgetStateLabelLong(GBgetParent(model), label, state);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

static void
mutex_state_labels_all(model_t model, int *state, int *labels)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    GBgetStateLabelsAll(GBgetParent(model), state, labels);

    pthread_mutex_unlock(&mc->mutex);
}

static void
mutex_state_labels_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    GBgetStateLabelsGroup(GBgetParent(model), group, state, labels);

    pthread_mutex_unlock(&mc->mutex);
}

static int
mutex_transition_in_group(model_t model, int *labels, int group)
{
    struct mutex_context *mc = (struct mutex_context*)GBgetContext(model);
    pthread_mutex_lock(&mc->mutex);
    mc->count++;

    int res = GBtransitionInGroup(GBgetParent(model), labels, group);

    pthread_mutex_unlock(&mc->mutex);
    return res;
}

/**
 * Finally, this method creates the 'mutex' wrapper around some model
 */

model_t
GBaddMutex(model_t parent_model)
{
    HREassert(parent_model != NULL, "No model");

    /* create new model */
    model_t mutex_model = GBcreateBase();

    /* initialize as child model */
    GBinitModelDefaults(&mutex_model, parent_model);

    /* create fork_context */
    struct mutex_context *ctx = HREmalloc(NULL, sizeof(struct mutex_context));
    pthread_mutex_init(&(ctx->mutex), NULL);

    /* set context to struct */
    GBsetContext(mutex_model, ctx);

    /* set overloaded functions */
    GBsetNextStateShort(mutex_model, mutex_next_short);
    GBsetNextStateLong(mutex_model, mutex_next_long);
    GBsetNextStateAll(mutex_model, mutex_next_all);
    GBsetNextStateMatching(mutex_model, mutex_next_matching);
    GBsetActionsShort(mutex_model, mutex_actions_short);
    GBsetActionsLong(mutex_model, mutex_actions_long);
    GBsetStateLabelShort(mutex_model, mutex_state_labels_short);
    GBsetStateLabelLong(mutex_model, mutex_state_labels_long);
    GBsetStateLabelsGroup(mutex_model, mutex_state_labels_group);
    GBsetStateLabelsAll(mutex_model, mutex_state_labels_all);
    GBsetTransitionInGroup(mutex_model, mutex_transition_in_group);
    // covered_by type needs "model" parameter!!!
    // GBsetIsCoveredBy(mutex_model, mutex_is_covered_by);
    // GBsetIsCoveredByShort(mutex_model, mutex_is_covered_by_short);

    return mutex_model;
}
