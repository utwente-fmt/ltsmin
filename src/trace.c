/*
 * trace.c
 *
 *  Created on: Jun 14, 2010
 *      Author: laarman
 */

#include "config.h"
#include <trace.h>
#include <runtime.h>
#include <lts_enum.h>
#include <lts_io.h>
#include <dir_ops.h>
#include <treedbs.h>
#include <stringindex.h>
#include <scctimer.h>
#include <fifo.h>
#include <struct_io.h>

#define  BUFLEN 4096

struct trc_s {
    int len;
    lts_type_t ltstype;
    string_index_t* values;
    treedbs_t state_db;
    treedbs_t map_db; // NULL als aantal defined state labels == 0
    treedbs_t edge_db; // NULL als aantal edges labels == 0
    int *state_lbl; // NULL als geen definined state labels
    int *edge_lbl; // NULL als geen edge labels
    int *trace_idx_map; // maps n-th step of the trace to state_db idx
};

static lts_enum_cb_t trace_handle=NULL;

typedef struct write_trace_step_s {
    int src_no;
    int dst_no;
    int* dst;
    int found;
    int                 N;
} write_trace_step_t;

struct trc_env_s {
    int                 N;
    model_t             model;
    trc_get_state_f         get_state;
    int                 state_labels;
    int                 start_idx;
};

trc_env_t *
trc_create(model_t model, trc_get_state_f get, int start_idx)
{
    trc_env_t        *trace = RTmalloc(sizeof(trc_env_t));
    lts_type_t          ltstype = GBgetLTStype (model);
    trace->N = lts_type_get_state_length (ltstype);
    trace->state_labels = lts_type_get_state_label_count (ltstype);
    trace->get_state = get;
    trace->model = model;
    trace->start_idx = start_idx;
    return trace;
}

int trc_get_length(trc_t trace) {
    return trace->len;
}

lts_type_t trc_get_ltstype(trc_t trace) {
    return trace->ltstype;
}

int trc_get_edge_label(trc_t trace, int i, int *dst) {
    if (trace->edge_lbl == NULL)
        return 0;
    if (dst==NULL)
        return 1;
    TreeUnfold(trace->edge_db, trace->edge_lbl[i], dst);
    return 1;
}

int trc_get_state_label(trc_t trace, int i, int *dst) {
    if (trace->map_db == NULL)
        return 0;
    if (dst==NULL)
        return 1;
    TreeUnfold(trace->map_db, trace->state_lbl[i], dst);
    return 1;
}

int  trc_get_state_idx(trc_t trace, int i) {
    return trace->trace_idx_map[i];
}

void trc_get_state(trc_t trace, int i, int *dst) {
    TreeUnfold(trace->state_db, trace->trace_idx_map[i], dst);
}

int
trc_get_type(trc_t trace, int type, int label, size_t dst_size, char* dst)
{
    // in case this is not a type
    if (type == -1)
        return -1;

    // otherwise, read the value
    char tmp[BUFLEN];
    chunk c;
    c.data = SIgetC(trace->values[type], label, (int*)&c.len);
    if (c.data) {
        chunk2string(c, BUFLEN, tmp);
        strncpy(dst, tmp, dst_size);
        return 0;
    } else {
        return -1;
    }
}

static void 
write_trace_state(trc_env_t *env, int src_no, int *state)
{
    Warning(debug,"dumping state %d",src_no);
    int labels[env->state_labels];
    if (env->state_labels) GBgetStateLabelsAll(env->model,state,labels);
    enum_state(trace_handle,0,state,labels);
}


static void 
write_trace_next(void*arg,transition_info_t *ti,int*dst)
{
    write_trace_step_t     *ctx=(write_trace_step_t*)arg;
    if(ctx->found) return;
    for(int i=0;i<ctx->N;i++)
        if (ctx->dst[i]!=dst[i]) return;
    ctx->found=1;
    enum_seg_seg(trace_handle,0,ctx->src_no,0,ctx->dst_no,ti->labels);
}

static void
write_trace_step(trc_env_t *env, int src_no,int*src,int dst_no,int*dst)
{
    Warning(debug,"finding edge for state %d",src_no);
    struct write_trace_step_s ctx;
    ctx.src_no=src_no;
    ctx.dst_no=dst_no;
    ctx.dst=dst;
    ctx.found=0;
    ctx.N = env->N;
    GBgetTransitionsAll(env->model,src,write_trace_next,&ctx);
    if (ctx.found==0) Fatal(1,error,"no matching transition found");
}

static void
write_trace(trc_env_t *env, size_t trace_size, uint32_t *trace)
{
    // write initial state
    size_t i = 0;
    int step = 0;
    int src[env->N];
    int store[env->N * 2]; //reserve extra for tree TODO: breaks interfaces
    int *dst = env->get_state(env->start_idx, store);
    write_trace_state(env, env->start_idx, dst);
    i++;
    while(i < trace_size) {
        for(int j=0; j < env->N; ++j)
            src[j] = dst[j];
        dst = env->get_state(trace[i], store);
        write_trace_step(env, step, src, step + 1, dst);  // write step
        write_trace_state(env, trace[i], dst);            // write dst_idx
        i++;
        step++;
    }
}

static void
find_trace_to(trc_env_t *env, int dst_idx, int level, int *parent_ofs)
{
    uint32_t *trace = (uint32_t*)RTmalloc(sizeof(uint32_t) * level);
    if (trace == NULL)
        Fatal(1, error, "unable to allocate memory for trace");
    int i = level - 1;
    int curr_idx = dst_idx;
    trace[i] = curr_idx;
    while(curr_idx != env->start_idx) {
        i--;
        curr_idx = parent_ofs[curr_idx];
        trace[i] = curr_idx;
    }
    // write trace
    write_trace(env, level - i, &trace[i]);
    RTfree(trace);
    return;
}

