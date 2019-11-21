// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <lts-io/internal.h>
#include <lts-lib/lts.h>
#include <ltsmin-lib/ltsmin-standard.h>

struct lts_file_s {
    lts_t lts;
    int edge_labels;
    uint32_t state_ofs;
    uint32_t trans_ofs;
    uint32_t init_count;
    uint32_t state_count;
    uint32_t* state_perseg;
    uint64_t edge_count;
    int segments;
};

static void write_init(lts_file_t file,int seg,void* state){
    uint32_t state_no;
    switch(lts_file_init_mode(file)){
    case Index:
        state_no=*((uint32_t*)state) * file->segments + seg;
        break;
    case SegVector:
    case Vector:
        if (seg != 0) Abort("(Seg)Vector format with multiple segments unsupported");
        state_no=TreeFold(file->lts->state_db,state);
        break;
    default:
        Abort("missing case");
    }
    if (file->init_count>=file->lts->root_count) {
        lts_set_size(file->lts,file->lts->root_count+32,file->lts->states,file->lts->transitions);
    }
    file->lts->root_list[file->init_count]=state_no;
    file->init_count++;
}

static void write_state(lts_file_t file,int seg,void* state,void*labels){
    uint32_t state_no;
    if (file->lts->state_db){
        state_no=TreeFold(file->lts->state_db,state);
    } else {
        state_no=file->state_perseg[seg]*file->segments+seg;
    }
    while(state_no>=file->lts->states) {
        lts_set_size(file->lts,file->lts->root_count,file->lts->states+32768,file->lts->transitions);
    }
    (void) state;
    if (file->lts->prop_idx) {
        file->lts->properties[state_no]=TreeFold(file->lts->prop_idx,labels);
    } else if (file->lts->properties) {
        file->lts->properties[state_no]=*((uint32_t*)labels);
    }
    file->state_perseg[seg]++;
}


static void write_edge(lts_file_t file,int src_seg,void* src_state,
                           int dst_seg,void*dst_state,void* labels
){
    uint32_t src_no;
    switch(lts_file_source_mode(file)){
    case Index:
        src_no=*((uint32_t*)src_state)*file->segments+src_seg;
        break;
    case SegVector:
    case Vector:
        if (src_seg != 0) Abort("(Seg)Vector format with multiple segments unsupported");
        src_no=TreeFold(file->lts->state_db,src_state);
        break;
    default:
        Abort("missing case");
    }
    uint32_t dst_no;
    switch(lts_file_dest_mode(file)){
    case Index:
        dst_no=*((uint32_t*)dst_state)*file->segments+dst_seg;
        break;
    case SegVector:
    case Vector:
        if (dst_seg != 0) Abort("(Seg)Vector format with multiple segments unsupported");
        dst_no=TreeFold(file->lts->state_db,dst_state);
        break;
    default:
        Abort("missing case");
    }
    if (file->edge_count>=file->lts->transitions) {
        lts_set_size(file->lts,file->lts->root_count,file->lts->states,file->lts->transitions+32768);
    }
    file->lts->src[file->edge_count]=src_no;
    file->lts->dest[file->edge_count]=dst_no;
    if (file->lts->edge_idx) {
        file->lts->label[file->edge_count]=TreeFold(file->lts->edge_idx,labels);
    } else if (file->lts->label) {
        file->lts->label[file->edge_count]=*((uint32_t*)labels);
    }
    file->edge_count++;
}

static void write_close(lts_file_t file){
    //if (file->init_count!=1) Abort("missing initial state"); in some cases no initial states makes sense!
    uint32_t pre_sum=0;
    for(int i=0;i<file->segments;i++) pre_sum+=file->state_perseg[i];
    uint32_t tmp;
    for (int i=0;i<file->segments;i++){
        tmp=lts_get_max_src_p1(file,i);
        if (tmp>file->state_perseg[i]) file->state_perseg[i]=tmp;
        tmp=lts_get_max_dst_p1(file,i);
        if (tmp>file->state_perseg[i]) file->state_perseg[i]=tmp;
    }
    file->state_count=0;
    for(int i=0;i<file->segments;i++) file->state_count+=file->state_perseg[i];
    if (pre_sum && pre_sum!=file->state_count) {
        Abort("edges use unwritten states");
    }
    uint32_t offset[file->segments];
    offset[0]=0;
    for(int i=1;i<file->segments;i++) offset[i]=offset[i-1]+file->state_perseg[i-1];
    uint32_t seg,ofs;
    for(uint32_t i=0;i<file->lts->root_count;i++){
        seg=file->lts->root_list[i]%file->segments;
        ofs=file->lts->root_list[i]/file->segments;
        file->lts->root_list[i]=offset[seg]+ofs;
    }
    for(uint32_t i=0;i<file->edge_count;i++){
        seg=file->lts->src[i]%file->segments;
        ofs=file->lts->src[i]/file->segments;
        file->lts->src[i]=offset[seg]+ofs;
        seg=file->lts->dest[i]%file->segments;
        ofs=file->lts->dest[i]/file->segments;
        file->lts->dest[i]=offset[seg]+ofs;
    }
    if (file->lts->properties){
        uint32_t* temp=file->lts->properties;
        file->lts->properties=(uint32_t*)RTmalloc(file->state_count*sizeof(uint32_t));
        for(int i=0;i<file->segments;i++){
            for(uint32_t j=0;j<file->state_perseg[i];j++){
                file->lts->properties[offset[i]+j]=temp[j*file->segments+i];
            }
        }
        RTfree(temp);
    }
    lts_set_size(file->lts,file->init_count,file->state_count,file->edge_count);
    file->lts->tau=-1;
    if (lts_type_get_edge_label_count(file->lts->ltstype)==1 &&
        strncmp(lts_type_get_edge_label_name(file->lts->ltstype,0),LTSMIN_EDGE_TYPE_ACTION_PREFIX,6)==0)
    {
        Print(infoShort,"action labeled, detecting silent step");
        int tableno=lts_type_get_edge_label_typeno(file->lts->ltstype,0);
        value_table_t vt=file->lts->values[tableno];
        int N = vt == NULL ? 0 : VTgetCount(vt);

        if (N > 0) {
            table_iterator_t it = VTiterator (vt);
            while (IThasNext(it)) {
                chunk c = ITnext (it);
                if ( (c.len==strlen(LTSMIN_EDGE_VALUE_TAU) &&
                             strcmp(c.data,LTSMIN_EDGE_VALUE_TAU)==0)
                  || (c.len==1 && strcmp(c.data,"i")==0)
                   )
                {
                    Print(infoShort,"invisible label is %s",c.data);
                    if (file->lts->tau>=0) Abort("two silent labels");
                    file->lts->tau = VTputChunk(vt, c);
                }
            }
        }
        if (file->lts->tau<0) {
            Print(infoShort,"no silent label");
        }
    }
}

