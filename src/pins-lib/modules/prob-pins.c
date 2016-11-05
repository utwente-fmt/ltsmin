#include <hre/config.h>

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dm/dm.h>
#include <hre/runtime.h>
#include <hre/stringindex.h>
#include <hre/unix.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <prob-lib/prob_helpers.h>
#include <prob-lib/prob_client.h>
#include <pins-lib/modules/prob-pins.h>
#include <util-lib/chunk_support.h>
#include <util-lib/util.h>

// is_init is a reserved state variable
static const char* IS_INIT = "is_init";

static int no_close = 0;

static char* zocket_prefix = "/tmp/ltsmin-";

typedef struct prob_context {
    size_t num_vars;
    int op_type_no;
    prob_client_t prob_client;
    int* op_type;
    int* var_type;
    char* zocket;
} prob_context_t;

static void
prob_popt(poptContext con, enum poptCallbackReason reason, const struct poptOption *opt, const char *arg, void *data)
{
    (void) con; (void) opt; (void) arg; (void) data;

    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterLoader("mch", ProBstartProb);
        GBregisterLoader("eventb", ProBstartProb);
        GBregisterLoader("tla", ProBstartProb);

        GBregisterLoader("probz", ProBcreateZocket);

        Warning(info, "ProB module initialized");
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }

    Abort("unexpected call to prob_popt");
}

struct poptOption prob_options[] = {
    { NULL, 0, POPT_ARG_CALLBACK | POPT_CBFLAG_POST | POPT_CBFLAG_SKIPOPTION, (void*) &prob_popt, 0, NULL, NULL },
    { "no-close", 0, POPT_ARG_NONE, &no_close, 0, "do not close the ProB connection (so that it can be reused)", NULL },
    { "zocket-prefix", 0, POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT, &zocket_prefix, 0, "use a default ZMQ socket prefix, ignored for a file with a .probz extension", NULL },
    POPT_TABLEEND };

static prob_context_t*
create_context(model_t model)
{
    prob_context_t* ctx = (prob_context_t*) RTmalloc(sizeof(prob_context_t));
    GBsetContext(model, ctx);
    return ctx;
}

static void
prob_load_model(model_t model);

void
ProBcreateZocket(model_t model, const char* file)
{
    if (HREpeers(HREglobal()) > 1) {
        if (HREme(HREglobal()) == 0) {
            Warning(error, "The \".probz\" extension is incompatible with parallelism. "
                "If you want to exploit parallelism, supply the machine file directly. "
                "Consult the manpage \"man prob2lts-mc\" for further information. "
                "If you don't need parallelism, you can also supply the --procs=1 option.");
        }
        HREabort(LTSMIN_EXIT_FAILURE);
    }
    prob_context_t* ctx = create_context(model);

    ctx->zocket = RTmalloc(sizeof(char[PATH_MAX]));
    ctx->zocket = realpath(file, ctx->zocket);

    // check file exists
    struct stat st;
    if (stat(ctx->zocket, &st) != 0) Abort("Zocket does not exist: %s", ctx->zocket);

    prob_load_model(model);
}

void
ProBstartProb(model_t model, const char* file)
{
    prob_context_t* ctx = create_context(model);

    char abs_filename[PATH_MAX];
    char* ret_filename = realpath(file, abs_filename);

    // check file exists
    struct stat st;
    if (stat(ret_filename, &st) != 0) Abort("File does not exist: %s", file);

    const char* zocket = "%s%d.probz";
    ctx->zocket = RTmalloc(snprintf(NULL, 0, zocket, zocket_prefix, HREme(HREglobal())) + 1);
    sprintf(ctx->zocket, zocket, zocket_prefix, HREme(HREglobal()));

    if (stat(ctx->zocket, &st) == 0) Abort("File already exists at %s", ctx->zocket);

    Warning(info, "Creating zocket for HRE worker %d at %s", HREme(HREglobal()), ctx->zocket);

    const char* command = "probcli %s -ltsmin2 %s &";
    char buf[snprintf(NULL, 0, command, ret_filename, ctx->zocket) + 1];
    sprintf(buf, command, ret_filename, ctx->zocket);

    if (system(buf) != 0) Abort("Could not launch 'probcli', make sure it is on your PATH");

    prob_load_model(model);
}

static ProBState
pins2prob_state(model_t model, int* pins)
{
    prob_context_t* ctx = (prob_context_t*) GBgetContext(model);

    ProBState prob;

    prob.size = ctx->num_vars;
    prob.chunks = RTmalloc(sizeof(ProBChunk) * prob.size);

    Debugf("pins2prob state (%d): ", ctx->num_vars);
    for (size_t i = 0; i < ctx->num_vars; i++) {
        chunk c = pins_chunk_get (model, ctx->var_type[i], pins[i]);

        prob.chunks[i].data = c.data;
        prob.chunks[i].size = c.len;
        Debugf("(%u)", c.len);
#ifdef LTSMIN_DEBUG
        for (unsigned int j = 0; j < c.len; j++) Debugf("%x", c.data[j]);
#endif

        Debugf(",");
    }
    Debugf("\n");

    return prob;
}

