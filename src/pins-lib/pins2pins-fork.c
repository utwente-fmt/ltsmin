#include <hre/config.h>

#include <pthread.h> // for pthread_key thread-specific variables
#include <stdlib.h>
#include <sys/socket.h> // for AF_UNIX etc

#include <hre-io/user.h> // for streaming
#include <ltsmin-lib/ltsmin-standard.h> // for LTSMIN_EXIT_FAILURE
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-fork.h>

int PINS_REQUIRE_FORK_WRAPPER = 0;

/**
 * Wraps a PINS interface in a separate process, communicating over UNIX socket
 */

// Struct for the 'context' field in the greybox model
struct fork_context
{
    pthread_key_t key;
    int n_groups; // number of transition groups
    int n_state_labels; // number of state labels
    int state_length; // length of state vector
    int *s_lengths; // for 'short' (int get_next and_get_actions)
    int *r_lengths; // for 'short' src (in get_next_r2w)
    int *a_lengths; // for 'short' src (in get_actions_r2w)
    int *w_lengths; // for 'short' dst/cpy (in get_next_r2w and get_actions_r2w)
    int *l_lengths; // for 'short' state (in get_state_label)
};

// Struct for the thread-specific information associated with the fork_context key
struct thread_info
{
    struct fork_context *fc;
    model_t model; // fork_model
    stream_t parent_socket_is, parent_socket_os;
    stream_t child_socket_is, child_socket_os;
    int parent_socket, child_socket;
    int pid; // process id
};

// All request types sent to the child processes
enum FORK_CALL {
    EXIT = 0,
    NEXT_SHORT,
    NEXT_SHORT_R2W,
    NEXT_LONG,
    NEXT_ALL,
    NEXT_MATCHING,
    ACTIONS_SHORT,
    ACTIONS_SHORT_R2W,
    ACTIONS_LONG,
    LABELS_SHORT,
    LABELS_LONG,
    LABELS_GROUP,
    LABELS_ALL,
    GROUPS_OF_EDGE,
    COVERED_BY,
    COVERED_BY_SHORT,
};

enum FORK_REPLY {
    DONE = 0,
    REPLY,
};


/**
 * Now follows the implementation on the "slave" side
 */

// Callback function for get-next and get-actions
static void
slave_transition_cb(void* context, transition_info_t* ti, int* dst, int* cpy, enum FORK_CALL type)
{
    struct thread_info *t_info = (struct thread_info *)context;
    stream_t os = t_info->child_socket_os;
    model_t model = t_info->model;

    // determine length of dst and cpy
    int length = -1;
    if (type == NEXT_SHORT) length = t_info->fc->s_lengths[ti->group];
    else if (type == NEXT_LONG) length = t_info->fc->state_length;
    else if (type == NEXT_SHORT_R2W) length = t_info->fc->w_lengths[ti->group];

    // send REPLY
    DSwriteS32(os, REPLY);
    // send group
    DSwriteS32(os, ti->group);
    // send dst
    for (int i=0; i<length; i++) DSwriteS32(os, dst[i]);
    // send cpy
    DSwriteS32(os, cpy != NULL ? 1 : 0);
    if (cpy != NULL) for (int i=0; i<length; i++) DSwriteS32(os, cpy[i]);
    // send labels
    int labels = lts_type_get_edge_label_count(GBgetLTStype(model));
    for (int i=0; i<labels; i++) DSwriteS32(os, ti->labels[i]);
    // send por proviso
    DSwriteS32(os, ti->por_proviso);
    // done
    stream_flush(os);
}

static void
slave_transition_cb_short(void* context, transition_info_t* ti, int* dst, int* cpy)
{
    slave_transition_cb(context, ti, dst, cpy, NEXT_SHORT);
}

static void
slave_transition_cb_short_r2w(void* context, transition_info_t* ti, int* dst, int* cpy)
{
    slave_transition_cb(context, ti, dst, cpy, NEXT_SHORT_R2W);
}

static void
slave_transition_cb_long(void* context, transition_info_t* ti, int* dst, int* cpy)
{
    slave_transition_cb(context, ti, dst, cpy, NEXT_LONG);
}