void
trc_find_and_write (trc_env_t *env, char *trc_output, int dst_idx, 
                      int level, int *parent_ofs)
{
    lts_output_t trace_output=lts_output_open(trc_output,
                                              env->model,1,0,1,"vsi",NULL);
    {
       int store[env->N * 2]; //reserve extra for tree TODO: breaks interfaces
       int *init_state = env->get_state(env->start_idx, store);
       lts_output_set_root_vec(trace_output,(uint32_t*)init_state);
       lts_output_set_root_idx(trace_output,0,0);
    }
    trace_handle=lts_output_begin(trace_output,0,1,0);
    mytimer_t timer = SCCcreateTimer();
    SCCstartTimer(timer);
    find_trace_to(env, dst_idx, level, parent_ofs);
    SCCstopTimer(timer);
    SCCreportTimer(timer,"constructing the trace took");
    lts_output_end(trace_output,trace_handle);
    lts_output_close(&trace_output);
}

trc_t trc_read(const char *name){
    trc_t trace=RT_NEW(struct trc_s);
    archive_t arch;
    char *decode;
    if (is_a_dir(name)){
        Warning(info,"open dir %s",name);
        arch=arch_dir_open((char*)name,IO_BLOCKSIZE);
        decode=NULL;
    } else {
        Warning(info,"open gcf %s",name);
        arch=arch_gcf_read(raf_unistd((char*)name));
        decode="auto";
    }
    stream_t ds=arch_read(arch,"info",decode);
    char description[1024];
    DSreadS(ds,description,1024);
    if(strncmp(description,"vsi",3)){
        Fatal(1,error,"input has to be vsi");
    }
    int N;
    fifo_t fifo=FIFOcreate(4096);
    if (strcmp(description+4,"1.0")){ // check version
        Fatal(1,error,"unknown version %s",description+4);
    }
    char *comment=DSreadSA(ds);
    Warning(info,"comment is %s",comment);
    N=DSreadU32(ds);
    if (N!=1) Fatal(1,error,"more than one segment");
    N=DSreadVL(ds);
    {
        stream_t fs=FIFOstream(fifo);
        char data[N];
        DSread(ds,data,N);
        DSwrite(fs,data,N);
        if(DSreadU32(fs)) Fatal(1,error,"root seg not 0");
        if(DSreadU32(fs)) Fatal(1,error,"root ofs not 0");
        N=DSreadU32(fs);
        if (N) {
            for(int i=0;i<N;i++){
                DSreadU32(fs);
            }
        }
        if (FIFOsize(fifo)) Fatal(1,error,"Too much data in initial state (%d bytes)",FIFOsize(fifo));
    }
    N=DSreadVL(ds);
    {
        stream_t fs=FIFOstream(fifo);
        char data[N];
        DSread(ds,data,N);
        DSwrite(fs,data,N);
        trace->ltstype=lts_type_deserialize(fs);
        if (FIFOsize(fifo)) Fatal(1,error,"Too much data in lts type (%d bytes)",FIFOsize(fifo));
    }
    Warning(info,"got the ltstype, skipping the rest");
    DSclose(&ds);

    int type_count=lts_type_get_type_count(trace->ltstype);
    trace->values=RTmallocZero(type_count*sizeof(string_index_t));
    for(int i=0;i<type_count;i++){
        Warning(info,"loading type %s",lts_type_get_type(trace->ltstype,i));
        char stream_name[1024];
        sprintf(stream_name,"CT-%d",i);
        ds=arch_read(arch,stream_name,decode);
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
    struct_stream_t vec=arch_read_vec_U32(arch,"SV-0-%d",N,decode);
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
    uint32_t edge_count = trace->len - 1;
    N=lts_type_get_state_label_count(trace->ltstype);
    if (N) {
        Warning(info,"reading defined state labels");
        trace->map_db=TreeDBScreate(N);
        trace->state_lbl=RTmallocZero(edge_count*sizeof(int));
        struct_stream_t map=arch_read_vec_U32(arch, "SL-0-%d",N,decode);
        for(uint32_t j=0;j<edge_count;++j) {
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
    N=lts_type_get_edge_label_count(trace->ltstype);
    if (N) {
        Warning(info,"reading edge labels");
        int lbl_vec[N];
        trace->edge_db=TreeDBScreate(N);
        trace->edge_lbl=RTmallocZero(edge_count*sizeof(int));
        struct_stream_t lbl=arch_read_vec_U32(arch,"EL-0-%d",N,decode);
        for (uint32_t j=0;j<edge_count;j++){
            if (DSstructEmpty(lbl)) {
                Fatal(1,error,"not enough edge labels found");
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
        struct_stream_t src=arch_read_vec_U32_named(arch,"ES-0-%s",2,segofs,decode);
        struct_stream_t dst=arch_read_vec_U32_named(arch,"ED-0-%s",1,ofs,decode);
        for (uint32_t j=0;j<edge_count;j++){
            if (DSstructEmpty(src) || DSstructEmpty(dst)) {
                Fatal(1,error,"not enough tranitions found");
            }
            DSreadStruct(src,src_vec);
            DSreadStruct(dst,dst_ofs);
            // check src/dst
            if (src_vec[0] != 0){
                Fatal(1,error,"transition source segment != 0");
            }
            if (src_vec[1] != dst_ofs[0] - 1 && src_vec[1] != (int)j) {
                Fatal(1,error,"transition src/dst is not straight sequence");
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