lts_file_t lts_writer(lts_t lts,int segments,lts_file_t settings){
    lts_file_t file=lts_file_bare("<heap>",lts->ltstype,segments,settings,sizeof(struct lts_file_s));
    file->lts=lts;
    file->segments=segments;
    file->state_perseg=(uint32_t*)RTmallocZero(segments*sizeof(uint32_t));
    file->edge_labels=lts_type_get_edge_label_count(lts->ltstype);
    lts_file_set_write_init(file,write_init);
    lts_file_set_write_state(file,write_state);
    lts_file_set_write_edge(file,write_edge);
    lts_file_set_close(file,write_close);
    lts_file_complete(file);
    lts_set_type(file->lts,LTS_LIST);
    int T=lts_type_get_type_count(lts->ltstype);
    for(int i=0;i<T;i++){
        lts_file_set_table(file,i,lts->values[i]);
    }
    return file;
}

static int read_init(lts_file_t file,int *seg,void* state){
    if (file->init_count<file->lts->root_count) {
        *((uint32_t*)seg)=file->lts->root_list[file->init_count]%file->segments;
        *((uint32_t*)state)=file->lts->root_list[file->init_count]/file->segments;
        file->init_count++;
        return 1;
    } else {
        return 0;
    }
}

static int read_state(lts_file_t file,int *seg,void* state,void*labels){
    if (file->state_count<file->lts->states){
        *seg=file->state_count%file->segments;
        if (file->lts->prop_idx) {
            TreeUnfold(file->lts->prop_idx,file->lts->properties[file->state_count],labels);
        } else if (file->lts->properties) {
            *((uint32_t*)labels)=file->lts->properties[file->state_count];
        }
        if (file->lts->state_db) {
            TreeUnfold(file->lts->state_db,file->state_count,state);
        } else {
            // TODO: decide if reading state number is allowed/required.
            *((uint32_t*)state)=file->state_count;
        }
        file->state_count++;
        return 1;
    } else {
        return 0;
    }
}

static int read_edge(lts_file_t file,int *src_seg,void* src_state,
                           int *dst_seg,void*dst_state,void* labels
){
    if (file->edge_count<file->lts->transitions) {
        *((uint32_t*)src_seg)=file->lts->src[file->edge_count]%file->segments;
        *((uint32_t*)src_state)=file->lts->src[file->edge_count]/file->segments;
        *((uint32_t*)dst_seg)=file->lts->dest[file->edge_count]%file->segments;
        *((uint32_t*)dst_state)=file->lts->dest[file->edge_count]/file->segments;
        if (file->lts->edge_idx) {
            TreeUnfold(file->lts->edge_idx,file->lts->label[file->edge_count],labels);
        } else if (file->lts->label) {
            *((uint32_t*)labels)=file->lts->label[file->edge_count];
        }
        file->edge_count++;
        return 1;
    } else {
        return 0;
    }
}

static void read_close(lts_file_t file){
    (void)file;
}

lts_file_t lts_reader(lts_t lts,int segments,lts_file_t settings){
    lts_file_t file=lts_file_bare("<heap>",lts->ltstype,segments,settings,sizeof(struct lts_file_s));
    file->lts=lts;
    file->segments=segments;
    file->edge_labels=lts_type_get_edge_label_count(lts->ltstype);
    lts_file_set_read_init(file,read_init);
    lts_file_set_read_state(file,read_state);
    lts_file_set_read_edge(file,read_edge);
    lts_file_set_close(file,read_close);
    lts_file_complete(file);
    int T=lts_type_get_type_count(lts->ltstype);
    for(int i=0;i<T;i++){
        lts_file_set_table(file,i,lts->values[i]);
    }
    lts_set_type(file->lts,LTS_LIST);
    file->init_count=0;
    file->state_count=0;
    file->edge_count=0;
    return file;
}
