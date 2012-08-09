// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/user.h>
#include <ltsmin-reduce-dist/sig-array.h>

struct sig_array_s {
    seg_lts_t lts;
    int seg;
    sig_id_t *current;
    sig_id_t **dest_id;
    enum {IDLE,SEND_ID,WAIT_ID,COMPUTE_ID} state;
    int count;
    int my_states;
    int id_count;
    int out_edges;
    uint32_t *dest_begin;
    uint32_t *dest_seg;
    uint32_t *dest_edge;
    uint32_t *dest_label;
};

sig_array_t SigArrayCreate(seg_lts_t lts,int seg,const char *sig_type){
    if (!sig_type) Fatal(1,error,"sig_type argument cannot be NULL");
    if (strcmp(sig_type,"strong")) Fatal(1,error,"signature type %s not supported",sig_type);
    if (seg != SLTSsegmentNumber(lts)) Fatal(1,error,"segment number mismatch %d != %d",seg,SLTSsegmentNumber(lts));
    if (SLTSgetLayout(lts)!=Succ_Pred) Fatal(1,error,"layout must be Succ_Pred");
    sig_array_t sa=RT_NEW(struct sig_array_s);
    sa->lts=lts;
    sa->seg=seg;
    sa->current=RTmalloc(sizeof(sig_id_t)*SLTSstateCount(lts));
    sa->state=IDLE;
    sa->my_states=SLTSstateCount(lts);
    sa->out_edges=SLTSoutgoingCount(lts);
    sa->dest_begin=SLTSmapOutBegin(lts);
    sa->dest_seg=SLTSmapOutField(lts,1);
    sa->dest_edge=SLTSmapOutField(lts,2);
    sa->dest_label=SLTSmapOutField(lts,3);
    return sa;
}

void SigArrayDestroy(sig_array_t sa,sig_id_t **save_equivalence){
    if (save_equivalence){
        *save_equivalence=sa->current;
    } else {
        RTfree(sa->current);
    }
    // TODO: free of dest_id
    // TODO: unmapping of dest_xxx
    RTfree(sa);
}

void SigArraySetID(sig_array_t sa,s_idx_t state,sig_id_t id){
    sa->id_count++;
    sa->current[state]=id;
}

sig_id_t SigArrayGetID(sig_array_t sa,s_idx_t state){
    return sa->current[state];
}

void SigArraySetDestID(sig_array_t sa,int dst_seg,e_idx_t edge,sig_id_t id){
    sa->id_count--;
    sa->dest_id[dst_seg][edge]=id;
}

sig_event_t SigArrayNext(sig_array_t sa){
    switch(sa->state){
        case IDLE:
            Fatal(1,error,"Attempt to fetch an event while idle.");
            break;
        case SEND_ID:
            if(sa->count<sa->my_states){
                sig_event_t event;
                event.what=ID_READY;
                event.where=sa->count;
                sa->count++;
                return event;
            }
            sa->state=WAIT_ID;
            Debug("transition to WAIT");
        case WAIT_ID:
            TQwhile(SLTSgetQueue(sa->lts),&sa->id_count);
            sa->count=0;
            sa->state=COMPUTE_ID;
            Debug("transition to COMPUTE");
        case COMPUTE_ID:
            if(sa->count<sa->my_states){
                sig_event_t event;
                event.what=SIG_READY;
                event.where=sa->count;
                sa->count++;
                return event;
            }
            if(sa->count==sa->my_states){
                sa->state=IDLE;
                Debug("transition to IDLE");
                return (sig_event_t){.what=COMPLETED};
            }
    }
    Fatal(1,error,"Internal error: missing case in SigArrayNext");
    return (sig_event_t){.what=COMPLETED};
}

void SigArrayStartRound(sig_array_t sa){
    Debug("id_count is %d",sa->id_count);
    int segment_count=SLTSsegmentCount(sa->lts);
    if (sa->dest_id ==NULL) {
        sa->dest_id=(sig_id_t**)RTmallocZero(segment_count*sizeof(sig_id_t*));
        e_idx_t count[segment_count];
        for(int i=0;i<segment_count;i++) count[i]=0;
        lts_type_t ltstype=SLTSgetType(sa->lts);
        int edge_length=3+lts_type_get_edge_label_count(ltstype);
        uint32_t edge[edge_length];
        for(int i=0;i<sa->my_states;i++){
            int N=SLTSoutCountState(sa->lts,i);
            for(int j=0;j<N;j++){
                SLTSgetOutEdge(sa->lts,i,j,edge);
                count[edge[1]]++;
            }
        }
        for(int i=0;i<segment_count;i++){
            Debug("count %d edge to %d",count[i],i);
            sa->dest_id[i]=(sig_id_t*)RTmallocZero(count[i]*sizeof(sig_id_t));
        }
    }
    if (sa->state != IDLE) Fatal(1,error,"bad calling sequence");
    sa->state=SEND_ID;
    sa->count=0;
    sa->id_count=sa->out_edges;
}