static void
prob2pins_state(ProBState s, int *state, model_t model)
{
    prob_context_t* ctx = (prob_context_t*) GBgetContext(model);
    HREassert(s.size == ctx->num_vars, "expecting %zu chunks, but got %zu", ctx->num_vars, s.size);

    Debugf("prob2pins state (%zu): ", s.size);
    for (size_t i = 0; i < s.size; i++) {
        chunk c;
        c.data = s.chunks[i].data;
        c.len = s.chunks[i].size;
        Debugf("(%u)", c.len);
#ifdef LTSMIN_DEBUG
        for (unsigned int j = 0; j < c.len; j++) Debugf("%x", c.data[j]);
#endif
        Debugf(",");
        int chunk_id = pins_chunk_put (model, ctx->var_type[i], c);
        state[i] = chunk_id;
    }
    Debugf("\n");
}

static int
get_successors_long(model_t model, int group, int *src, TransitionCB cb, void *ctx)
{
    prob_context_t* prob_ctx = (prob_context_t*) GBgetContext(model);

    /* Don't give any successors for the init group if we have already initialized.
     * This prevents adding a self loop to the initial state. */
    if (group == 0 && src[prob_ctx->num_vars] == 1) return 0;

    // Don't give any successors for groups other than the init group if we have not initialized
    if (group > 0 && src[prob_ctx->num_vars] == 0) return 0;

    int operation_type = prob_ctx->op_type_no;

    chunk op_name = pins_chunk_get (model, operation_type, prob_ctx->op_type[group]);

    ProBState prob = pins2prob_state(model, src);

    int nr_successors;
    ProBState *successors = prob_next_state(prob_ctx->prob_client, prob, op_name.data, &nr_successors);
    prob_destroy_state(&prob);

    int s[prob_ctx->num_vars + 1];
    s[prob_ctx->num_vars] = 1;
    for (int i = 0; i < nr_successors; i++) {

        int transition_labels[1] = { prob_ctx->op_type[group] };
        transition_info_t transition_info = { transition_labels, group, 0 };

        prob2pins_state(successors[i], s, model);
        prob_destroy_state(successors + i);
        cb(ctx, &transition_info, s, NULL);
    }

    RTfree(successors);

    return nr_successors;
}

void
prob_exit(model_t model)
{
    prob_context_t* ctx = (prob_context_t*) GBgetContext(model);

    if (!no_close) {
        Warning(info, "terminating ProB connection");
        prob_terminate(ctx->prob_client);
        Warning(info, "disconnecting from zocket %s", prob_get_zocket(ctx->prob_client));
        prob_disconnect(ctx->prob_client);
        prob_client_destroy(ctx->prob_client);
    }
}