// Implementation of "wait for commands" loop
static void
child_process(struct thread_info *ti)
{
    model_t parent_model = GBgetParent(ti->model);
    stream_t is = ti->child_socket_is;
    stream_t os = ti->child_socket_os;

    for (;;) {
        enum FORK_CALL next = DSreadS32(is);
        if (next == NEXT_SHORT) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->s_lengths[group];
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetTransitionsShort(parent_model, group, src, slave_transition_cb_short, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == NEXT_SHORT_R2W) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->r_lengths[group];
            int src[length];
            // call parent model
            int res = GBgetTransitionsShortR2W(parent_model, group, src, slave_transition_cb_short_r2w, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == NEXT_LONG) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetTransitionsLong(parent_model, group, src, slave_transition_cb_long, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == NEXT_ALL) {
            // read src state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetTransitionsAll(parent_model, src, slave_transition_cb_long, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == NEXT_MATCHING) {
            // read label index
            int label = DSreadS32(is);
            // read value
            int value = DSreadS32(is);
            // read src state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetTransitionsMatching(parent_model, label, value, src, slave_transition_cb_long, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == ACTIONS_SHORT) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->s_lengths[group];
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetActionsShort(parent_model, group, src, slave_transition_cb_short, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == ACTIONS_SHORT_R2W) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->a_lengths[group];
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetActionsShortR2W(parent_model, group, src, slave_transition_cb_short_r2w, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == ACTIONS_LONG) {
            // read transition group
            int group = DSreadS32(is);
            // read src state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetActionsLong(parent_model, group, src, slave_transition_cb_long, ti);
            // signal that all successor states have been sent
            DSwriteS32(os, DONE);
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == LABELS_SHORT) {
            // read label
            int label = DSreadS32(is);
            // read src state
            int length = ti->fc->l_lengths[label];
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetStateLabelShort(parent_model, label, src);
            // signal the result of the label evaluation
            DSwriteS32(os, res);
            stream_flush(os);
         } else if (next == LABELS_LONG) {
            // read label
            int label = DSreadS32(is);
            // read src state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int res = GBgetStateLabelLong(parent_model, label, src);
            // signal the result of the label evaluation
            DSwriteS32(os, res);
            stream_flush(os);
        } else if (next == LABELS_ALL) {
            // read state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int n_labels = ti->fc->n_state_labels;
            int labels[n_labels];
            GBgetStateLabelsAll(parent_model, src, labels);
            // signal the result of the label evaluation
            for (int i=0; i<n_labels; i++) DSwriteS32(os, labels[i]);
            stream_flush(os);
        } else if (next == LABELS_GROUP) {
            // read group
            sl_group_enum_t group = DSreadS32(is);
            // read state
            int length = ti->fc->state_length;
            int src[length];
            for (int i=0; i < length; i++) src[i] = DSreadS32(is);
            // call parent model
            int n_labels = GBgetStateLabelGroupInfo(parent_model, group)->count;
            int labels[n_labels];
            GBgetStateLabelsAll(parent_model, src, labels);
            // signal the result of the label evaluation
            for (int i=0; i<n_labels; i++) DSwriteS32(os, labels[i]);
            stream_flush(os);
        } else if (next == GROUPS_OF_EDGE) {
            int edgeno = DSreadS32(is);
            int index = DSreadS32(is);
            int groups[pins_get_group_count(parent_model)];
            const int n = GBgroupsOfEdge(parent_model, edgeno, index, groups);
            DSwriteS32(os, n);
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    DSwriteS32(os, groups[i]);
                }
            }
            stream_flush(os);
        } else if (next == EXIT) {
            break;
        } else {
            Print1(lerror, "unsupported operation");
            HREexit(LTSMIN_EXIT_FAILURE);
        }
    }

    exit(0);
}


/**
 * Now follows the implementation on the "master" side
 */

// Helper function that handles forking on demand for every thread
static struct thread_info*
get_or_fork(struct fork_context *ctx, model_t model)
{
    struct thread_info *ti = pthread_getspecific(ctx->key);
    if (ti == NULL) {
        Print1(info, "Forking...");
        ti = HREmalloc(NULL, sizeof(struct thread_info));
        pthread_setspecific(ctx->key, ti);

        ti->fc = ctx;
        ti->model = model;

        int fd[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, fd);
        ti->parent_socket = fd[0];
        ti->parent_socket_is = fd_input(fd[0]);
        ti->parent_socket_os = fd_output(fd[0]);
        ti->child_socket = fd[1];
        ti->child_socket_is = fd_input(fd[1]);
        ti->child_socket_os = fd_output(fd[1]);

        ti->pid = fork();
        if (ti->pid == -1) {
            Print1(lerror, "cannot fork, error: %s", strerror(errno));
            HREexit(LTSMIN_EXIT_FAILURE);
        }
        if (ti->pid == 0) {
            // ok we are the child process
            child_process(ti);
        }
    }
    return ti;
}

