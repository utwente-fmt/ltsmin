// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>

#include <hre/user.h>
#include <lts-io/provider.h>

struct lts_file_s {
    int fixed;
    char *name;
    hre_context_t ctx;
    size_t user_size;
    lts_type_t ltstype;
    int segments;
    edge_owner_t edge_owner;
    state_format_t init_mode;
    state_format_t src_mode;
    state_format_t dst_mode;
    value_table_t *values;
    lts_push_m push;
    lts_pull_m pull;
    lts_read_init_m read_init;
    lts_write_init_m write_init;
    lts_read_state_m read_state;
    lts_write_state_m write_state;
    lts_read_edge_m read_edge;
    lts_write_edge_m write_edge;
    lts_set_table_m set_table;
    lts_attach_m attach;
    lts_close_m close;
    uint32_t roots; // number of calls to write init.
    uint32_t *states; // number of calls to write state.
    uint64_t *edges;  // number of calls to write edge.
    uint32_t *max_src_p1; // maximum number seen as source state plus 1.
    uint32_t *max_dst_p1; // maximum number seen as target state plus 1.
    uint32_t *expected_values; // the expected number of values.
};

// size of struct rounded up to multiple of 8.
static const size_t system_size=((sizeof(struct lts_file_s)+7)/8)*8;

#define USER(ptr) ((lts_file_t)(((char*)(ptr))+system_size))
#define SYSTEM(ptr) ((lts_file_t)(((char*)(ptr))-system_size))

lts_file_t lts_index_template(){
    lts_file_t lts=(lts_file_t)HREmallocZero(hre_heap,system_size);
    lts->edge_owner=DestOwned;
    lts->init_mode=Index;
    lts->src_mode=Index;
    lts->dst_mode=Index;
    return USER(lts);
}

lts_file_t lts_vset_template(){
    lts_file_t lts=(lts_file_t)HREmallocZero(hre_heap,system_size);
    lts->edge_owner=SourceOwned;
    lts->init_mode=SegVector;
    lts->src_mode=Index;
    lts->dst_mode=SegVector;
    return USER(lts);
}

lts_file_t lts_get_template(lts_file_t file){
    file=SYSTEM(file);
    lts_file_t lts=(lts_file_t)HREmallocZero(hre_heap,system_size);
    lts->init_mode=file->init_mode;
    lts->edge_owner=file->edge_owner;
    lts->src_mode=file->src_mode;
    lts->dst_mode=file->dst_mode;
    return USER(lts);
}

static void no_write_init(lts_file_t lts,int seg,void* state){
    (void)seg;
    (void)state;
    Abort("the file format of %s does not support the write init operation",lts->name);
}

static void no_write_state(lts_file_t file,int seg,void* state,void*labels){
    (void)seg;
    (void)state;
    (void)labels;
    Abort("the file format of %s does not support the write state operation",file->name);
}

static void no_write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels){
    (void)src_seg;
    (void)src_state;
    (void)dst_seg;
    (void)dst_state;
    (void)labels;
    Abort("the file format of %s does not support the write edge operation",file->name);
}

lts_file_t lts_file_bare(const char* name,lts_type_t ltstype,int segments,lts_file_t settings,size_t user_size){
    lts_file_t lts=(lts_file_t)HREmallocZero(hre_heap,system_size+user_size);
    lts->states=(uint32_t*)HREmallocZero(hre_heap,segments*sizeof(uint32_t));
    lts->edges=(uint64_t*)HREmallocZero(hre_heap,segments*sizeof(uint64_t));
    lts->max_src_p1=(uint32_t*)HREmallocZero(hre_heap,segments*sizeof(uint32_t));
    lts->max_dst_p1=(uint32_t*)HREmallocZero(hre_heap,segments*sizeof(uint32_t));
    lts->expected_values=(uint32_t*)HREmallocZero(hre_heap,segments*sizeof(uint32_t));
    lts->ctx=lts_file_context(settings) ? lts_file_context(settings) : HREglobal();
    lts->name=strdup(name);
    lts->user_size=user_size;
    lts->ltstype=ltstype;
    int N=lts_type_get_type_count(ltstype);
    lts->values=(value_table_t*)HREmallocZero(hre_heap,N*sizeof(value_table_t));
    lts->segments=segments;
    if (settings){
        settings=SYSTEM(settings);
        lts->edge_owner=settings->edge_owner;
        lts->init_mode=settings->init_mode;
        lts->src_mode=settings->src_mode;
        lts->dst_mode=settings->dst_mode;
    }
    lts->write_init=no_write_init;
    lts->write_state=no_write_state;
    lts->write_edge=no_write_edge;
    return USER(lts);
}

