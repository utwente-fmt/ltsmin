#include <stdio.h>
#include <assert.h>

#include "stringindex.h"
#include "nips-greybox.h"
#include "runtime.h"
#include <nips-vm/nipsvm.h>
#include <nips-vm/bytecode.h>
#include <nips-vm/state_parts.h>

static const size_t MAX_INITIAL_STATE_COUNT = 10000;
static const size_t MAX_NIPSVM_STATE_SIZE   = 65536;

static struct lts_structure_s ltstype;
static struct edge_info e_info;
static struct state_info s_info = { 0, NULL, NULL };

static int          NIPSgroupCheck (int, nipsvm_state_t *,
                                    nipsvm_transition_information_t *);

/* Debugging ============================================== */
#ifdef DEBUG
void
DBG_print_chunk (char *tag, int type, int cid, size_t len, char *data)
{
    fprintf (stderr, "\n%s, Chunk type:%2d, ID:%2d, len:%2lu, data:\n    ",
             tag, type, cid, len);
    unsigned int        i;
    for (i = 0; i < len; ++i) {
        fprintf (stderr, "%02x ", (unsigned char)data[i]);
    }
    fprintf (stderr, "\n");
}

void
DBG_print_ivec (char *tag, size_t len, int *ivec)
{
    fprintf (stderr, "\n%s, intvec:", tag);
    unsigned int        i;
    for (i = 0; i < len; ++i) {
        fprintf (stderr, " %02x", ivec[i]);
    }
    fprintf (stderr, "\n");
}
#else
#define DBG_print_chunk(...)
#define DBG_print_ivec(...)
#endif

/* Parts ================================================== */
struct Cpart_context_s {
    size_t              count;
    size_t              nglobals;
    size_t              nprocs;
    size_t              nchans;
};

static struct Cpart_context_s *
init_Cpart_context (struct Cpart_context_s *p_ctx)
{
    p_ctx->count = 0;
    p_ctx->nglobals = 0;
    p_ctx->nprocs = 0;
    p_ctx->nchans = 0;
    return p_ctx;
}

static void
Cpart_glob_callback (char *data, unsigned int len, void *ctx)
{
    struct Cpart_context_s *p_ctx = ctx;
    (void)data;
    (void)len;
    ++p_ctx->count;
    ++p_ctx->nglobals;
}

static void
Cpart_proc_callback (char *data, unsigned int len, void *ctx)
{
    struct Cpart_context_s *p_ctx = ctx;
    (void)data;
    (void)len;
    ++p_ctx->count;
    ++p_ctx->nprocs;
}

static void
Cpart_chan_callback (char *data, unsigned int len, void *ctx)
{
    struct Cpart_context_s *p_ctx = ctx;
    (void)data;
    (void)len;
    ++p_ctx->count;
    ++p_ctx->nchans;
}

struct part_context_s {
    model_t             model;
    int                 group;
    size_t              count;
    size_t              len;
    int                *ivec;
};

static struct part_context_s *
init_part_context (struct part_context_s *p_ctx, model_t model,
                   int *ivec, size_t len)
{
    p_ctx->model = model;
    p_ctx->count = 0;
    p_ctx->len = len;
    p_ctx->ivec = ivec;
    return p_ctx;
}

static void
part_glob_callback (char *data, unsigned int len, void *ctx)
{
    struct part_context_s *p_ctx = ctx;
    assert (p_ctx->count <= p_ctx->len);
    int                 i;
    p_ctx->ivec[p_ctx->count++] = i =
        GBchunkPut (p_ctx->model, 0, chunk_ld (len, data));
    DBG_print_chunk ("Put", 0, i, len, data);
}

static void
part_proc_callback (char *data, unsigned int len, void *ctx)
{
    struct part_context_s *p_ctx = ctx;
    int                 i;
    assert (p_ctx->count <= p_ctx->len);
    p_ctx->ivec[p_ctx->count++] = i =
        GBchunkPut (p_ctx->model, 1, chunk_ld (len, data));
    DBG_print_chunk ("Put", 1, i, len, data);
}