static int
forked_next(model_t model, int param1, int param2, int* src, TransitionCB cb, void* context, enum FORK_CALL type)
{
    struct fork_context *fc = (struct fork_context*)GBgetContext(model);
    struct thread_info *ti = get_or_fork(fc, model);
    stream_t is = ti->parent_socket_is;
    stream_t os = ti->parent_socket_os;

    int r_length; int w_length;
    switch (type) {
        case NEXT_SHORT:
        case ACTIONS_SHORT:
            r_length = fc->s_lengths[param1];
            w_length = fc->s_lengths[param1];
        break;
        case NEXT_SHORT_R2W:
            r_length = fc->r_lengths[param1];
            w_length = fc->w_lengths[param1];
            break;
        case NEXT_LONG:
        case ACTIONS_LONG:
            r_length = fc->state_length;
            w_length = fc->state_length;
            break;
        case ACTIONS_SHORT_R2W:
            r_length = fc->a_lengths[param1];
            w_length = fc->w_lengths[param1];
            break;
        default:
            Abort("Unknown type");
    }

    int labels = lts_type_get_edge_label_count(GBgetLTStype(model));
    int res;

    // send command, param1, param2, src state
    DSwriteS32(os, type);
    if (type != NEXT_ALL) DSwriteS32(os, param1);
    if (type == NEXT_MATCHING) DSwriteS32(os, param2);
    for (int i=0; i<r_length; i++) DSwriteS32(os, src[i]);
    stream_flush(os);

    // wait for replies
    for (;;) {
        enum FORK_REPLY next = DSreadS32(is);
        if (next == REPLY) {
            // read group
            int group = DSreadS32(is);
            // read dst
            int dst[w_length];
            for (int i=0; i<w_length; i++) dst[i] = DSreadS32(is);
            // read cpy_vector
            int cpy_vector = DSreadS32(is);
            // read cpy (if cpy_vector)
            int cpy[w_length];
            if (cpy_vector) for (int i=0; i<w_length; i++) cpy[i] = DSreadS32(is);
            // create transition_info
            transition_info_t ti;
            ti.group = group;
            int ti_labels[labels];
            ti.labels = ti_labels;
            // read labels
            for (int i=0; i<labels; i++) ti.labels[i] = DSreadS32(is);
            // read por proviso
            ti.por_proviso = DSreadS32(is);
            // call callback
            if (cpy_vector) cb(context, &ti, dst, cpy);
            else cb(context, &ti, dst, NULL);
        } else if (next == DONE) {
            res = DSreadS32(is);
            break;
        } else {
            Print1(lerror, "invalid reply");
            HREexit(LTSMIN_EXIT_FAILURE);
        }
    }

    return res;
}

static int
forked_next_short(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, NEXT_SHORT);
}

static int
forked_next_short_r2w(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, NEXT_SHORT_R2W);
}

static int
forked_next_long(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, NEXT_LONG);
}

static int
forked_next_all(model_t model, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, 0, 0, src, cb, context, NEXT_ALL);
}

static int
forked_next_matching(model_t model, int label_idx, int value, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, label_idx, value, src, cb, context, NEXT_MATCHING);
}

static int
forked_actions_short(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, ACTIONS_SHORT);
}

static int
forked_actions_short_r2w(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, ACTIONS_SHORT_R2W);
}

static int
forked_actions_long(model_t model, int group, int* src, TransitionCB cb, void* context)
{
    return forked_next(model, group, 0, src, cb, context, ACTIONS_LONG);
}

static int
forked_state_labels(model_t model, int label, int *state, enum FORK_CALL type)
{
    struct fork_context *fc = (struct fork_context*)GBgetContext(model);
    struct thread_info *ti = get_or_fork(fc, model);
    stream_t is = ti->parent_socket_is;
    stream_t os = ti->parent_socket_os;

    if (label < 0) return label; // invalid!

    int length = type == LABELS_SHORT ? fc->l_lengths[label] : fc->state_length;

    // send command, label, state
    DSwriteS32(os, type);
    DSwriteS32(os, label);
    for (int i=0; i<length; i++) DSwriteS32(os, state[i]);
    stream_flush(os);

    // wait for reply
    int result = DSreadS32(is);
    return result;
}

static int
forked_state_labels_short(model_t model, int label, int *state)
{
    return forked_state_labels(model, label, state, LABELS_SHORT);
}

static int
forked_state_labels_long(model_t model, int label, int *state)
{
    return forked_state_labels(model, label, state, LABELS_LONG);
}

static void
forked_state_labels_all(model_t model, int *state, int *labels)
{
    struct fork_context *fc = (struct fork_context*)GBgetContext(model);
    struct thread_info *ti = get_or_fork(fc, model);
    stream_t is = ti->parent_socket_is;
    stream_t os = ti->parent_socket_os;

    // send command and state
    DSwriteS32(os, LABELS_ALL);
    for (int i=0; i<fc->state_length; i++) DSwriteS32(os, state[i]);
    stream_flush(os);

    // read labels
    for (int i=0; i<fc->n_state_labels; i++) labels[i] = DSreadS32(is);
}