void lts_file_set_table_callback(lts_file_t file,lts_set_table_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->set_table=method;
}

state_format_t lts_file_init_mode(lts_file_t file){
    return SYSTEM(file)->init_mode;
}

void lts_file_set_init_mode(lts_file_t file,state_format_t mode){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->init_mode=mode;
}

state_format_t lts_file_source_mode(lts_file_t file){
    return SYSTEM(file)->src_mode;
}

void lts_file_set_source_mode(lts_file_t file,state_format_t mode){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->src_mode=mode;
}

state_format_t lts_file_dest_mode(lts_file_t file){
    return SYSTEM(file)->dst_mode;
}

void lts_file_set_dest_mode(lts_file_t file,state_format_t mode){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->dst_mode=mode;
}

edge_owner_t lts_file_get_edge_owner(lts_file_t file){
    return SYSTEM(file)->edge_owner;
}

void lts_file_set_edge_owner(lts_file_t file,edge_owner_t owner){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->edge_owner=owner;
}

int lts_file_get_segments(lts_file_t lts){
    return SYSTEM(lts)->segments;
}

void lts_file_set_segments(lts_file_t lts,int segments){
    assert(!SYSTEM(lts)->fixed);
    SYSTEM(lts)->segments=segments;
}

lts_type_t lts_file_get_type(lts_file_t lts){
    return SYSTEM(lts)->ltstype;
}

value_table_t lts_file_get_table(lts_file_t lts,int type_no){
    return SYSTEM(lts)->values[type_no];
}

void lts_file_set_table(lts_file_t lts,int type_no,value_table_t table){
    if (SYSTEM(lts)->set_table) {
        SYSTEM(lts)->values[type_no]=SYSTEM(lts)->set_table(lts,type_no,table);
    } else {
        SYSTEM(lts)->values[type_no]=table;
    }
}

void lts_file_set_push(lts_file_t file,lts_push_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->push=method;
}

void lts_file_set_pull(lts_file_t file,lts_pull_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->pull=method;
}

int lts_write_supported(lts_file_t file){
    return SYSTEM(file)->write_init!=NULL && SYSTEM(file)->write_init!=no_write_init;
}

int lts_push_supported(lts_file_t file){
    return SYSTEM(file)->push!=NULL;
}

int lts_read_supported(lts_file_t file){
    return SYSTEM(file)->read_init!=NULL;
}

int lts_pull_supported(lts_file_t file){
    return SYSTEM(file)->pull!=NULL;
}

void lts_file_set_read_init(lts_file_t file,lts_read_init_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->read_init=method;
}

void lts_file_set_write_init(lts_file_t file,lts_write_init_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->write_init=method;
}

void lts_file_set_read_state(lts_file_t file,lts_read_state_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->read_state=method;
}

void lts_file_set_write_state(lts_file_t file,lts_write_state_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->write_state=method;
}

void lts_file_set_read_edge(lts_file_t file,lts_read_edge_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->read_edge=method;
}

void lts_file_set_write_edge(lts_file_t file,lts_write_edge_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->write_edge=method;
}

void lts_file_set_close(lts_file_t file,lts_close_m method){
    assert(!SYSTEM(file)->fixed);
    SYSTEM(file)->close=method;
}

const char* lts_file_get_name(lts_file_t file){
    return SYSTEM(file)->name;
}

void lts_file_close(lts_file_t lts){
    assert(SYSTEM(lts)->fixed);
    SYSTEM(lts)->close(lts);
}

int lts_read_init(lts_file_t lts,int *seg,void* state){
    assert(SYSTEM(lts)->fixed);
    return SYSTEM(lts)->read_init(lts,seg,state);
}

