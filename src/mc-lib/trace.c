/*
 * trace.c
 *
 *  Created on: Jun 14, 2010
 *      Author: laarman
 */

#include <hre/config.h>

#include <hre/stringindex.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <mc-lib/trace.h>
#include <util-lib/treedbs.h>


typedef struct write_trace_step_s {
    int                 src_no;
    int                 dst_no;
    int                *dst;
    int                 found;
    trc_env_t          *env;
} write_trace_step_t;

struct trc_env_s {
    int                 N;
    model_t             model;
    trc_get_state_f     get_state;
    void               *get_state_arg;
    int                 state_labels;
    lts_file_t          trace_handle;
};

trc_env_t *
trc_create (model_t model, trc_get_state_f get, void *arg)
{
    trc_env_t          *trace = RTmalloc(sizeof(trc_env_t));
    lts_type_t          ltstype = GBgetLTStype (model);
    trace->N = lts_type_get_state_length (ltstype);
    trace->state_labels = lts_type_get_state_label_count (ltstype);
    trace->get_state = get;
    trace->get_state_arg = arg;
    trace->model = model;
    return trace;
}

static void 
write_trace_state(trc_env_t *env, int *state)
{
    int                 labels[env->state_labels];
    if (env->state_labels) GBgetStateLabelsAll(env->model, state, labels);
    lts_write_state (env->trace_handle, 0, state, labels);
}

static void 
write_trace_next (void *arg, transition_info_t *ti, int *dst, int *cpy)
{
    (void) cpy;
    write_trace_step_t *ctx = (write_trace_step_t*)arg;
    ti->por_proviso = 0;
    if (ctx->found) return;
    if (0 != memcmp(ctx->dst, dst, sizeof(int[ctx->env->N]))) return;
    ctx->found = 1;
    lts_write_edge (ctx->env->trace_handle, 0, &ctx->src_no, 0, &ctx->dst_no, ti->labels);
}

static void
write_trace_step (write_trace_step_t *ctx, int *src)
{
    Warning (debug,"finding edge for state %d", ctx->src_no);
    ctx->found = 0;
    GBgetTransitionsAll (ctx->env->model, src, write_trace_next, ctx);
    if (ctx->found==0) Abort("no matching transition found for %u", ctx->dst_no);
}

static void
write_trace (trc_env_t *env, size_t trace_size, ref_t *trace)
{
    string_index_t      si = SIcreate();
    size_t              i = 1;
    int                 last;
    int                 src[env->N];
    write_trace_step_t  ctx;
    ctx.env = env;
    ctx.dst = env->get_state (trace[0], env->get_state_arg);
    ctx.dst_no = SIputC (si, (const char *)&trace[0], sizeof(ref_t));
     // write initial state
    write_trace_state (env, ctx.dst);

    while (i < trace_size) {
        memcpy(src, ctx.dst, sizeof(int[env->N]));
        ctx.src_no = ctx.dst_no;
        ctx.dst = env->get_state (trace[i], env->get_state_arg);
        ctx.dst_no = last = SIlookupC(si, (const char *)&trace[i], sizeof(ref_t));
        if (ctx.dst_no == SI_INDEX_FAILED) {
            ctx.dst_no = SIputC (si, (const char *)&trace[i], sizeof(ref_t));
        }
        write_trace_step (&ctx, src);                           // write step
        if (last == SI_INDEX_FAILED)
            write_trace_state (env, ctx.dst);                   // write dst
        i++;
    }
    SIdestroy (&si);
}

ref_t *
trc_find_trace (ref_t dst_idx, int level, ref_t *parent_ofs, ref_t start_idx,
                size_t *length)
{
    rt_timer_t timer = RTcreateTimer ();
    RTstartTimer (timer);
    /* Other workers may have influenced the trace, writing to parent_ofs.
     * we artificially limit the length of the trace to 10 times that of the
     * found one */
    size_t              max_length = level * 10;
    ref_t              *trace = RTmalloc(sizeof(ref_t) * max_length);
    if (trace == NULL)
        Abort("unable to allocate memory for trace");
    int                 i = max_length - 1;
    ref_t               curr_idx = dst_idx;
    trace[i] = curr_idx;
    while(curr_idx != start_idx) {
        i--;
        if (i < 0)
            Abort("Trace length 10x longer than initially found trace. Giving up.");
        curr_idx = parent_ofs[curr_idx];
        trace[i] = curr_idx;
    }
    Warning (info, "reconstructed trace length: %zu", max_length - i);

    RTstopTimer (timer);
    RTprintTimer (info, timer, "constructing the trace took");
    *length = max_length - i;

    ref_t              *trace2 = RTmalloc(sizeof(ref_t) * *length);
    for (size_t x = 0; x < *length; x++) {
        trace2[x] = trace[i + x];
    }
    RTfree (trace);
    return trace2;
}

void
trc_find_and_write (trc_env_t *env, char *trc_output, ref_t dst_idx,
                    int level, ref_t *parent_ofs, ref_t start_idx)
{
    size_t          length;
    ref_t          *trace;
    trace = trc_find_trace (dst_idx, level, parent_ofs, start_idx, &length);
    trc_write_trace (env, trc_output, trace, length);
    RTfree (trace);
}

void
trc_write_trace (trc_env_t *env, char *trc_output, ref_t *trace, int level)
{
    lts_type_t ltstype = GBgetLTStype(env->model);
    hre_context_t n = HREctxCreate(0, 1, "blah", 0);
    lts_file_t template = lts_index_template();
    lts_file_set_context(template, n);
    env->trace_handle=lts_file_create(trc_output,ltstype,1,template);
    for(int i=0;i<lts_type_get_type_count(ltstype);i++)
        lts_file_set_table(env->trace_handle,i,GBgetChunkMap(env->model,i));
    uint32_t tmp[1]={trace[0]};
    lts_write_init(env->trace_handle,0,tmp);
    write_trace (env, level, trace);
    lts_file_close (env->trace_handle);
}