static void
prob_load_model(model_t model)
{
    Warning(info, "ProB init");

    prob_context_t* ctx = (prob_context_t*) GBgetContext(model);

    ctx->prob_client = prob_client_create();

    const char* ipc = "ipc://";
    char zocket[strlen(ipc) + strlen(ctx->zocket) + 1];
    sprintf(zocket, "%s%s", ipc, ctx->zocket);
    RTfree(ctx->zocket);

    Warning(info, "connecting to zocket %s", zocket);
    prob_connect(ctx->prob_client, zocket);

    ProBInitialResponse init = prob_init(ctx->prob_client);

    lts_type_t ltstype = lts_type_create();

    // add an "Operation" type for edge labels
    int is_new = 0;
    ctx->op_type_no = lts_type_add_type(ltstype, "Operation", &is_new);
    if (!is_new) Abort("Can not add type");
    lts_type_set_format(ltstype, ctx->op_type_no, LTStypeChunk);

    lts_type_set_edge_label_count(ltstype, 1);
    lts_type_set_edge_label_name(ltstype, 0, "action");
    lts_type_set_edge_label_typeno(ltstype, 0, ctx->op_type_no);

    // init state labels
    const int sl_size = 0;
    lts_type_set_state_label_count(ltstype, sl_size);
    const int bool_type = lts_type_add_type(ltstype, "Boolean", NULL);
    lts_type_set_format(ltstype, bool_type, LTStypeBool);

    ctx->num_vars = init.variables.size;

    lts_type_set_state_length(ltstype, ctx->num_vars + 1);

    /* One state variable is artificial to make sure the DA$INIT_STATE group
     * can only fire when we are in the initial state. This is necessary when
     * doing backward reachability. */
    lts_type_set_state_name(ltstype, ctx->num_vars, IS_INIT);
    lts_type_set_state_typeno(ltstype, ctx->num_vars, bool_type);

    ctx->var_type = RTmalloc(sizeof(int[ctx->num_vars]));

    string_index_t var_si = SIcreate();
    for (size_t i = 0; i < ctx->num_vars; i++) {
        const char* type = init.variable_types.chunks[i].data;

        HREassert(type != NULL, "invalid type name");

        int is_new = 0;
        const int type_no = lts_type_add_type(ltstype, type, &is_new);
        ctx->var_type[i] = type_no;

        if (is_new) lts_type_set_format(ltstype, type_no, LTStypeChunk);

        const char* name = init.variables.chunks[i].data;
        if (strcmp(name, IS_INIT) == 0) {
            Abort("State variable name \"%s\" is reserved, please use another name", name);
        }
        SIputAt(var_si, name, i);

        lts_type_set_state_name(ltstype, i, name);
        lts_type_set_state_typeno(ltstype, i, type_no);
    }

    // done with ltstype
    lts_type_validate(ltstype);

    // make sure to set the lts-type before anything else in the GB
    GBsetLTStype(model, ltstype);

    const int num_groups = init.transition_groups.size;

    ctx->op_type = RTmalloc(sizeof(int[num_groups]));

    string_index_t op_si = SIcreate();
    for (int i = 0; i < num_groups; i++) {
        const char* name = init.transition_groups.chunks[i].data;
        const int at = pins_chunk_put (model, ctx->op_type_no, chunk_str(name));
        ctx->op_type[i] = at;
        SIputAt(op_si,name,i);
    }

    matrix_t* must_write = RTmalloc(sizeof(matrix_t));
    matrix_t* read = RTmalloc(sizeof(matrix_t));
    matrix_t* dm = RTmalloc(sizeof(matrix_t));
    dm_create(must_write, num_groups, ctx->num_vars + 1);
    dm_create(read, num_groups, ctx->num_vars + 1);

    GBsetDMInfoMustWrite(model, must_write);
    GBsetDMInfoRead(model, read);

    GBsetDMInfo(model, dm);

    matrix_t* sl_info = RTmalloc(sizeof(matrix_t));
    dm_create(sl_info, 0, 0);
    GBsetStateLabelInfo(model, sl_info);

    // set all variables for init group to write dependent
    for (size_t i = 0; i < ctx->num_vars + 1; i++) dm_set(must_write, 0, i);
    // also set the init var to read dependent for all groups
    for (int i = 0; i < num_groups; i++) dm_set(read, i, ctx->num_vars);

    for (size_t i = 0; i < init.must_write.nr_rows; i++) {
        const char* name = init.must_write.rows[i].transition_group.data;
        const int vars = init.must_write.rows[i].variables.size;
        const int row = SIlookup(op_si, name);
        for (int j = 0; j < vars; j++) {
            const char* var = init.must_write.rows[i].variables.chunks[j].data;
            const int col = SIlookup(var_si, var);
            dm_set(must_write, row, col);
        }
    }

    for (size_t i = 0; i < init.may_write.nr_rows; i++) {
        const char* name = init.may_write.rows[i].transition_group.data;
        const int vars = init.may_write.rows[i].variables.size;
        const int row = SIlookup(op_si, name);
        for (int j = 0; j < vars; j++) {
            const char* var = init.may_write.rows[i].variables.chunks[j].data;
            const int col = SIlookup(var_si, var);
            dm_set(must_write, row, col);
            dm_set(read, row, col);
        }
    }

    for (size_t i = 0; i < init.reads_action.nr_rows; i++) {
        const char* name = init.reads_action.rows[i].transition_group.data;
        const int vars = init.reads_action.rows[i].variables.size;
        const int row = SIlookup(op_si, name);
        for (int j = 0; j < vars; j++) {
            const char* var = init.reads_action.rows[i].variables.chunks[j].data;
            const int col = SIlookup(var_si, var);
            dm_set(read, row, col);
        }
    }

    for (size_t i = 0; i < init.reads_guard.nr_rows; i++) {
        const char* name = init.reads_guard.rows[i].transition_group.data;
        const int vars = init.reads_guard.rows[i].variables.size;
        const int row = SIlookup(op_si, name);
        for (int j = 0; j < vars; j++) {
            const char* var = init.reads_guard.rows[i].variables.chunks[j].data;
            const int col = SIlookup(var_si, var);
            dm_set(read, row, col);
        }
    }

    dm_copy(must_write, dm);
    dm_apply_or(dm, read);

    int init_state[ctx->num_vars + 1];
    prob2pins_state(init.initial_state, init_state, model);
    init_state[ctx->num_vars] = 0;
    GBsetInitialState(model, init_state);

    prob_destroy_initial_response(&init);

    GBsetNextStateLong(model, get_successors_long);

    GBsetExit(model, prob_exit);

    SIdestroy(&var_si);
    SIdestroy(&op_si);
}