int lts_read_state(lts_file_t lts,int *seg,void* state,void* labels){
    assert(SYSTEM(lts)->fixed);
    return SYSTEM(lts)->read_state(lts,seg,state,labels);
}

int lts_read_edge(lts_file_t lts,int *src_seg,void* src_state,int *dst_seg,void*dst_state,void* labels){
    assert(SYSTEM(lts)->fixed);
    return SYSTEM(lts)->read_edge(lts,src_seg,src_state,dst_seg,dst_state,labels);
}

void lts_write_init(lts_file_t lts,int seg,void* state){
    assert(SYSTEM(lts)->fixed);
    SYSTEM(lts)->roots++;
    SYSTEM(lts)->write_init(lts,seg,state);
}

void lts_write_state(lts_file_t lts,int seg,void* state,void* labels){
    assert(SYSTEM(lts)->fixed);
    SYSTEM(lts)->states[seg]++;
    SYSTEM(lts)->write_state(lts,seg,state,labels);
}

void lts_write_edge(lts_file_t lts,int src_seg,void* src_state,int dst_seg,void*dst_state,void* labels){
    assert(SYSTEM(lts)->fixed);
    switch(SYSTEM(lts)->edge_owner){
    case SourceOwned:
        SYSTEM(lts)->edges[src_seg]++;
        break;
    case DestOwned:
        SYSTEM(lts)->edges[dst_seg]++;
        break;
    default:
        Abort("missing case in write edge %d",SYSTEM(lts)->edge_owner);
    }
    if (SYSTEM(lts)->src_mode==Index
    && *((uint32_t*)src_state)>=SYSTEM(lts)->max_src_p1[src_seg]){
        SYSTEM(lts)->max_src_p1[src_seg]=*((uint32_t*)src_state)+1;
    }
    if (SYSTEM(lts)->dst_mode==Index
    && *((uint32_t*)dst_state)>=SYSTEM(lts)->max_dst_p1[dst_seg]){
        SYSTEM(lts)->max_dst_p1[dst_seg]=*((uint32_t*)dst_state)+1;
    }
    if (!SYSTEM(lts)->write_edge) Abort("write edge not set");
    SYSTEM(lts)->write_edge(lts,src_seg,src_state,dst_seg,dst_state,labels);
}

void lts_file_push(lts_file_t src,lts_file_t dst){
    assert(SYSTEM(dst)->fixed);
    assert(SYSTEM(src)->fixed);
    if (SYSTEM(src)->segments != SYSTEM(dst)->segments){
        Abort("segment count mismatch %d != %d",SYSTEM(src)->segments,SYSTEM(dst)->segments);
    }
    SYSTEM(src)->push(src,dst);
}

void lts_file_pull(lts_file_t dst,lts_file_t src){
    assert(SYSTEM(dst)->fixed);
    assert(SYSTEM(src)->fixed);
    if (SYSTEM(src)->segments != SYSTEM(dst)->segments){
        Abort("segment count mismatch %d != %d",SYSTEM(src)->segments,SYSTEM(dst)->segments);
    }
    SYSTEM(dst)->pull(dst,src);
}