static void
forked_state_labels_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    struct fork_context *fc = (struct fork_context*)GBgetContext(model);
    struct thread_info *ti = get_or_fork(fc, model);
    stream_t is = ti->parent_socket_is;
    stream_t os = ti->parent_socket_os;

    // send command and state
    DSwriteS32(os, LABELS_GROUP);
    DSwriteS32(os, group);
    for (int i=0; i<fc->state_length; i++) DSwriteS32(os, state[i]);
    stream_flush(os);

    // read labels
    int count = DSreadS32(is);
    for (int i=0; i<count; i++) labels[i] = DSreadS32(is);
}

static int
forked_groups_of_edge(model_t model, int edgeno, int index, int *groups)
{
    struct fork_context *fc = (struct fork_context*)GBgetContext(model);
    struct thread_info *ti = get_or_fork(fc, model);
    stream_t is = ti->parent_socket_is;
    stream_t os = ti->parent_socket_os;

    // send command, edgeno and index
    DSwriteS32(os, GROUPS_OF_EDGE);
    DSwriteS32(os, edgeno);
    DSwriteS32(os, index);
    stream_flush(os);

    const int n = DSreadS32(is);
    if (n == 0) return 0;

    if (groups != NULL) {
        // read group numbers
        for (int i = 0; i < n; i++) groups[i] = DSreadS32(is);
    }

    return n;
}


/**
 * Finally, this method creates the 'fork' wrapper around some model
 */

model_t
GBaddFork(model_t parent_model)
{
    HREassert(parent_model != NULL, "No model");

    if (!PINS_REQUIRE_FORK_WRAPPER) return parent_model;

    /* create new model */
    model_t forked_model = GBcreateBase();

    /* initialize as child model */
    GBinitModelDefaults(&forked_model, parent_model);

    /* create fork_context */
    struct fork_context *ctx = HREmalloc(NULL, sizeof(struct fork_context));
    pthread_key_create(&ctx->key, NULL);

    /* precalculate some values */
    ctx->state_length = lts_type_get_state_length(GBgetLTStype(parent_model));
    ctx->n_groups = dm_nrows(GBgetDMInfo(parent_model));
    ctx->n_state_labels = dm_nrows(GBgetStateLabelInfo(parent_model));
    ctx->s_lengths = (int*)HREmalloc(NULL, sizeof(int)*ctx->n_groups);
    ctx->r_lengths = (int*)HREmalloc(NULL, sizeof(int)*ctx->n_groups);
    ctx->a_lengths = (int*)HREmalloc(NULL, sizeof(int)*ctx->n_groups);
    ctx->w_lengths = (int*)HREmalloc(NULL, sizeof(int)*ctx->n_groups);
    ctx->l_lengths = (int*)HREmalloc(NULL, sizeof(int)*ctx->n_state_labels);
    for (int i=0; i<ctx->n_groups; i++) ctx->s_lengths[i] = dm_ones_in_row(GBgetDMInfo(parent_model), i);
    for (int i=0; i<ctx->n_groups; i++) ctx->r_lengths[i] = dm_ones_in_row(GBgetDMInfoRead(parent_model), i);
    for (int i=0; i<ctx->n_groups; i++) ctx->a_lengths[i] = dm_ones_in_row(GBgetMatrix(parent_model, GBgetMatrixID(parent_model, LTSMIN_MATRIX_ACTIONS_READS)), i);
    for (int i=0; i<ctx->n_groups; i++) ctx->w_lengths[i] = dm_ones_in_row(GBgetDMInfoMayWrite(parent_model), i);
    for (int i=0; i<ctx->n_state_labels; i++) ctx->l_lengths[i] = dm_ones_in_row(GBgetStateLabelInfo(parent_model), i);

    /* set context to struct */
    GBsetContext(forked_model, ctx);

    /* set overloaded functions */
    GBsetNextStateShort(forked_model, forked_next_short);
    GBsetNextStateShortR2W(forked_model, forked_next_short_r2w);
    GBsetNextStateLong(forked_model, forked_next_long);
    GBsetNextStateAll(forked_model, forked_next_all);
    GBsetNextStateMatching(forked_model, forked_next_matching);
    GBsetActionsShort(forked_model, forked_actions_short);
    GBsetActionsShort(forked_model, forked_actions_short_r2w);
    GBsetActionsLong(forked_model, forked_actions_long);
    GBsetStateLabelShort(forked_model, forked_state_labels_short);
    GBsetStateLabelLong(forked_model, forked_state_labels_long);
    GBsetStateLabelsGroup(forked_model, forked_state_labels_group);
    GBsetStateLabelsAll(forked_model, forked_state_labels_all);
    GBsetGroupsOfEdge(forked_model, forked_groups_of_edge);
    // GBsetIsCoveredBy
    // GBsetIsCoveredByShort

    // Note: the commented functions above are not yet implemented

    return forked_model;
}
