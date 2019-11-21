// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <assert.h>
#include <string.h>

#include <lts-io/provider.h>
#include <hre/user.h>
#include <ltsmin-reduce-dist/seg-lts.h>
#include <util-lib/dynamic-array.h>

struct seg_lts_s {
    lts_type_t sig;
    seg_lts_layout_t layout;
    int root_seg;
    uint32_t root_ofs;
    int seg_no;
    int seg_count;
    int state_length;
    int state_labels;
    int state_width;
    int edge_labels;
    int vt_count;
    value_table_t *vt;
    hre_task_queue_t task_queue;
    s_idx_t state_count;
    uint32_t *in_count;
    uint32_t *out_count;
    matrix_table_t state_table;
    matrix_table_t in_edges;
    matrix_table_t out_edges;
};

value_table_t SLTStable(seg_lts_t lts,int type_no){
    return lts->vt[type_no];
}

lts_type_t SLTSgetType(seg_lts_t lts){
    return lts->sig;
}

int SLTSsegmentCount(seg_lts_t lts){
    return lts->seg_count;
}

int SLTSsegmentNumber(seg_lts_t lts){
    return lts->seg_no;
}

hre_task_queue_t SLTSgetQueue(seg_lts_t lts){
    return lts->task_queue;
}

seg_lts_t SLTScreate(lts_type_t signature,hre_task_queue_t task_queue,seg_lts_layout_t layout){
    seg_lts_t lts=RT_NEW(struct seg_lts_s);
    lts->sig=signature;
    lts->task_queue=task_queue;
    lts->seg_no=HREme(TQcontext(task_queue));
    lts->seg_count=HREpeers(TQcontext(task_queue));
    lts->state_length=lts_type_get_state_length(signature);
    lts->state_labels=lts_type_get_state_label_count(signature);
    lts->edge_labels=lts_type_get_edge_label_count(signature);
    lts->layout=layout;
    lts->vt_count=lts_type_get_type_count(signature);
    lts->vt=RTmalloc(lts->vt_count*sizeof(value_table_t));
    for(int i=0;i<lts->vt_count;i++) {
        char *type_name=lts_type_get_type(signature,i);
        lts->vt[i]=HREcreateTable(TQcontext(task_queue),type_name);
    }
    return lts;
}

int SLTSstateCount(seg_lts_t lts){
       return lts->state_count;
}

int SLTSoutgoingCount(seg_lts_t lts){
    if (lts->out_edges) {
        return MTgetCount(lts->out_edges);
    } else {
        return 0;
    }
}

int SLTSincomingCount(seg_lts_t lts){
    if (lts->in_edges) {
        return MTgetCount(lts->in_edges);
    } else {
        return 0;
    }
}

seg_lts_layout_t SLTSgetLayout(seg_lts_t slts){
    return slts->layout;
}

const char* SLTSlayoutString(seg_lts_layout_t layout){
    switch(layout){
        case In_List:       return "In_List";
        case Out_List:      return "Out_List";
        case IO_Edge_List:  return "IO_Edge_List";
        case Succ_Pred:     return "Succ_Pred";
    }
    return NULL;
}