static void default_push(lts_file_t src,lts_file_t dst){
    if (SYSTEM(src)->ctx != SYSTEM(dst)->ctx) Abort("cannot copy between different contexts");
    int me=HREme(SYSTEM(src)->ctx);
    lts_type_t ltstype=lts_file_get_type(src);
    int N1=lts_type_get_state_length(ltstype);
    int N2=lts_type_get_state_label_count(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    int do_state;
    if (N1){
        Print(infoLong,"vector length is %d",N1);
        do_state=1;
    } else {
        do_state=N2?1:0;
        N1=1;
    }
    int src_seg;
    uint32_t src_state[N1];
    int dst_seg;
    uint32_t dst_state[N1];
    uint32_t edge_labels[K];
    if (me==0) {
        Print(infoLong,"copying initial states");
        int count=0;
        while(lts_read_init(src,&src_seg,src_state)){
            count++;
            lts_write_init(dst,src_seg,src_state);
        }
        Print(infoLong,"%d initial state(s)",count);
    }
    for(int i=0;i<lts_file_owned_count(src);i++){
        src_seg=lts_file_owned(src,i);
        dst_seg=lts_file_owned(src,i);
        if (do_state) {
            Print(infoLong,"copying states of segment %d",src_seg);
            uint32_t state_labels[N2];
            while(lts_read_state(src,&src_seg,src_state,state_labels)){
                lts_write_state(dst,src_seg,src_state,state_labels);
            }
        }
        Print(infoLong,"copying edges of segment %d",src_seg);
        while(lts_read_edge(src,&src_seg,src_state,&dst_seg,dst_state,edge_labels)){
            lts_write_edge(dst,src_seg,src_state,dst_seg,dst_state,edge_labels);
        }
    }
    Print(infoLong,"done");
}

static void default_pull(lts_file_t dst,lts_file_t src){
    default_push(src,dst);
}

void lts_file_complete(lts_file_t lts){
    assert(!SYSTEM(lts)->fixed);
    lts=SYSTEM(lts);
    if (lts->read_init && !lts->push){
        lts->push=default_push;
    }
    if (lts->write_init && !lts->pull){
        lts->pull=default_pull;
    }
    lts->fixed=1;
}

uint32_t lts_get_init_count(lts_file_t lts){
    return SYSTEM(lts)->roots;
}

uint32_t lts_get_state_count(lts_file_t lts,int segment){
    return SYSTEM(lts)->states[segment];
}

uint64_t lts_get_edge_count(lts_file_t lts,int segment){
    return SYSTEM(lts)->edges[segment];
}


uint32_t lts_get_max_src_p1(lts_file_t lts,int segment){
    return SYSTEM(lts)->max_src_p1[segment];
}

uint32_t lts_get_max_dst_p1(lts_file_t lts,int segment){
    return SYSTEM(lts)->max_dst_p1[segment];
}

void lts_set_init_count(lts_file_t lts,uint32_t count){
    SYSTEM(lts)->roots=count;
}


void lts_set_state_count(lts_file_t lts,int segment,uint32_t count){
    SYSTEM(lts)->states[segment]=count;
}

void lts_set_edge_count(lts_file_t lts,int segment,uint64_t count){
    SYSTEM(lts)->edges[segment]=count;
}

void lts_file_sync(lts_file_t lts){
    lts=SYSTEM(lts);
    int peers=HREpeers(lts->ctx);
    HREreduce(lts->ctx,1,&lts->roots,&lts->roots,UInt32,Sum);
    HREreduce(lts->ctx,peers,lts->states,lts->states,UInt32,Sum);
    HREreduce(lts->ctx,peers,lts->edges,lts->edges,UInt64,Sum);
    HREreduce(lts->ctx,peers,lts->max_src_p1,lts->max_src_p1,UInt32,Max);
    HREreduce(lts->ctx,peers,lts->max_dst_p1,lts->max_dst_p1,UInt32,Max);  
}

int lts_file_owned_count(lts_file_t lts){
    int me=HREme(SYSTEM(lts)->ctx);
    int peers=HREpeers(SYSTEM(lts)->ctx);
    int N=SYSTEM(lts)->segments;
    return (N+peers-me-1)/peers; 
}

int lts_file_owned(lts_file_t lts,int nth){
    int me=HREme(SYSTEM(lts)->ctx);
    int peers=HREpeers(SYSTEM(lts)->ctx);
    return me+peers*nth;  
}

void lts_file_set_context(lts_file_t lts, hre_context_t ctx){
    SYSTEM(lts)->ctx = ctx;
}

hre_context_t lts_file_context(lts_file_t lts){
    if (lts == NULL) return NULL;
    return SYSTEM(lts)->ctx;
}

void lts_set_expected_value_count(lts_file_t lts,int type_no,uint32_t count){
    SYSTEM(lts)->expected_values[type_no]=count;
}

uint32_t lts_get_get_expected_value_count(lts_file_t lts,int type_no){
    return SYSTEM(lts)->expected_values[type_no];
}

stream_t lts_file_attach(lts_file_t lts,char *name){
    if (SYSTEM(lts)->attach) {
        return SYSTEM(lts)->attach(lts,name);
    }
    Abort("LTS file does not support attachements");
}

void lts_file_set_attach(lts_file_t file,lts_attach_m method){
    SYSTEM(file)->attach=method;
}


