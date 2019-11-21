/**
 *
 */

#include <hre/config.h>

#include <hre/stringindex.h>

#include <ltsmin-lib/ltsmin-standard.h>
#include <pins2lts-mc/algorithm/ltl.h>
#include <pins2lts-mc/parallel/global.h>

int              ecd = 1;

struct poptOption alg_ltl_options[] = {
    {"no-ecd", 0, POPT_ARG_VAL | POPT_ARGFLAG_DOC_HIDDEN, &ecd, 0,
     "turn off early cycle detection (NNDFS/MCNDFS)", NULL},
    POPT_TABLEEND
};

typedef struct trace_ctx_s {
    state_info_t       *state;
    string_index_t      si;             // Trace index
} trace_ctx_t;

static void *
get_stack_state (ref_t ref, void *arg)
{
    trace_ctx_t        *ctx = (trace_ctx_t *) arg;
    trace_info_t       *state = (trace_info_t     *) SIget(ctx->si, ref);
    state_info_set (ctx->state, state->val.ref, state->val.lattice);
    state_data_t        data  = state_info_pins_state (ctx->state);
    Debug ("Trace %zu (%zu,%zu)", ref, state->val.ref, state->val.lattice);
    return data;
}

static inline void
new_state (trace_info_t     *out, state_info_t *si)
{
    out->val.ref = si->ref;
    out->val.lattice = si->lattice;
}

void
find_and_write_dfs_stack_trace (model_t model, dfs_stack_t stack, bool is_lasso)
{
    size_t              level = dfs_stack_nframes (stack) + 1;
    trace_ctx_t         ctx;
    ctx.state = state_info_create ();
    ref_t              *trace = RTmalloc (sizeof(ref_t) * level);
    trace_info_t        state;
    ctx.si = SIcreate();
    for (int i = level - 1; i >= 0; i--) {
        state_data_t        data = dfs_stack_peek_top (stack, i);
        state_info_deserialize (ctx.state, data);
        new_state (&state, ctx.state);
        Warning (infoLong, "%zu\t(%zu),", ctx.state->ref, level - i);
        int val = SIputC (ctx.si, state.data, sizeof(struct val_s));
        trace[level - i - 1] = (ref_t) val;
    }
    if (is_lasso && trace[level-1] >= level - 1) {
        Warning (lerror, "Trace has no cycle. Writing erroneous trace for debugging purposes.");
        global->exit_status = LTSMIN_EXIT_FAILURE;
    }
    trc_env_t          *trace_env = trc_create (model, get_stack_state, &ctx);
    Warning (info, "Writing trace to %s", trc_output);
    trc_write_trace (trace_env, trc_output, trace, level);
    SIdestroy (&ctx.si);
    RTfree (trace);
}

void
ndfs_report_cycle (wctx_t *ctx, model_t model, dfs_stack_t stack,
                   state_info_t *cycle_closing_state)
{
    (void)model;
    global->exit_status = LTSMIN_EXIT_COUNTER_EXAMPLE;
    /* Stop other workers, exit if some other worker was first here */
    if ( no_exit || !run_stop(ctx->run) )
        return;
    size_t              level = dfs_stack_nframes (stack) + 1;
    Warning (info, " ");
    Warning (info, "Accepting cycle FOUND at depth %zu!", level);
    Warning (info, " ");
    if (trc_output && ctx->counter_example == 0) {
        state_info_set (ctx->ce_state, cycle_closing_state->ref, cycle_closing_state->lattice);
        ctx->counter_example = 1; // delay, to avoid re-entrancy of GBgetTrans
        //find_and_write_dfs_stack_trace (model, stack, true);
    }
}