seg_lts_t SLTSload(const char*name,hre_task_queue_t task_queue){
    lts_file_t input=lts_file_open(name);
    seg_lts_layout_t layout;
    switch(lts_file_get_edge_owner(input)){
    case SourceOwned:
        layout=Out_List;
        break;
    case DestOwned:
        layout=In_List;
        break;
    default:
        Fatal(1,error,"missing case in SLTSloadSegment");
    }
    if(Index!=lts_file_source_mode(input) || Index!=lts_file_dest_mode(input)){
        Abort("indexed input only");
    }
    lts_type_t ltstype=lts_file_get_type(input);
    int NV=lts_type_get_state_length(ltstype);
    int NS=lts_type_get_state_label_count(ltstype);
    int NT=lts_type_get_type_count(ltstype);
    seg_lts_t lts=SLTScreate(ltstype,task_queue,layout);
    if (lts_file_get_segments(input)!=lts->seg_count) {
        Abort("number of segments in input(%d) different from worker count(%d)",
            lts_file_get_segments(input),lts->seg_count);
    }
    for(int i=0;i<NT;i++){
        lts_file_set_table(input,i,lts->vt[i]);
    }
    if (lts->seg_no==0){
        Debug("reading initial state");
        lts_read_init(input,&lts->root_seg,&lts->root_ofs);
    }
    lts->state_width=NV+NS;
    if (lts->state_width){
        Debug("reading state vectors and labels");
        lts->state_table=MTcreate(lts->state_width);
        uint32_t row[lts->state_width];
        uint32_t extra;
        uint32_t* vec=NV?row:&extra;
        uint32_t* lbl=row+NV;
        int seg;
        while(lts_read_state(input,&seg,vec,lbl)){
            MTaddRow(lts->state_table,row);
        }
    } else {
        Debug("not reading state vectors and labels");
    }
    switch(layout){
    case In_List:
        {
        lts->in_edges=MTcreate(lts->edge_labels+3);
        uint32_t row[lts->edge_labels+3];
        uint32_t extra;
        uint32_t *lbl=row+3;
        uint32_t *src_seg=row;
        uint32_t *src_ofs=row+1;
        uint32_t *dst_seg=&extra;
        uint32_t *dst_ofs=row+2;
        *dst_seg=HREme(TQcontext(task_queue));
        Debug("reading in edges");
        while(lts_read_edge(input,(int*)src_seg,src_ofs,(int*)dst_seg,dst_ofs,lbl)){
            MTaddRow(lts->in_edges,row);
        }
        break;
        }
    case Out_List:
        {
        lts->out_edges=MTcreate(lts->edge_labels+3);
        uint32_t row[lts->edge_labels+3];
        uint32_t extra;
        uint32_t *lbl=row+3;
        uint32_t *src_seg=&extra;
        uint32_t *src_ofs=row;
        uint32_t *dst_seg=row+1;
        uint32_t *dst_ofs=row+2;
        *src_seg=HREme(TQcontext(task_queue));
        Debug("reading out edges");
        while(lts_read_edge(input,(int*)src_seg,src_ofs,(int*)dst_seg,dst_ofs,lbl)){
            MTaddRow(lts->out_edges,row);
        }
        break;
        }
    default:
        Fatal(1,error,"unsupported layout %s",SLTSlayoutString(layout));
    }
    Debug("edges read");
    lts->state_count=lts_get_state_count(input,lts->seg_no);
    lts_file_close(input);
    return lts;
}

static void matrix_table_add(void *context,int from,int len,void*arg){
    (void)len;(void)from;
    matrix_table_t mt=(matrix_table_t)context;
    uint32_t *row=(uint32_t *)arg;
    MTaddRow(mt,row);
}

static void In_List_2_IO_Edge_List(seg_lts_t lts){
    lts->out_edges=MTcreate(lts->edge_labels+3);
    hre_task_t task=TaskCreate(lts->task_queue,0,4096,matrix_table_add,lts->out_edges,(lts->edge_labels+3)*4);
    TQwait(lts->task_queue);
    int N=MTgetCount(lts->in_edges);
    uint32_t row[lts->edge_labels+3];
    lts->in_count=(uint32_t*)RTmallocZero(lts->seg_count*sizeof(uint32_t));
    for(int i=0;i<N;i++){
        MTgetRow(lts->in_edges,i,row);
        uint32_t src_seg=row[0];
        uint32_t src_ofs=row[1];
        uint32_t dst_seg=lts->seg_no;
        //uint32_t dst_ofs=row[2];
        uint32_t edge=lts->in_count[src_seg];
        lts->in_count[src_seg]++;
        //src_seg; skip src_seg for out-edge
        row[0]=src_ofs;
        row[1]=dst_seg;
        row[2]=edge;
        MTupdate(lts->in_edges,i,1,edge);
        TaskSubmitFixed(task,src_seg,row);
    }
    TQwait(lts->task_queue);
    TaskDestroy(task);
    lts->layout=IO_Edge_List;
}