struct sig_entry {
    sig_id_t id;
    uint32_t label;
};

#define SIGMAX 4096

static volatile int len;
static char temp[SIGMAX*16];
static struct sig_entry sig[SIGMAX];

static void id_string(sig_id_t id){
/*
    do {
        sig_id_t digit=id%52;
        id=id/52;
        if (digit<26) temp[len]='a'+digit;
        else temp[len]='A'+digit-26;
        len++;
    } while(id>0);

    memcpy(temp+len,&id,sizeof(sig_id_t));
    len+=sizeof(sig_id_t);
*/
    do {
        uint32_t digit=id%255;
        id=id/255;
        temp[len]=digit+1;
        len++;
    } while(id>0);
}

static void lbl_string(uint32_t id){
/*
    do {
        uint32_t digit=id%10;
        id=id/10;
        temp[len]='0'+digit;
        len++;
    } while(id>0);


    memcpy(temp+len,&id,4);
    len+=4;
*/
    temp[len]=0;
    len++;
    do {
        uint32_t digit=id%255;
        id=id/255;
        temp[len]=digit+1;
        len++;
    } while(id>0);
    temp[len]=0;
    len++;
}

chunk SigArrayGetSig(sig_array_t sa,s_idx_t state){
    int N=0;
/*
    lts_type_t ltstype=SLTSgetType(sa->lts);
    int edge_length=3+lts_type_get_edge_label_count(ltstype);
    uint32_t edge[edge_length];
    int K=SLTSoutCountState(sa->lts,state);
    for(int i=0;i<K;i++){
        SLTSgetOutEdge(sa->lts,state,i,edge);
        struct sig_entry e;
        //Debug("add [%d.%d]==%lld to sig of %d",edge[1],edge[2],sa->dest_id[edge[1]][edge[2]],state);
        e.id=sa->dest_id[edge[1]][edge[2]];
        e.label=edge[3];
        int k=0;
        while(k<N && sig[k].label<e.label) k++;
        while(k<N && sig[k].label==e.label && sig[k].id<e.id) k++;
        if (k<N && sig[k].label==e.label && sig[k].id==e.id) continue;
        if (N>=64) Fatal(1,error,"exceeded max sig length");
        for(int j=N;j>k;j--) sig[j]=sig[j-1];
        sig[k]=e;
        N++;
    }
*/

    for(uint32_t i=sa->dest_begin[state];i<sa->dest_begin[state+1];i++){
        struct sig_entry e;
        //Debug("add [%d.%d]==%lld to sig of %d",edge[1],edge[2],sa->dest_id[edge[1]][edge[2]],state);
        e.id=sa->dest_id[sa->dest_seg[i]][sa->dest_edge[i]];
        e.label=sa->dest_label[i];
        int k=0;
        while(k<N && sig[k].label<e.label) k++;
        while(k<N && sig[k].label==e.label && sig[k].id<e.id) k++;
        if (k<N && sig[k].label==e.label && sig[k].id==e.id) continue;
        if (N>=SIGMAX) Fatal(1,error,"exceeded max sig length");
        for(int j=N;j>k;j--) sig[j]=sig[j-1];
        sig[k]=e;
        N++;
    }



    //Debug("state %d has %d edges",state,N);

    len=0;

    //char *ptr=temp+sprintf(temp,"%s",id_string(sa->current[state]));
    id_string(sa->current[state]);
    for(int i=0;i<N;i++){
        //ptr+=sprintf(ptr,"%d%s",sig[i].label,id_string(sig[i].id));
        lbl_string(sig[i].label);
        id_string(sig[i].id);
    }
    //ptr=temp+sprintf(temp,"%s",id_string(sa->current[state]));
    //for(int i=0;i<N;i++){
    //     ptr+=sprintf(ptr,"%d%s",sig[i].label,id_string(sig[i].id));
    //}
    //return chunk_str(temp);
/*
    memcpy(temp,&sa->current[state],sizeof(sig_id_t));
    int len=sizeof(sig_id_t);
    for(int i=0;i<N;i++){
        memcpy(temp+len,&sig[i],sizeof(struct sig_entry));
        len+=sizeof(struct sig_entry);
    }
    */
    return chunk_ld(len,temp);
}