static void
part_chan_callback (char *data, unsigned int len, void *ctx)
{
    struct part_context_s *p_ctx = ctx;
    assert (p_ctx->count <= p_ctx->len);
    int                 i;
    p_ctx->ivec[p_ctx->count++] = i =
        GBchunkPut (p_ctx->model, 2, chunk_ld (len, data));
    DBG_print_chunk ("Put", 2, i, len, data);
}


static unsigned int
Rpart_callback (int type, char *buf, unsigned int buf_len, void *ctx)
{
    struct part_context_s *p_ctx = ctx;
    int                 cid;
    chunk               chunk;

    cid = p_ctx->ivec[p_ctx->count++];
    chunk = GBchunkGet (p_ctx->model, type, cid);
    DBG_print_chunk ("Get", type, cid, chunk.len, chunk.data);
    if (buf_len < chunk.len)
        return 0;
    memcpy (buf, chunk.data, chunk.len);
    return chunk.len;
}

static unsigned int
Rpart_glob_callback (char *data, unsigned int len, void *ctx)
{
    return Rpart_callback (0, data, len, ctx);
}

static unsigned int
Rpart_proc_callback (char *data, unsigned int len, void *ctx)
{
    return Rpart_callback (1, data, len, ctx);
}

static unsigned int
Rpart_chan_callback (char *data, unsigned int len, void *ctx)
{
    return Rpart_callback (2, data, len, ctx);
}

/* NIPS next-state callback ============================= */
struct gb_context_s {
    model_t             model;
    TransitionCB        callback;
    void               *context;

    int                 group;

    nipsvm_errorcode_t  err;
    nipsvm_pid_t        pid;
    nipsvm_pc_t         pc;
};

static struct gb_context_s *
init_gb_context (struct gb_context_s *gb_ctx, model_t model,
                 TransitionCB cb, void *context, int group)
{
    gb_ctx->model = model;
    gb_ctx->callback = cb;
    gb_ctx->context = context;
    gb_ctx->group = group;
    gb_ctx->err = 0;
    return gb_ctx;
}

static nipsvm_status_t
scheduler_callback (size_t succ_size, nipsvm_state_t *succ,
                    nipsvm_transition_information_t *ti, void *context)
{
    struct gb_context_s *gb_context = context;
    struct part_context_s part_ctx;
    size_t              ilen = GBgetLTStype (gb_context->model)->state_length;
    int                 ivec[ilen];
    int                 ilabel[] = { 0 };       /* XXX */
    (void)succ_size;
    (void)ti;

    assert (succ != NULL);
    assert ((!(ti->succ_cb_flags & INSTR_SUCC_CB_FLAG_SYNC) &&
             ti->step_info && !ti->step_info->previous));

    if (gb_context->group != -1) {
        if (!NIPSgroupCheck (gb_context->group, succ, ti))
            return IC_CONTINUE;
    }

    init_part_context (&part_ctx, gb_context->model, ivec, ilen);
    state_parts (succ, part_glob_callback, part_proc_callback,
                 part_chan_callback, &part_ctx);
    assert (ilen == part_ctx.count);
    DBG_print_ivec ("ENCODE", ilen, ivec);

    gb_context->callback (gb_context->context, ilabel, ivec);
    return IC_CONTINUE;
}

static nipsvm_status_t
error_callback (nipsvm_errorcode_t err,
                nipsvm_pid_t pid, nipsvm_pc_t pc, void *context)
{
    struct gb_context_s *gb_context = context;
    gb_context->err = err;
    gb_context->pid = pid;
    gb_context->pc = pc;
    return IC_STOP;
}

/* Transition groups ====================================== */
static int
NIPSgroupCheck (int group,
                nipsvm_state_t *succ, nipsvm_transition_information_t *ti)
// This crucially depends on the group setup done in
// NIPSgetProjection.
{
    char               *ptr;
    int                 i;
    ptr = (char *)succ + sizeof (st_global_state_header)        // header
        + be2h_16 (succ->gvar_sz);     // variables
    for (i = 0; i < succ->proc_cnt; ++i) {
        t_pid               pid_be = h2be_pid (ti->step_info->pid);
        if (((st_process_header *)ptr)->pid == pid_be) {
#ifdef DEBUG
            Warning (info, "pid %d modified, group %d, %s\n",
                     ti->step_info->pid, group,
                     i != group ? "DISREGARD" : "OK");
#endif
            if (i != group) {
                // other group -> not okay
                return 0;
            }
        }

        ptr += process_size ((st_process_header *)ptr); // next process
    }
    return 1;
}