static void Out_List_2_Succ_Pred(seg_lts_t lts){
    lts->in_edges=MTcreate(lts->edge_labels+3);
    Debug("cluster out edges");
    MTclusterBuild(lts->out_edges,0,SLTSstateCount(lts)); // cluster out edges on src_ofs;
    MTclusterSort(lts->out_edges,3); // sort outgoing edges on label;
    Debug("copying edges");
    hre_task_t task=TaskCreate(lts->task_queue,0,4096,matrix_table_add,lts->in_edges,(lts->edge_labels+3)*4);
    TQwait(lts->task_queue);
    uint32_t N=MTclusterCount(lts->out_edges);
    uint32_t row[lts->edge_labels+3];
    uint32_t edge_no[lts->seg_count];
    for(int i=0;i<lts->seg_count;i++){
        edge_no[i]=0;
    }
    for(uint32_t i=0;i<N;i++){
        int K=MTclusterSize(lts->out_edges,i);
        for(int j=0;j<K;j++){
            MTclusterGetRow(lts->out_edges,i,j,row);
            uint32_t src_seg=lts->seg_no;
            //uint32_t src_ofs=row[0];
            uint32_t dst_seg=row[1];
            //uint32_t dst_ofs=row[2];
            uint32_t edge=edge_no[dst_seg];
            edge_no[dst_seg]++;
            row[0]=src_seg;
            row[1]=edge;
            //dst_seg; skip dst seg for in-edge
            //row[2]=dst_ofs;
            MTclusterUpdate(lts->out_edges,i,j,2,edge);
            TaskSubmitFixed(task,dst_seg,row);
        }
    }
    TQwait(lts->task_queue);
    TaskDestroy(task);
    MTclusterBuild(lts->in_edges,2,SLTSstateCount(lts)); // cluster in edges on dst_ofs;
    Debug("sorting");
    lts->layout=Succ_Pred;
}

static void assign_dest_map(void *context,int from,int len,void*arg){
    (void)len;
    uint32_t** dest_map=(uint32_t**)context;
    uint32_t* msg=(uint32_t*)arg;
    dest_map[from][msg[0]]=msg[1];
}

void SLTSapplyMap(seg_lts_t lts,uint32_t *map,uint32_t tau){
    (void)map;
    if(lts->layout!=Succ_Pred) Fatal(1,error,"apply map needs Succ_Pred input");
    //uint32_t row[lts->edge_labels+3];
    if(lts->out_count==NULL) {
        TQwait(lts->task_queue);
        Warning(info,"counting out edges");
        lts->out_count=(uint32_t*)RTmallocZero(lts->seg_count*sizeof(uint32_t));
        for(uint32_t i=0;i<lts->state_count;i++){
            int K=MTclusterSize(lts->out_edges,i);
            for(int j=0;j<K;j++){
                lts->out_count[MTclusterGetElem(lts->out_edges,i,j,1)]++;
            }
        }
    }
    uint32_t* dest_map[lts->seg_count];
    for(int i=0;i<lts->seg_count;i++){
        dest_map[i]=(uint32_t*)RTmalloc(lts->out_count[i]*sizeof(uint32_t));
    }
    hre_task_t task=TaskCreate(lts->task_queue,0,4096,assign_dest_map,dest_map,8);
    TQwait(lts->task_queue);
    for(uint32_t i=0;i<lts->state_count;i++){
        int K=MTclusterSize(lts->in_edges,i);
        uint32_t msg[2];
        msg[1]=map[i];
        for(int j=0;j<K;j++){
            uint32_t seg=MTclusterGetElem(lts->in_edges,i,j,0);
            msg[0]=MTclusterGetElem(lts->in_edges,i,j,1);
            TaskSubmitFixed(task,seg,msg);
        }
    }
    TQwait(lts->task_queue);
    TaskDestroy(task);
    MTdestroyZ(&lts->in_edges);
    matrix_table_t new_out=MTcreate(lts->edge_labels+3);
    task=TaskCreate(lts->task_queue,0,4096,matrix_table_add,new_out,(lts->edge_labels+3)*4);
    TQwait(lts->task_queue);
    uint32_t row[lts->edge_labels+3];
    int W=lts->seg_count;
    for(uint32_t i=0;i<lts->state_count;i++){
        int K=MTclusterSize(lts->out_edges,i);
        for(int j=0;j<K;j++){
            MTclusterGetRow(lts->out_edges,i,j,row);
            uint32_t dst=dest_map[row[1]][row[2]];
            if (map[row[0]]==dst && row[3]==tau) continue; // skip tau self-loops
            uint32_t owner=map[row[0]]%W;
            row[0]=map[row[0]]/W;
            row[1]=dst%W;
            row[2]=dst/W;
            TaskSubmitFixed(task,owner,row);
        }
    }
    TQwait(lts->task_queue);
    TaskDestroy(task);
    //for(int i=0;i<lts->seg_count;i++) RTfree(dest_map[i]);
    MTdestroy(lts->out_edges);
    lts->out_edges=MTcreate(lts->edge_labels+3);
    MTsimplify(lts->out_edges,new_out);
    MTdestroy(new_out);
    RTfree(lts->out_count);
    lts->out_count=NULL;
    if (lts->in_count) {
        RTfree(lts->in_count);
        lts->in_count=NULL;
    }
    lts->state_count=MTgetMax(lts->out_edges,0)+1;
    lts->layout=Out_List;

    uint32_t tmp_seg=0;
    uint32_t tmp_ofs=0;
    if (lts->root_seg==lts->seg_no){
        uint32_t root_map=map[lts->root_ofs];
        tmp_seg=root_map%W;
        tmp_ofs=root_map/W;
    }
    HREreduce(TQcontext(lts->task_queue),1,&tmp_seg,&lts->root_seg,UInt32,Max);
    HREreduce(TQcontext(lts->task_queue),1,&tmp_ofs,&lts->root_ofs,UInt32,Max);
    if (0==lts->seg_no)Warning(info,"root is %u/%u",lts->root_seg,lts->root_ofs);
}

