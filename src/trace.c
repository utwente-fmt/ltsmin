/*
 * trace.c
 *
 *  Created on: Jun 14, 2010
 *      Author: laarman
 */

#include <config.h>


#include <dir_ops.h>
#include <hre/user.h>
#include <lts-io/user.h>
#include <stringindex.h>
#include <trace.h>
#include <treedbs.h>
//#include <struct_io.h>

struct trc_s {
    int                 len;
    lts_type_t          ltstype;
    string_index_t*     values;
    treedbs_t           state_db;
    treedbs_t           map_db; // NULL als aantal defined state labels == 0
    treedbs_t           edge_db; // NULL als aantal edges labels == 0
    int                *state_lbl; // NULL als geen definined state labels
    int                *edge_lbl; // NULL als geen edge labels
    int                *trace_idx_map; // maps n-th step of the trace to state_db idx
};

static lts_file_t       trace_handle=NULL;

typedef struct write_trace_step_s {
    int                 src_no;
    int                 dst_no;
    int                *dst;
    int                 found;
    int                 N;
} write_trace_step_t;

struct trc_env_s {
    int                 N;
    model_t             model;
    trc_get_state_f     get_state;
    void               *get_state_arg;
    int                 state_labels;
    ref_t               start_idx;
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

int
trc_get_length (trc_t trace)
{
    return trace->len;
}

lts_type_t trc_get_ltstype (trc_t trace) {
    return trace->ltstype;
}

int
trc_get_edge_label (trc_t trace, int i, int *dst)
{
    if (trace->edge_lbl == NULL)
        return 0;
    if (dst==NULL)
        return 1;
    TreeUnfold (trace->edge_db, trace->edge_lbl[i], dst);
    return 1;
}

int
trc_get_state_label (trc_t trace, int i, int *dst)
{
    if (trace->map_db == NULL)
        return 0;
    if (dst==NULL)
        return 1;
    TreeUnfold(trace->map_db, trace->state_lbl[i], dst);
    return 1;
}

int
trc_get_state_idx (trc_t trace, int i)
{
    return trace->trace_idx_map[i];
}

void
trc_get_state (trc_t trace, int i, int *dst)
{
    TreeUnfold (trace->state_db, trace->trace_idx_map[i], dst);
}

int
trc_get_type (trc_t trace, int type, int label, size_t dst_size, char* dst)
{
    // in case this is not a type
    if (type == -1)
        return -1;

    // otherwise, read the value
    chunk c;
    c.data = SIgetC (trace->values[type], label, (int*)&c.len);
    if (c.data) {
        chunk2string (c, dst_size, dst);
        return 0;
    } else {
        return -1;
    }
}

static void 
write_trace_state(trc_env_t *env, int src_no, int *state)
{
    Warning (debug,"dumping state %d", src_no);
    int                 labels[env->state_labels];
    if (env->state_labels) GBgetStateLabelsAll(env->model, state, labels);
    lts_write_state (trace_handle, 0, state, labels);
}


static void 
write_trace_next (void *arg, transition_info_t *ti, int *dst)
{
    write_trace_step_t *ctx = (write_trace_step_t*)arg;
    if (ctx->found) return;
    for (int i = 0; i < ctx->N; i++)
        if (ctx->dst[i] != dst[i]) return;
    ctx->found = 1;
    lts_write_edge (trace_handle, 0, &ctx->src_no, 0, &ctx->dst_no, ti->labels);
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
    ctx.N = env->N;
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
    trace_handle=lts_file_create(trc_output,ltstype,1,NULL);
    int *init_state = env->get_state(env->start_idx, env->get_state_arg);
    lts_write_init(trace_handle,0,init_state);
    rt_timer_t timer = RTcreateTimer ();
    RTstartTimer (timer);
    find_trace_to (env, dst_idx, level, parent_ofs);
    RTstopTimer (timer);
    RTprintTimer (info, timer, "constructing the trace took");
    lts_file_close (trace_handle);
}

void
trc_write_trace (trc_env_t *env, char *trc_output, ref_t *trace, int level)
{
    lts_type_t ltstype = GBgetLTStype(env->model);
    trace_handle=lts_file_create(trc_output,ltstype,1,NULL);
    int *init_state = env->get_state(env->start_idx, env->get_state_arg);
    lts_write_init(trace_handle,0,init_state);
    rt_timer_t timer = RTcreateTimer ();
    RTstartTimer (timer);
    write_trace (env, level, trace);
    RTstopTimer (timer);
    RTprintTimer (info, timer, "constructing the trace took");
    lts_file_close (trace_handle);
}

trc_t trc_read (const char *name){
    trc_t trace=RT_NEW(struct trc_s);
    archive_t arch;
    stream_t ds=arch_read(arch,"info");
    char description[1024];
    DSreadS(ds,description,1024);
    if(strncmp(description,"vsi",3)){
        Abort("input has to be vsi");
    }
    int N;
    stream_t fs=FIFOcreate(4096);
    if (strcmp(description+4,"1.0")){ // check version
        Abort("unknown version %s",description+4);
    }
    char *comment=DSreadSA(ds);
    Warning(info,"comment is %s",comment);
    N=DSreadU32(ds);
    if (N!=1) Abort("more than one segment");
    N=DSreadVL(ds);
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fs,data,N);
        if(DSreadU32(fs)) Abort("root seg not 0");
        if(DSreadU32(fs)) Abort("root ofs not 0");
        N=DSreadU32(fs);
        if (N) {
            for(int i=0;i<N;i++){
                DSreadU32(fs);
            }
        }
        if (FIFOsize(fs)) Abort("Too much data in initial state (%d bytes)",FIFOsize(fs));
    }
    N=DSreadVL(ds);
    {
        char data[N];
        DSread(ds,data,N);
        DSwrite(fs,data,N);
        trace->ltstype=lts_type_deserialize(fs);
        if (FIFOsize(fs)) Abort("Too much data in lts type (%d bytes)",FIFOsize(fs));
    }
    Warning(info,"got the ltstype, skipping the rest");
    DSclose(&ds);

    int type_count=lts_type_get_type_count(trace->ltstype);
    trace->values=RTmallocZero(type_count*sizeof(string_index_t));
    for(int i=0;i<type_count;i++){
        Warning(info,"loading type %s",lts_type_get_type(trace->ltstype,i));
        char stream_name[1024];
        sprintf(stream_name,"CT-%d",i);
        ds=arch_read(arch,stream_name);
        trace->values[i]=SIcreate();
        int L;
        for(L=0;;L++){
            if (DSempty(ds)) break;
            int len=DSreadVL(ds);
            char data[len];
            DSread(ds,data,len);
            SIputCAt(trace->values[i],data,len,L);
        }
        Warning(info,"%d elements",L);
        DSclose(&ds);
    }
    Warning(info,"reading states");
    N=lts_type_get_state_length(trace->ltstype);
    trace->state_db=TreeDBScreate(N);
    int trc_size = 1<<7;
    trace->trace_idx_map=RTmalloc(trc_size * sizeof(int));
    struct_stream_t vec=arch_read_vec_U32(arch,"SV-0-%d",N);
    while(!DSstructEmpty(vec)){
        uint32_t state[N];
        DSreadStruct(vec,state);
        if (trace->len >= trc_size) {
            trc_size = trc_size << 1;
            trace->trace_idx_map = RTrealloc(trace->trace_idx_map, trc_size * sizeof(int) );
        }
        trace->trace_idx_map[trace->len] = TreeFold(trace->state_db,(int*)state);
        trace->len++;
    }
    // realloc to proper length
    trace->trace_idx_map = RTrealloc(trace->trace_idx_map, trace->len * sizeof(int) );
    DSstructClose(&vec);
    Warning(info,"length of trace is %d",trace->len);
    // should be one less then the length of the trace
    uint32_t sl_count = trace->len;
    N=lts_type_get_state_label_count(trace->ltstype);
    if (N) {
        Warning(info,"reading defined state labels");
        trace->map_db=TreeDBScreate(N);
        trace->state_lbl=RTmallocZero(sl_count*sizeof(int));
        struct_stream_t map=arch_read_vec_U32(arch, "SL-0-%d",N);
        for(uint32_t j=0;j<sl_count;++j) {
            int map_vec[N];
            DSreadStruct(map, map_vec);
            trace->state_lbl[j] = TreeFold(trace->map_db, map_vec);
        }
        if (!DSstructEmpty(map)) {
            Warning(info,"too much state label information found");
        }
        DSstructClose(&map);
    } else {
        trace->map_db = NULL;
        trace->state_lbl = NULL;
    }
    uint32_t edge_count = trace->len - 1;
    N=lts_type_get_edge_label_count(trace->ltstype);
    if (N) {
        Warning(info,"reading edge labels");
        int lbl_vec[N];
        trace->edge_db=TreeDBScreate(N);
        trace->edge_lbl=RTmallocZero(edge_count*sizeof(int));
        struct_stream_t lbl=arch_read_vec_U32(arch,"EL-0-%d",N);
        for (uint32_t j=0;j<edge_count;j++){
            if (DSstructEmpty(lbl)) {
                Abort("not enough edge labels found");
            }
            DSreadStruct(lbl,lbl_vec);
            trace->edge_lbl[j] = TreeFold(trace->edge_db, lbl_vec);
        }
        if (!DSstructEmpty(lbl)) {
            Warning(info,"too much edge label information found");
        }
        DSstructClose(&lbl);

    } else {
            trace->edge_db = NULL;
            trace->edge_lbl = NULL;
    }
    Warning(info,"checking transitions");
    {
        char* ofs[1]={"ofs"};
        char* segofs[2]={"seg","ofs"};
        int src_vec[2];
        int dst_ofs[1];
        struct_stream_t src=arch_read_vec_U32_named(arch,"ES-0-%s",2,segofs);
        struct_stream_t dst=arch_read_vec_U32_named(arch,"ED-0-%s",1,ofs);
        for (uint32_t j=0;j<edge_count;j++){
            if (DSstructEmpty(src) || DSstructEmpty(dst)) {
                Abort("not enough tranitions found");
            }
            DSreadStruct(src,src_vec);
            DSreadStruct(dst,dst_ofs);
            // check src/dst
            if (src_vec[0] != 0){
                Abort("transition source segment != 0");
            }
            if (src_vec[1] != dst_ofs[0] - 1 && src_vec[1] != (int)j) {
                Abort("transition src/dst is not straight sequence");
            }
        }
        if (!(DSstructEmpty(src) || DSstructEmpty(dst))) {
            Warning(info,"too many transitions found");
        } else {
            Warning(info,"read %d transitions", edge_count);
        }
        // Note that:
        // 1. We should test what was requested.
        // 2. We should return the requested info in one pass. (ltsmin-convert will break)
        DSstructClose(&src);
        DSstructClose(&dst);
    }

    Warning(info,"closing %s",name);
    arch_close(&arch);
    return trace;
}