size_t
NIPSgetProjection (nipsvm_t *vm, struct Cpart_context_s * Cpart_ctx,
                   int *projvec, int proj)
{
    size_t              k = 0;
    size_t              i;
    (void)vm;

    for (i = 0; i < Cpart_ctx->nglobals; ++i)
        projvec[k++] = i;

    projvec[k++] = Cpart_ctx->nglobals + proj;

    for (i = 0; i < Cpart_ctx->nchans; ++i)
        projvec[k++] = Cpart_ctx->nglobals + Cpart_ctx->nprocs + i;

    return k;
}

/* Transition functions =================================== */
void
NIPSinitGreybox (int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    nipsvm_module_init ();
}

nipsvm_t           *
NIPSgetVM (model_t model)
{
    return GBgetContext (model);
}

int
NIPSgetTransitionsAll (model_t model, int *src, TransitionCB cb,
                       void *context)
{
    struct gb_context_s gb_context;
    struct part_context_s part_ctx;
    char                state[MAX_NIPSVM_STATE_SIZE];
    (void)model;

    DBG_print_ivec ("RESTORE", GBgetLTStype (model)->state_length, src);
    init_part_context (&part_ctx, model, src, -1);
    state_restore (state, count (state),
                   Rpart_glob_callback,
                   Rpart_proc_callback, Rpart_chan_callback, &part_ctx);

    init_gb_context (&gb_context, model, cb, context, -1);
    nipsvm_scheduler_iter (NIPSgetVM (model), (nipsvm_state_t *)state,
                           &gb_context);

    return 0;
}

int
NIPSgetTransitionsLong (model_t model, int group, int *src,
                        TransitionCB cb, void *context)
{
    struct gb_context_s gb_context;
    struct part_context_s part_ctx;
    char                state[MAX_NIPSVM_STATE_SIZE];
    (void)model;
    (void)group;

    init_part_context (&part_ctx, model, src, -1);
    state_restore (state, count (state),
                   Rpart_glob_callback,
                   Rpart_proc_callback, Rpart_chan_callback, &part_ctx);

    init_gb_context (&gb_context, model, cb, context, group);
    nipsvm_scheduler_iter (NIPSgetVM (model), (nipsvm_state_t *)state,
                           &gb_context);

    return 0;
}

typedef struct {
    nipsvm_state_t    **stack;
    size_t              depth;
    const size_t        nmaxdepth;
    size_t              ntransitions;
} search_context_t;

static nipsvm_status_t
ISscheduler_callback (size_t succ_size, nipsvm_state_t *succ,
                      nipsvm_transition_information_t *ti, void *context)
{
    search_context_t   *sc = context;
    (void)ti;

    if (sc->depth >= sc->nmaxdepth)
        return IC_STOP;
    ++sc->ntransitions;
    sc->stack[sc->depth++] = memcpy (RTmalloc (succ_size), succ, succ_size);
    return IC_CONTINUE;
}