static void Out_List_2_IO_Edge_List(seg_lts_t lts){
    lts->in_edges=MTcreate(lts->edge_labels+3);
    hre_task_t task=TaskCreate(lts->task_queue,0,4096,matrix_table_add,lts->in_edges,(lts->edge_labels+3)*4);
    TQwait(lts->task_queue);
    int N=MTgetCount(lts->out_edges);
    uint32_t row[lts->edge_labels+3];
    lts->out_count=(uint32_t*)RTmallocZero(lts->seg_count*sizeof(uint32_t));
    for(int i=0;i<N;i++){
        MTgetRow(lts->out_edges,i,row);
        uint32_t src_seg=lts->seg_no;
        //uint32_t src_ofs=row[0];
        uint32_t dst_seg=row[1];
        //uint32_t dst_ofs=row[2];
        uint32_t edge=lts->out_count[dst_seg];
        lts->out_count[dst_seg]++;
        row[0]=src_seg;
        row[1]=edge;
              //dst_seg; skip dst seg for in-edge
        //row[2]=dst_ofs;
        MTupdate(lts->out_edges,i,2,edge);
        TaskSubmitFixed(task,dst_seg,row);
    }
    TQwait(lts->task_queue);
    TaskDestroy(task);
    lts->layout=IO_Edge_List;
}

static void IO_Edge_List_2_Succ_Pred(seg_lts_t lts){
    Debug("share is %d states, and %d + %d edges (in+out)",
        SLTSstateCount(lts),
        SLTSincomingCount(lts),
        SLTSoutgoingCount(lts));
    MTclusterBuild(lts->in_edges,2,SLTSstateCount(lts)); // cluster in edges on dst_ofs;
    MTclusterBuild(lts->out_edges,0,SLTSstateCount(lts)); // cluster out edges on src_ofs;
    Debug("sorting");
    MTclusterSort(lts->out_edges,3); // sort outgoing edges on label;
    lts->layout=Succ_Pred;
}

static void In_List_2_Succ_Pred(seg_lts_t lts){
    In_List_2_IO_Edge_List(lts);
    IO_Edge_List_2_Succ_Pred(lts);
}

void SLTSsetLayout(seg_lts_t lts,seg_lts_layout_t layout){
    if (layout==lts->layout) {
        Debug("requested layout %s is equal to current layout",SLTSlayoutString(layout));
        return;
    }
    Debug("changing layout from %s to %s",SLTSlayoutString(lts->layout),SLTSlayoutString(layout));
    switch(lts->layout){
    case In_List:
        switch(layout){
        case IO_Edge_List:
            In_List_2_IO_Edge_List(lts);
            return;
        case Succ_Pred:
            In_List_2_Succ_Pred(lts);
            return;
        default:
            goto fail;
        }
    case Out_List:
        switch(layout){
        case IO_Edge_List:
            Out_List_2_IO_Edge_List(lts);
            return;
        case Succ_Pred:
            //Out_List_2_IO_Edge_List(lts);
            //IO_Edge_List_2_Succ_Pred(lts);
            Out_List_2_Succ_Pred(lts);
            return;
        default:
            goto fail;
        }
    case IO_Edge_List:
        switch(layout){
        case Succ_Pred:
            IO_Edge_List_2_Succ_Pred(lts);
            return;
        default:
            goto fail;
        }
    default:
        goto fail;
    }
    fail: Fatal(1,error,"this conversion is not implemented");
}

