/*
 * trace.c
 *
 *  Created on: Jun 14, 2010
 *      Author: laarman
 */

#include <hre/config.h>

#include <hre/user.h>
#include <lts-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <mc-lib/trace.h>
#include <hre/stringindex.h>
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
    ref_t               start_idx;
    lts_file_t          trace_handle;
};

trc_env_t *
trc_create (model_t model, trc_get_state_f get, ref_t start_idx, void *arg)
{
    trc_env_t          *trace = RTmalloc(sizeof(trc_env_t));
    lts_type_t          ltstype = GBgetLTStype (model);
    trace->N = lts_type_get_state_length (ltstype);
    trace->state_labels = lts_type_get_state_label_count (ltstype);
    trace->get_state = get;
    trace->get_state_arg = arg;
    trace->model = model;
    trace->start_idx = start_idx;
    return trace;
}

static void 
write_trace_state(trc_env_t *env, int src_no, int *state)
{
    Warning (debug,"dumping state %d", src_no);
    int                 labels[env->state_labels];
    if (env->state_labels) GBgetStateLabelsAll(env->model, state, labels);
    lts_write_state (env->trace_handle, 0, state, labels);
}

static void 
write_trace_next (void *arg, transition_info_t *ti, int *dst)
{
    write_trace_step_t *ctx = (write_trace_step_t*)arg;
    if (ctx->found) return;
    for (int i = 0; i < ctx->env->N; i++)
        if (ctx->dst[i] != dst[i]) return;
    ctx->found = 1;
    lts_write_edge (ctx->env->trace_handle, 0, &ctx->src_no, 0, &ctx->dst_no, ti->labels);
}

static void
write_trace_step (trc_env_t *env, int src_no, int *src, int dst_no, int *dst)
{
    Warning (debug,"finding edge for state %d", src_no);
    struct write_trace_step_s ctx;
    ctx.src_no = src_no;
    ctx.dst_no = dst_no;
    ctx.dst = dst;
    ctx.found = 0;
    ctx.env = env;
    GBgetTransitionsAll (env->model, src, write_trace_next, &ctx);
    if (ctx.found==0) Abort("no matching transition found");
}

static void
write_trace (trc_env_t *env, size_t trace_size, ref_t *trace)
{
    // write initial state
    size_t              i = 0;
    int                 step = 0;
    int                 src[env->N];
    int                *dst = env->get_state (env->start_idx, env->get_state_arg);
    write_trace_state (env, env->start_idx, dst);
    i++;
    while (i < trace_size) {
        for (int j=0; j < env->N; ++j)
            src[j] = dst[j];
        dst = env->get_state (trace[i], env->get_state_arg);
        write_trace_step (env, step, src, step + 1, dst);  // write step
        write_trace_state (env, trace[i], dst);            // write dst_idx
        i++;
        step++;
    }
}

static void
find_trace_to (trc_env_t *env, int dst_idx, int level, ref_t *parent_ofs)
{
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
    while(curr_idx != env->start_idx) {
        i--;
        if (i < 0)
            Abort("Trace length 10x longer than initially found trace. Giving up.");
        curr_idx = parent_ofs[curr_idx];
        trace[i] = curr_idx;
    }
    Warning (info, "reconstructed trace length: %zu", max_length - i);
    // write trace
    write_trace (env, max_length - i, &trace[i]);
    RTfree (trace);
}

void
trc_find_and_write (trc_env_t *env, char *trc_output, ref_t dst_idx, 
                      int level, ref_t *parent_ofs)
{
    lts_type_t ltstype = GBgetLTStype(env->model);
    hre_context_t n = HREctxCreate(0, 1, "blah", 0);
    lts_file_t template = lts_index_template();
    lts_file_set_context(template, n);
    env->trace_handle=lts_file_create(trc_output,ltstype,1,template);
    lts_file_set_context(env->trace_handle, n);
    for(int i=0;i<lts_type_get_type_count(ltstype);i++)
        lts_file_set_table(env->trace_handle,i,GBgetChunkMap(env->model,i));
    int *init_state = env->get_state(env->start_idx, env->get_state_arg);
    lts_write_init(env->trace_handle,0,init_state);
    rt_timer_t timer = RTcreateTimer ();
    RTstartTimer (timer);
    find_trace_to (env, dst_idx, level, parent_ofs);
    RTstopTimer (timer);
    RTprintTimer (info, timer, "constructing the trace took");
    lts_file_close (env->trace_handle);
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
    int *init_state = env->get_state(env->start_idx, env->get_state_arg);
    lts_write_init(env->trace_handle,0,init_state);
    rt_timer_t timer = RTcreateTimer ();
    RTstartTimer (timer);
    write_trace (env, level, trace);
    RTstopTimer (timer);
    RTprintTimer (info, timer, "constructing the trace took");
    lts_file_close (env->trace_handle);
}