static nipsvm_state_t *
NIPSfindInitializedState (nipsvm_bytecode_t *bytecode, size_t nmax_states)
{
    nipsvm_state_t     *stack[nmax_states];
    nipsvm_state_t     *initial = NULL;
    search_context_t    sc = {
        .stack = stack,
        .depth = 0,
        .nmaxdepth = nmax_states,
        .ntransitions = 0,
    };
    string_index_t      visited = SIcreate ();
    nipsvm_t            vm;

    nipsvm_init (&vm, bytecode, ISscheduler_callback, NULL);

    size_t              count = 0;
    nipsvm_state_t     *s = nipsvm_initial_state (&vm);
    size_t              sz = nipsvm_state_size (s);
    sc.stack[sc.depth++] = memcpy (RTmalloc (sz), s, sz);
    for (; sc.depth-- > 0 && count < nmax_states;) {
        nipsvm_state_t     *state = sc.stack[sc.depth];
        if (SIlookupC (visited, (void *)state, nipsvm_state_size (state))
            != SI_INDEX_FAILED)
            continue;
        ++count;
        SIputC (visited, (void *)state, nipsvm_state_size (state));
        if (nipsvm_state_creative (&vm, state)) {
            nipsvm_scheduler_iter (&vm, state, &sc);
            free (state);
        } else if (initial == NULL) {
            initial = state;
            sz = nipsvm_state_size (initial);
        } else if (nipsvm_state_compare (initial, state, sz) != 0) {
            Warning (info, "Mismatching non-creative states:");
            global_state_print (initial);
            global_state_print (state);
            /* XXX free things here */
            initial = NULL;
            break;
        }
    }
    nipsvm_finalize (&vm);
    if (count >= nmax_states) {
        Warning (info,
                 "%d states explored, and still not fully initialized.",
                 count);
        initial = NULL;
    } else if (initial != NULL) {
        Warning (info, "Fully initialized state found: "
                 "%d states explored, %d transitions.",
                 count - 1, sc.ntransitions);
    }
    return initial;
}

static char        *edge_name[] = { "action" };
static int          edge_type[] = { 3 };
static char        *NIPS_types[] = { "globals", "process", "channel", "label" };

void
NIPSloadGreyboxModel (model_t m, char *filename)
{
    st_bytecode        *bytecode;
    nipsvm_t           *vm = RTmalloc (sizeof *vm);

    bytecode = bytecode_load_from_file (filename, NULL);
    if (bytecode == NULL) {
        FatalCall (1, error, "Failed to open %s.", filename);
        return;
    }
    if (nipsvm_init (vm, bytecode, scheduler_callback, error_callback) !=
        0) {
        Fatal (1, error, "Could not initialize VM.");
        return;
    }
    GBsetContext (m, vm);

    nipsvm_state_t     *initial;
    initial = NIPSfindInitializedState (bytecode, MAX_INITIAL_STATE_COUNT);
    if (initial == NULL) {
        Fatal (1, error, "Could not obtain initial state.");
    }

    struct Cpart_context_s Cpart_ctx;
    init_Cpart_context (&Cpart_ctx);
    state_parts (initial, Cpart_glob_callback, Cpart_proc_callback,
                 Cpart_chan_callback, &Cpart_ctx);

    ltstype.state_length = Cpart_ctx.count;
    ltstype.type_count = count (NIPS_types);
    ltstype.type_names = NIPS_types;
    ltstype.visible_count = 0;
    ltstype.visible_indices = NULL;
    ltstype.visible_name = NULL;
    ltstype.visible_type = NULL;
    ltstype.state_labels = 0;
    ltstype.state_label_name = NULL;
    ltstype.state_label_type = NULL;
    ltstype.edge_labels = count (edge_name);
    ltstype.edge_label_name = edge_name;
    ltstype.edge_label_type = edge_type;
    GBsetLTStype (m, &ltstype);
    GBchunkPut(m, 3, chunk_str("dummy")); /* XXX */

    struct part_context_s part_ctx;
    int                 ivec[Cpart_ctx.count];
    init_part_context (&part_ctx, m, ivec, count (ivec));
    state_parts (initial, part_glob_callback, part_proc_callback,
                 part_chan_callback, &part_ctx);
    GBsetInitialState (m, ivec);

    e_info.groups = Cpart_ctx.nprocs;
    e_info.length = RTmalloc (e_info.groups * sizeof (int));
    e_info.indices = RTmalloc (e_info.groups * sizeof (int *));
    for (int i = 0; i < e_info.groups; ++i) {
        int                 temp[ltstype.state_length];
        e_info.length[i] = NIPSgetProjection (vm, &Cpart_ctx, temp, i);
        e_info.indices[i] = RTmalloc (e_info.length[i] * sizeof (int));
        for (int j = 0; j < e_info.length[i]; ++j)
            e_info.indices[i][j] = temp[j];
    }

    GBsetEdgeInfo (m, &e_info);
    GBsetStateInfo (m, &s_info);
    GBsetNextStateLong (m, NIPSgetTransitionsLong);
    GBsetNextStateAll (m, NIPSgetTransitionsAll);
}
