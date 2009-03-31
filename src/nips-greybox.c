#include <stdio.h>
#include <assert.h>

#include "stringindex.h"
#include "nips-greybox.h"
#include "runtime.h"
#include <nips-vm/nipsvm.h>
#include <nips-vm/bytecode.h>
#include <nips-vm/state_parts.h>

static void nips_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST: {
		nipsvm_module_init ();
		GBregisterLoader("b",NIPSloadGreyboxModel);
		Warning(info,"NIPS language module initialized");
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to nips_popt");
}
struct poptOption nips_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , nips_popt , 0 , NULL ,NULL},
	POPT_TABLEEND
};

static const size_t MAX_INITIAL_STATE_COUNT = 10000;
static const size_t MAX_NIPSVM_STATE_SIZE   = 65536;

static lts_type_t ltstype;
static struct edge_info e_info;
static struct state_info s_info = { 0, NULL, NULL };

int ILABEL_TAU  = -1;

static t_pid        NIPSgroupPID (int, nipsvm_state_t *);

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
    int                 state_length;
    TransitionCB        callback;
    void               *context;

    nipsvm_state_t     *source_state;
    int                 group;

    nipsvm_errorcode_t  err;
    nipsvm_pid_t        pid;
    nipsvm_pc_t         pc;
};

static struct gb_context_s *
init_gb_context (struct gb_context_s *gb_ctx, model_t model,
                 TransitionCB cb, void *context,
                 nipsvm_state_t *source, int group)
{
    gb_ctx->model = model;
    gb_ctx->state_length = lts_type_get_state_length(GBgetLTStype(model));
    gb_ctx->callback = cb;
    gb_ctx->context = context;
    gb_ctx->source_state = source;
    gb_ctx->group = group;
    gb_ctx->err = 0;
    return gb_ctx;
}

static const int GB_NO_GROUP = -1;

static nipsvm_status_t
scheduler_callback (size_t succ_size, nipsvm_state_t *succ,
                    nipsvm_transition_information_t *ti, void *context)
{
    struct gb_context_s *gb_context = context;
    struct part_context_s part_ctx;
    (void)succ_size;
    (void)ti;

    const unsigned int NIPS_UNSUPPORTED_FEATURES_MASK =
        INSTR_SUCC_CB_FLAG_SYNC   |
        INSTR_SUCC_CB_FLAG_TIMEOUT;
    assert (succ != NULL);
    assert (!(ti->succ_cb_flags & NIPS_UNSUPPORTED_FEATURES_MASK));
    assert (ti->step_info);
    assert (!ti->step_info->previous);
    
    if (gb_context->group != GB_NO_GROUP &&
        NIPSgroupPID (gb_context->group, succ) != ti->step_info->pid)
        return IC_CONTINUE;

    /* Continue until atomic flag is unset.  Downside: a model checker
     * does not see these intermediate "atomic" states.
     */
    if (succ->excl_pid != 0)
        return IC_CONTINUE_INVISIBLY;
    
    size_t          ilen = gb_context->state_length;
    int             ivec[ilen];
    int             ilabel[] = { ILABEL_TAU };
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
static t_pid
NIPSgroupPID (int group, nipsvm_state_t *succ)
// This crucially depends on the group setup done in
// NIPSgetProjection.
{
    char               *ptr;
    assert (group != GB_NO_GROUP);

    ptr = (char *)succ + sizeof (st_global_state_header) // header
        + be2h_16 (succ->gvar_sz);                       // variables

    for (int i = 0; i < group; ++i) {
        assert (i < succ->proc_cnt);
        ptr += process_size ((st_process_header *)ptr); // next process
    }

    t_pid group_pid = be2h_pid (((st_process_header *)ptr)->pid);
    return group_pid;
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

extern void
NIPSstateRestore (model_t model, int *src, char *state, size_t sz)
{
    assert (state != NULL);
    struct part_context_s part_ctx;
    init_part_context (&part_ctx, model, src, -1);
    state_restore (state, sz,
                   Rpart_glob_callback,
                   Rpart_proc_callback, Rpart_chan_callback, &part_ctx);
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

    init_gb_context (&gb_context, model, cb, context,
                     (nipsvm_state_t *)state, GB_NO_GROUP);
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

    init_gb_context (&gb_context, model, cb, context,
                     (nipsvm_state_t *)state, group);
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

    if (succ->excl_pid != 0)
        return IC_CONTINUE_INVISIBLY;

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

void
NIPSloadGreyboxModel (model_t m, const char *filename)
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

    ltstype=lts_type_create();
    if (lts_type_add_type(ltstype,"globals",NULL) != 0) {
        Fatal(1,error,"wrong type number");
    }
    if (lts_type_add_type(ltstype,"process",NULL) != 1) {
        Fatal(1,error,"wrong type number");
    }
    if (lts_type_add_type(ltstype,"channel",NULL) != 2) {
        Fatal(1,error,"wrong type number");
    }
    int label_type;
    if ((label_type = lts_type_add_type (ltstype,"label",NULL)) != 3) {
        Fatal(1,error,"wrong type number");
    }
    int state_length=Cpart_ctx.count;
    lts_type_set_state_length(ltstype,state_length);
    lts_type_set_edge_label_count(ltstype,1);
    lts_type_set_edge_label_name(ltstype,0,"label");
    lts_type_set_edge_label_type(ltstype,0,"label");

    GBsetLTStype (m, ltstype);
    ILABEL_TAU = GBchunkPut(m, label_type, chunk_str("tau"));

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
        int                 temp[state_length];
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