int SLTSfirstInEdge(seg_lts_t lts,s_idx_t state,int*edge_no){
    uint32_t row[lts->edge_labels+3];
    matrix_table_t mt=lts->in_edges;
    int N=MTgetCount(mt);
    for(int i=0;i<N;i++){
        MTgetRow(mt,i,row);
        if(row[2]==state) {
            *edge_no=i;
            return 1;
        }
    }
    return 0;
}

int SLTSnextInEdge(seg_lts_t lts,s_idx_t state,int*edge_no){
    uint32_t row[lts->edge_labels+3];
    matrix_table_t mt=lts->in_edges;
    int N=MTgetCount(mt);
    for(int i=(*edge_no)+1;i<N;i++){
        MTgetRow(mt,i,row);
        if(row[2]==state) {
            *edge_no=i;
            return 1;
        }
    }
    return 0;
}

int SLTSfirstOutEdge(seg_lts_t lts,s_idx_t state,int*edge_no){
    uint32_t row[lts->edge_labels+3];
    matrix_table_t mt=lts->out_edges;
    int N=MTgetCount(mt);
    for(int i=0;i<N;i++){
        MTgetRow(mt,i,row);
        if(row[0]==state) {
            *edge_no=i;
            return 1;
        }
    }
    return 0;
}

int SLTSnextOutEdge(seg_lts_t lts,s_idx_t state,int*edge_no){
    uint32_t row[lts->edge_labels+3];
    matrix_table_t mt=lts->out_edges;
    int N=MTgetCount(mt);
    for(int i=(*edge_no)+1;i<N;i++){
        MTgetRow(mt,i,row);
        if(row[0]==state) {
            *edge_no=i;
            return 1;
        }
    }
    return 0;
}

int SLTSinCountState(seg_lts_t lts,s_idx_t state){
    return MTclusterSize(lts->in_edges,state);
}

void SLTSgetInEdge(seg_lts_t lts,s_idx_t state,int edge_no,uint32_t *edge){
    MTclusterGetRow(lts->in_edges,state,edge_no,edge);
}

uint32_t SLTSgetInEdgeField(seg_lts_t lts,s_idx_t state,int edge_no,int field){
    return MTclusterGetElem(lts->in_edges,state,edge_no,field);
}

int SLTSoutCountState(seg_lts_t lts,s_idx_t state){
    return MTclusterSize(lts->out_edges,state);
}

void SLTSgetOutEdge(seg_lts_t lts,s_idx_t state,int edge_no,uint32_t *edge){
    MTclusterGetRow(lts->out_edges,state,edge_no,edge);
}

uint32_t* SLTSmapInBegin(seg_lts_t lts){
    return MTclusterMapBegin(lts->in_edges);
}

uint32_t* SLTSmapInField(seg_lts_t lts,int field){
    return MTclusterMapColumn(lts->in_edges,field);
}

uint32_t* SLTSmapOutBegin(seg_lts_t lts){
    return MTclusterMapBegin(lts->out_edges);
}

uint32_t* SLTSmapOutField(seg_lts_t lts,int field){
    return MTclusterMapColumn(lts->out_edges,field);
}

void SLTSstore(seg_lts_t lts,const char* name){
    if (lts->layout!=Out_List) Fatal(1,error,"Out_List only");
    lts_file_t settings=lts_index_template();
    lts_file_set_edge_owner(settings,SourceOwned);
    lts_file_t output=lts_file_create(name,lts->sig,lts->seg_count,settings);
    HREbarrier(HREglobal());
    int T=lts_type_get_type_count(lts->sig);
    for(int i=0;i<T;i++){
        lts_file_set_table(output,i,lts->vt[i]);
    }
    if(lts->seg_no==0) lts_write_init(output,lts->root_seg,&lts->root_ofs);
    uint32_t row[lts->edge_labels+3];
    matrix_table_t mt=lts->out_edges;
    int N=MTgetCount(mt);
    for(int i=0;i<N;i++){
        MTgetRow(mt,i,row);
        lts_write_edge(output,lts->seg_no,row+0,row[1],row+2,row+3);
    }
    lts_file_close(output);
    HREbarrier(HREglobal());
}



