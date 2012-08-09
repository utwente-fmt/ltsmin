// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre/runtime.h>
#include <hre-mpi/user.h>
#include <lts-lib/set.h>
#include <ltsmin-reduce-dist/sigmin-types.h>
#include <ltsmin-reduce-dist/sigmin-set.h>
#include <util-lib/bitset.h>

static int mpi_nodes=0;
static int mpi_me=0;

static event_queue_t mpi_queue;
static event_barrier_t barrier;

static int synch_buffer_size=16000;
static int synch_receive_size=0;

struct poptOption sigmin_set_options[] = {
    {"synch-buffer",0,POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT,
        &synch_buffer_size,0,
     "size of signature synchronization buffer","<size>"},
    POPT_TABLEEND
};

#define BARRIER_TAG 1
#define SYNCH_ASK_TAG 2
#define SYNCH_REPLY_TAG 3
#define EM_TAG 4
#define EID_TAG 5
#define SIG_TAG 6
#define FWD_TAG 7
#define INV_TAG 8

/** FWD_BUFFER_SIZE must be multiple of 6 **/
#define FWD_BUFFER_SIZE 12288
#define INV_BUFFER_SIZE 12288
#define EM_BUFFER_SIZE 12288
#define EID_BUFFER_SIZE 12288
#define SIG_BUFFER_SIZE 12288

#define SET_TAG_UNDEF (-1)
#define SET_TAG_ASKED (-2)

#define OLD_ID_LABEL (1<<30)

/****** memory mapped LTS *********/

static uint32_t *in_begin=NULL;
static uint32_t *in_seg=NULL;
static uint32_t *in_edge=NULL;
static uint32_t *in_label=NULL;
static uint32_t *in_count=NULL;

static uint32_t **in_state=NULL;

static uint32_t my_states=0;

static int max_fanout=0;

static uint32_t *dest_begin=NULL;
static uint32_t *dest_seg=NULL;
static uint32_t *dest_edge=NULL;
static uint32_t *dest_label=NULL;
static uint32_t *out_count=NULL;

static void map_lts(seg_lts_t lts){
    Debug("mapping LTS");
    my_states=SLTSstateCount(lts);

    dest_begin=SLTSmapOutBegin(lts);
    dest_seg=SLTSmapOutField(lts,1);
    dest_edge=SLTSmapOutField(lts,2);
    dest_label=SLTSmapOutField(lts,3);

    in_begin=SLTSmapInBegin(lts);
    in_seg=SLTSmapInField(lts,0);
    in_edge=SLTSmapInField(lts,1);
    in_label=SLTSmapInField(lts,3);

    Debug("counting in edges");
    in_count=(uint32_t*)RTmallocZero(mpi_nodes*sizeof(uint32_t));
    for(uint32_t i=0;i<my_states;i++){
        for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
            in_count[in_seg[j]]++;
        }
    }
    for(int i=0;i<mpi_nodes;i++){
        Debug("in count from %d is %d",i,in_count[i]);
    }
    Debug("counting out edges");
    out_count=(uint32_t*)RTmallocZero(mpi_nodes*sizeof(uint32_t));
    for(uint32_t i=0;i<my_states;i++){
        for(uint32_t j=dest_begin[i];j<dest_begin[i+1];j++){
            out_count[dest_seg[j]]++;
        }
    }
    for(int i=0;i<mpi_nodes;i++){
        Debug("out count to %d is %d",i,out_count[i]);
    }
    if (mpi_me==0) Debug("scanning for worst case fanout");
    int max_local=0;
    for(uint32_t i=0;i<my_states;i++){
        int fanout=dest_begin[i+1]-dest_begin[i];
        if (fanout>max_local) max_local=fanout;
    }
    MPI_Allreduce(&max_local,&max_fanout,1,MPI_INT,MPI_MAX,MPI_COMM_WORLD);
    if (mpi_me==0) Warning(info,"worst case fanout is %d",max_fanout);

    Debug("local LTS info ready");
}

/********** maintaining ID maps and signature **************/

static int *set=NULL; // current signature of a state.
static int **map=NULL; // current map ID for dest edges.
static int *oldid=NULL; // previous round id for owned states.
static int **tmp_map=NULL; // temp space for sending id's to predecessor.
static MPI_Status *status_array=NULL;
static MPI_Request *request_array=NULL;

static void sig_map_init(){
    map=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
    for(int i=0;i<mpi_nodes;i++){
        map[i]=(int*)RTmallocZero(out_count[i]*sizeof(int));
    }
    set=(int*)RTmallocZero(my_states*sizeof(int));
    oldid=(int*)RTmallocZero(my_states*sizeof(int));
    status_array=(MPI_Status*)RTmallocZero(2*mpi_nodes*sizeof(MPI_Status));
    request_array=(MPI_Request*)RTmallocZero(2*mpi_nodes*sizeof(MPI_Request));
};

static void id_exchange_all(){
    if (tmp_map==NULL){
        tmp_map=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            tmp_map[i]=(int*)RTmallocZero(in_count[i]*sizeof(int));
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    for(uint32_t i=0;i<my_states;i++){
        for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
            int tag=SetGetTag(set[i]);
            tmp_map[in_seg[j]][in_edge[j]]=tag;
        }
    }
    for(uint32_t i=0;i<my_states;i++){
        oldid[i]=SetGetTag(set[i]);
        if (oldid[i]<0) Abort("bad ID");
    }
    for(int i=0;i<mpi_nodes;i++){
        MPI_Isend(tmp_map[i],in_count[i],MPI_INT,i,37,MPI_COMM_WORLD,request_array+i);
        MPI_Irecv(map[i],out_count[i],MPI_INT,i,37,MPI_COMM_WORLD,request_array+mpi_nodes+i);
    }
    MPI_Waitall(2*mpi_nodes,request_array,status_array);
    MPI_Barrier(MPI_COMM_WORLD);
}



/********** creating a backward propagation structure *********/

static int inv_send=0;
static int inv_recv=0;
static int inv_wait=0;
static uint32_t **inv_src;
static int *inv_recv_buffer=NULL;
static int *inv_send_offset=NULL;
static int **inv_send_buffer=NULL;


static void inv_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int i;
    int len;

    MPI_Get_count(recv_status,MPI_INT,&len);
    if(len==0){
        inv_wait--;
    } else {
        len=len>>1;
            //Warning(info,"%d: registering %d forwarding states",mpi_me,len);
        for(i=0;i<len;i++){
            inv_src[recv_status->MPI_SOURCE][inv_recv_buffer[i+i]]=inv_recv_buffer[i+i+1];
            inv_recv++;	
        }
    }
    event_Irecv(mpi_queue,inv_recv_buffer,INV_BUFFER_SIZE,MPI_INT,
                MPI_ANY_SOURCE,INV_TAG,MPI_COMM_WORLD,inv_callback,NULL);
}

static void inv_init(uint32_t **target){
    if (inv_recv_buffer==NULL){
        inv_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
        inv_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            if (i==mpi_me) continue;
            inv_send_buffer[i]=(int*)RTmallocZero(INV_BUFFER_SIZE*sizeof(int));
        }
        inv_recv_buffer=(int*)RTmallocZero(INV_BUFFER_SIZE*sizeof(int));
        event_Irecv(mpi_queue,inv_recv_buffer,INV_BUFFER_SIZE,MPI_INT,
                    MPI_ANY_SOURCE,INV_TAG,MPI_COMM_WORLD,inv_callback,NULL);
    }
    inv_src=target;
    for(int i=0;i<mpi_nodes;i++){
        inv_send_offset[i]=0;
        for(uint32_t j=0;j<in_count[i];j++){
            inv_src[i][j]=INVALID_STATE;
        }
    }
    inv_send=0;
    inv_recv=0;
    inv_wait=mpi_nodes-1;
    event_barrier_wait(barrier);
}

static void inv_set(int dst_seg,int edge,int src_state){
    if(dst_seg==mpi_me) {
        inv_src[mpi_me][edge]=src_state;
    } else {
        inv_send++;
        if (inv_send_offset[dst_seg]+2>INV_BUFFER_SIZE){
            event_Send(mpi_queue,inv_send_buffer[dst_seg],inv_send_offset[dst_seg],MPI_INT,
                       dst_seg,INV_TAG,MPI_COMM_WORLD);
            inv_send_offset[dst_seg]=0;
        }
        inv_send_buffer[dst_seg][inv_send_offset[dst_seg]]=edge;
        inv_send_buffer[dst_seg][inv_send_offset[dst_seg]+1]=src_state;
        inv_send_offset[dst_seg]+=2;
    }
}

static void inv_fini(){
    for(int i=0;i<mpi_nodes;i++){
        if(mpi_me==i) continue;
        if (inv_send_offset[i]!=0) {
            event_Send(mpi_queue,inv_send_buffer[i],inv_send_offset[i],
                       MPI_INT,i,INV_TAG,MPI_COMM_WORLD);
            inv_send_offset[i]=0;
        }
        event_Send(mpi_queue,NULL,0,MPI_INT,i,INV_TAG,MPI_COMM_WORLD);
    }
    Warning(infoLong,"%d: submission finished",mpi_me);
    event_while(mpi_queue,&inv_wait);
    event_barrier_wait(barrier);
    Debug("backpropagation setup: %d sent %d received",inv_send,inv_recv);
}

/********** Edge Signature forwarding  *********/

static int sig_recv_size=0;
static int *sig_recv_buffer=NULL;
static int *sig_send_offset=NULL;
static int **sig_send_buffer=NULL;
static int sig_expect=0;

static void synch_set_tag(int set);

static void sig_request_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int i;
    int len;
    int item_len;
    int offset;
    int count;
    int set;


    MPI_Get_count(recv_status,MPI_INT,&len);
    if (len==0){
        sig_expect--;
    } else {
        for(offset=0;offset<len;offset+=item_len){
            item_len=sig_recv_buffer[offset];
            count=(item_len-2)>>1;
            set=EMPTY_SET;
            for(i=0;i<count;i++){
                set=SetInsert(set,sig_recv_buffer[offset+2+2*i],sig_recv_buffer[offset+3+2*i]);
            }
            if(SetGetTag(set)==SET_TAG_UNDEF){
                SetSetTag(set,SET_TAG_ASKED);
                synch_set_tag(set);
            }
            map[recv_status->MPI_SOURCE][sig_recv_buffer[offset+1]]=set;
        }
    }
    event_Irecv(mpi_queue,sig_recv_buffer,sig_recv_size,MPI_INT,
                MPI_ANY_SOURCE,SIG_TAG,MPI_COMM_WORLD,sig_request_callback,NULL);
}

static void sig_init(){
    if (sig_recv_buffer==NULL) {
        sig_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
        sig_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            if (i==mpi_me) continue;
            sig_send_buffer[i]=(int*)RTmallocZero(SIG_BUFFER_SIZE*sizeof(int));
        }
        sig_recv_size=2*max_fanout+4;
        if (SIG_BUFFER_SIZE > sig_recv_size) sig_recv_size=SIG_BUFFER_SIZE;
        sig_recv_buffer=(int*)RTmallocZero(sig_recv_size*sizeof(int));
        event_Irecv(mpi_queue,sig_recv_buffer,sig_recv_size,MPI_INT,
                    MPI_ANY_SOURCE,SIG_TAG,MPI_COMM_WORLD,sig_request_callback,NULL);
    }
    sig_expect=mpi_nodes-1;
    event_barrier_wait(barrier);
}

static void sig_set(int dst_seg,int edge,int sig){
    if(dst_seg==mpi_me) {
        map[mpi_me][edge]=sig;
        return;
    }
    uint32_t len=2*SetGetSize(sig)+2;
    if((sig_send_offset[dst_seg]+len)>SIG_BUFFER_SIZE) {
        event_Send(mpi_queue,sig_send_buffer[dst_seg],sig_send_offset[dst_seg],
                                MPI_INT,dst_seg,SIG_TAG,MPI_COMM_WORLD);
            sig_send_offset[dst_seg]=0;
    }
    if(len>SIG_BUFFER_SIZE) {
        Debug("using special send");
        int *buffer=(int*)RTmalloc(len*sizeof(int));
        buffer[0]=len;
        buffer[1]=edge;
        SetGetSet(sig,buffer+2);
        event_Send(mpi_queue,buffer,len,
                   MPI_INT,dst_seg,SIG_TAG,MPI_COMM_WORLD);
        RTfree(buffer);
    }
    int ofs=sig_send_offset[dst_seg];
    sig_send_offset[dst_seg]+=len;
    sig_send_buffer[dst_seg][ofs]=len;
    sig_send_buffer[dst_seg][ofs+1]=edge;
    SetGetSet(sig,(sig_send_buffer[dst_seg])+ofs+2);
}

static void sig_fini(){
    //Debug("em finalizing");
    for(int i=0;i<mpi_nodes;i++){
        if (i==mpi_me) continue;
        if (sig_send_offset[i]!=0) {
            event_Send(mpi_queue,sig_send_buffer[i],sig_send_offset[i],
                        MPI_INT,i,SIG_TAG,MPI_COMM_WORLD);
            sig_send_offset[i]=0;
        }
        event_Send(mpi_queue,NULL,0,MPI_INT,i,SIG_TAG,MPI_COMM_WORLD); // signal last entry.
    }
    event_while(mpi_queue,&sig_expect);
    event_barrier_wait(barrier);
}


/****************************************/

static uint32_t tau;

/********** Edge ID forwarding  *********/

static int *eid_recv_buffer=NULL;
static int *eid_send_offset=NULL;
static int **eid_send_buffer=NULL;
static int eid_expect=0;

static void eid_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int i;
    int len;

    MPI_Get_count(recv_status,MPI_INT,&len);
    if (len==0) eid_expect--;
    len=len>>1;
    for(i=0;i<len;i++){
        map[recv_status->MPI_SOURCE][eid_recv_buffer[i+i]]=eid_recv_buffer[i+i+1];
    }
    event_Irecv(mpi_queue,eid_recv_buffer,EID_BUFFER_SIZE,MPI_INT,
                MPI_ANY_SOURCE,EID_TAG,MPI_COMM_WORLD,eid_callback,NULL);
}

static void eid_init(){
    if (eid_recv_buffer==NULL) {
        eid_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
        eid_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            if (i==mpi_me) continue;
            eid_send_buffer[i]=(int*)RTmallocZero(EID_BUFFER_SIZE*sizeof(int));
        }
        eid_recv_buffer=(int*)RTmallocZero(EID_BUFFER_SIZE*sizeof(int));
        event_Irecv(mpi_queue,eid_recv_buffer,EID_BUFFER_SIZE,MPI_INT,
                    MPI_ANY_SOURCE,EID_TAG,MPI_COMM_WORLD,eid_callback,NULL);
    }
    eid_expect=mpi_nodes-1;
    event_barrier_wait(barrier);
}

static void eid_set(int dst_seg,int edge,int id){
    if(dst_seg==mpi_me) {
        map[mpi_me][edge]=id;
    } else {
        eid_send_buffer[dst_seg][eid_send_offset[dst_seg]]=edge;
        eid_send_buffer[dst_seg][eid_send_offset[dst_seg]+1]=id;
        eid_send_offset[dst_seg]+=2;
        if(eid_send_offset[dst_seg]==EID_BUFFER_SIZE){
            event_Send(mpi_queue,eid_send_buffer[dst_seg],EID_BUFFER_SIZE,MPI_INT,
                     dst_seg,EID_TAG,MPI_COMM_WORLD);
            eid_send_offset[dst_seg]=0;
        }
    }
}

static void eid_fini(){
    for(int i=0;i<mpi_nodes;i++){
        if (i==mpi_me) continue;
        if (eid_send_offset[i]!=0) {
                event_Send(mpi_queue,eid_send_buffer[i],eid_send_offset[i],MPI_INT,i,EID_TAG,MPI_COMM_WORLD);
                eid_send_offset[i]=0;
        }
        event_Send(mpi_queue,NULL,0,MPI_INT,i,EID_TAG,MPI_COMM_WORLD); // signal last entry.
    }
    event_while(mpi_queue,&eid_expect);
}

/******** global sig2id mapping *********/

static int *synch_receive_buffer=NULL;
static int *synch_reply_buffer=NULL;
static int synch_next=0;
static int synch_pending=0;
static int *synch_send_offset=NULL;
static int **synch_send_buffer=NULL;

static int synch_get_tag(int set){
    int id=SetGetTag(set);
    if (id<0) {
        id=synch_next*mpi_nodes+mpi_me;
        synch_next++;
        SetSetTag(set,id);
    }
    return id;
}

static void synch_set_tag(int s){
    uint32_t hash=SetGetHash(s)%mpi_nodes;
    if (hash==(uint32_t)mpi_me){
        (void)synch_get_tag(s);
        return;
    }
    uint32_t len=2*SetGetSize(s)+2;
    if(((int)len)>synch_buffer_size) {
        Debug("using special send");
        int *buffer=(int*)RTmalloc(len*sizeof(int));
        buffer[0]=len;
        buffer[1]=s;
        SetGetSet(s,buffer+2);
        event_Send(mpi_queue,buffer,len,
                   MPI_INT,hash,SYNCH_ASK_TAG,MPI_COMM_WORLD);
        RTfree(buffer);
    } else {
        if((int)(synch_send_offset[hash]+len)>synch_buffer_size) {
            event_Send(mpi_queue,synch_send_buffer[hash],synch_send_offset[hash],
                MPI_INT,hash,SYNCH_ASK_TAG,MPI_COMM_WORLD);
            synch_send_offset[hash]=0;
        }
        int ofs=synch_send_offset[hash];
        synch_send_offset[hash]+=len;
        synch_send_buffer[hash][ofs]=len;
        synch_send_buffer[hash][ofs+1]=s;
        SetGetSet(s,(synch_send_buffer[hash])+ofs+2);
    }
    synch_pending++;
}

static void synch_barrier(){
    for(int i=0;i<mpi_nodes;i++) {
        if (i==mpi_me) continue;
        if (synch_send_offset[i]>0){
            event_Send(mpi_queue,synch_send_buffer[i],synch_send_offset[i],MPI_INT,i,SYNCH_ASK_TAG,MPI_COMM_WORLD);
            synch_send_offset[i]=0;
        }
    }
    //if (RTverbosity>1) Warning(info,"submitted all requests");
    event_while(mpi_queue,&synch_pending); // get my answers
    event_barrier_wait(barrier); // serve others
    //if (RTverbosity>1) Warning(info,"share is %d",synch_next);
}

static void synch_request_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int len;
    int item;
    int item_len;
    int offset;
    int count;
    int set;
    int id;
    int i;
    MPI_Get_count(recv_status,MPI_INT,&len);
    item=0;
    for(offset=0;offset<len;offset+=item_len){
        item_len=synch_receive_buffer[offset];
        count=(item_len-2)>>1;
        set=EMPTY_SET;
        for(i=0;i<count;i++){
            set=SetInsert(set,synch_receive_buffer[offset+2+2*i],synch_receive_buffer[offset+3+2*i]);
        }
        id=synch_get_tag(set);
        synch_receive_buffer[2*item]=synch_receive_buffer[offset+1];
        synch_receive_buffer[2*item+1]=id;
        item++;
    }
    event_Send(mpi_queue,synch_receive_buffer,item*2,MPI_INT,recv_status->MPI_SOURCE,SYNCH_REPLY_TAG,MPI_COMM_WORLD);
    event_Irecv(mpi_queue,synch_receive_buffer,synch_receive_size,MPI_INT,
                MPI_ANY_SOURCE,SYNCH_ASK_TAG,MPI_COMM_WORLD,synch_request_callback,NULL);
}

static void synch_reply_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int len,i;
    MPI_Get_count(recv_status,MPI_INT,&len);
    //Warning(1,"%d: got synch answer with %d entries",mpi_me,len>>1);
    for(i=0;i<len;i+=2){
        SetSetTag(synch_reply_buffer[i],synch_reply_buffer[i+1]);
        synch_pending--;
    }
    event_Irecv(mpi_queue,synch_reply_buffer,synch_buffer_size,MPI_INT,
                MPI_ANY_SOURCE,SYNCH_REPLY_TAG,MPI_COMM_WORLD,synch_reply_callback,NULL);
}


static void synch_reset(){
    if (synch_receive_buffer==NULL){
        synch_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
        synch_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            if (i==mpi_me) continue;
            synch_send_buffer[i]=(int*)RTmallocZero(synch_buffer_size*sizeof(int));
        }
        synch_receive_size=2*max_fanout+4;
        if (synch_buffer_size > synch_receive_size) {
            synch_receive_size = synch_buffer_size;
        } else {
            if (mpi_me==0) Warning(info,"adjusting synch receive buffer for worst case signature");
        }
        synch_receive_buffer=(int*)RTmallocZero(synch_receive_size*sizeof(int));
        event_Irecv(mpi_queue,synch_receive_buffer,synch_receive_size,MPI_INT,
                    MPI_ANY_SOURCE,SYNCH_ASK_TAG,MPI_COMM_WORLD,synch_request_callback,NULL);
        synch_reply_buffer=(int*)RTmallocZero(synch_buffer_size*sizeof(int));
        event_Irecv(mpi_queue,synch_reply_buffer,synch_buffer_size,MPI_INT,
                    MPI_ANY_SOURCE,SYNCH_REPLY_TAG,MPI_COMM_WORLD,synch_reply_callback,NULL);
    }
    synch_pending=0;
    synch_next=0;
    for(int i=0;i<mpi_nodes;i++) synch_send_offset[i]=0;
    event_barrier_wait(barrier);
}

/******************* branching ************************/

static uint32_t fwd_todo=0;
static int *fwd_todo_list=NULL;
static int *fwd_recv_buffer=NULL;
static int *fwd_send_offset=NULL;
static int **fwd_send_buffer=NULL;
static int *new_set,*send_set;
static int new_count,*new_list;
static int fwd_wait;

void add_pair(int state,int label,int dstid){
    int s1=set[state];
    int s2=SetInsert(s1,label,dstid);
    if(s1!=s2){
        set[state]=s2;
        if(new_set[state]==EMPTY_SET){
            new_list[new_count]=state;
            new_count++;
        }
        new_set[state]=SetInsert(new_set[state],label,dstid);
    }
}

void fwd_pair(int seg,int state,int label,int dstid){
    if (seg==mpi_me) {
        add_pair(state,label,dstid);
        return;
    }
    if (fwd_send_offset[seg]+3>FWD_BUFFER_SIZE){
        event_Send(mpi_queue,fwd_send_buffer[seg],fwd_send_offset[seg],MPI_INT,
                   seg,FWD_TAG,MPI_COMM_WORLD);
        fwd_send_offset[seg]=0;
    }
    fwd_send_buffer[seg][fwd_send_offset[seg]]=state;
    fwd_send_buffer[seg][fwd_send_offset[seg]+1]=label;
    fwd_send_buffer[seg][fwd_send_offset[seg]+2]=dstid;
    fwd_send_offset[seg]+=3;
}

void fwd_barrier(){
    for(int i=0;i<mpi_nodes;i++){
        if (mpi_me==i) continue;
        if (fwd_send_offset[i]!=0) {
            event_Send(mpi_queue,fwd_send_buffer[i],fwd_send_offset[i],
                       MPI_INT,i,FWD_TAG,MPI_COMM_WORLD);
            fwd_send_offset[i]=0;
        }
        event_Send(mpi_queue,NULL,0,MPI_INT,i,FWD_TAG,MPI_COMM_WORLD);
    }
    event_while(mpi_queue,&fwd_wait);
    fwd_wait=mpi_nodes-1;
    event_barrier_wait(barrier);
}

void fwd_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int i;
    int len;

    MPI_Get_count(recv_status,MPI_INT,&len);
    if (len) {
        for(i=0;i<len;i+=3){
            int state=fwd_recv_buffer[i];
            int label=fwd_recv_buffer[i+1];
            int dstid=fwd_recv_buffer[i+2];
            add_pair(state,label,dstid);
        }
    } else {
        fwd_wait--;
    }
    event_Irecv(mpi_queue,fwd_recv_buffer,FWD_BUFFER_SIZE,MPI_INT,
                MPI_ANY_SOURCE,FWD_TAG,MPI_COMM_WORLD,fwd_callback,NULL);
}

void fwd_alloc(){
    fwd_todo_list=(int*)RTmallocZero(my_states*sizeof(int));
    new_list=(int*)RTmallocZero(my_states*sizeof(int));
    fwd_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
    fwd_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
    send_set=(int*)RTmallocZero(my_states*sizeof(int));
    new_set=(int*)RTmallocZero(my_states*sizeof(int));
    fwd_recv_buffer=(int*)RTmallocZero(FWD_BUFFER_SIZE*sizeof(int));
    for(int i=0;i<mpi_nodes;i++){
        if (mpi_me==i) continue;
        fwd_send_buffer[i]=(int*)RTmallocZero(FWD_BUFFER_SIZE*sizeof(int));
        fwd_send_offset[i]=0;
    }
    event_Irecv(mpi_queue,fwd_recv_buffer,FWD_BUFFER_SIZE,MPI_INT,
                MPI_ANY_SOURCE,FWD_TAG,MPI_COMM_WORLD,fwd_callback,NULL);
}

void fwd_init(){
    fwd_todo=0;
    for(uint32_t i=0;i<my_states;i++) {
        new_set[i]=EMPTY_SET;
        send_set[i]=set[i];
        if (set[i]!=EMPTY_SET){
            fwd_todo_list[fwd_todo]=i;
            fwd_todo++;
        }
    }
    fwd_wait=mpi_nodes-1;
    event_barrier_wait(barrier);
}

sig_id_t *sigmin_set_branching(event_queue_t queue,seg_lts_t lts,uint32_t tau_arg){
    mpi_queue=queue;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
    barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);
    tau=tau_arg;
    map_lts(lts);
    sig_map_init();
    event_barrier_wait(barrier);
    int iter,oldcount,newcount;
    int total_new_set_count;
    int *tmp;

    Debug("allocating maps");
    in_state=(uint32_t**)RTmallocZero(mpi_nodes*sizeof(uint32_t*));
    for(int i=0;i<mpi_nodes;i++){
        in_state[i]=(uint32_t*)RTmallocZero(in_count[i]*sizeof(s_idx_t));
    }
    iter=0;
    oldcount=0;
    newcount=1;
    for(uint32_t i=0;i<my_states;i++) oldid[i]=0;
    fwd_alloc();
    event_barrier_wait(barrier);
    while(oldcount!=newcount){
        if (iter) id_exchange_all();
        iter++;
        oldcount=newcount;
        SetClear(SET_TAG_UNDEF);
        if (mpi_me==0) {
            Warning(infoLong,"computing initial signatures and marking invisible steps");
        }
        inv_init(in_state);
        event_barrier_wait(barrier);
        for(uint32_t i=0;i<my_states;i++){
            int s=EMPTY_SET;
            for(uint32_t j=dest_begin[i];j<dest_begin[i+1];j++){
                uint32_t seg=dest_seg[j];
                if (dest_label[j]==tau && oldid[i]==map[dest_seg[j]][dest_edge[j]]){
                    inv_set(seg,dest_edge[j],i);
                } else {
                    s=SetInsert(s,dest_label[j],map[dest_seg[j]][dest_edge[j]]);
                }
            }
            set[i]=s;
        }
        inv_fini();
        event_barrier_wait(barrier);
        if (mpi_me==0) Debug("starting forwarding phase");
        fwd_init();
        event_barrier_wait(barrier);
        total_new_set_count=1;
        while(total_new_set_count>0){
            new_count=0;
            event_barrier_wait(barrier);
            for(uint32_t i=0;i<fwd_todo;i++){
                int s=fwd_todo_list[i];
                while(send_set[s]!=EMPTY_SET){
                    int l=SetGetLabel(send_set[s]);
                    int d=SetGetDest(send_set[s]);
                    send_set[s]=SetGetParent(send_set[s]);
                    for(uint32_t j=in_begin[s];j<in_begin[s+1];j++){
                        uint32_t to=in_seg[j];
                        uint32_t src=in_state[to][in_edge[j]];
                        if (src==INVALID_STATE) continue;
                        fwd_pair(to,src,l,d);
                    }
                }
            }
            fwd_barrier();
            event_barrier_wait(barrier);
            fwd_todo=new_count;
            new_count=0;
            tmp=new_list; new_list=fwd_todo_list; fwd_todo_list=tmp;
            tmp=send_set; send_set=new_set; new_set=tmp;
            MPI_Allreduce(&fwd_todo,&total_new_set_count,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
            if (mpi_me==0) Debug("sub iteration yielded %d modified sigs.",total_new_set_count);
        }
        event_barrier_wait(barrier);
        if (mpi_me==0) Warning(infoLong,"%d: exchanging signatures",mpi_me);
        synch_reset();
        event_barrier_wait(barrier);
        for(uint32_t i=0;i<my_states;i++){
            if(SetGetTag(set[i])==SET_TAG_UNDEF){
                SetSetTag(set[i],SET_TAG_ASKED);
                synch_set_tag(set[i]);
            }
        }
        synch_barrier();
        event_barrier_wait(barrier);
        MPI_Allreduce(&synch_next,&newcount,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
        if (mpi_me==0) Warning(info,"count of iteration %d is %d",iter,newcount);
    }
    for(uint32_t i=0;i<my_states;i++){
        oldid[i]=SetGetTag(set[i]);
    }
    if ((uint32_t)mpi_me==0) {
        Warning(info,"reduced state space has %d states",newcount);
    }
    SetFree();
    if (sizeof(sig_id_t)!=sizeof(int)){
        Abort("size mismatch sig_id_t and int");
    }
    return (sig_id_t*)oldid;
}

sig_id_t *sigmin_set_strong(event_queue_t queue,seg_lts_t lts){
    mpi_queue=queue;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
    barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);
    map_lts(lts);
    sig_map_init();
    event_barrier_wait(barrier);
    int iter=0;
    int oldcount=0;
    int newcount=1;
    while(oldcount!=newcount){
        if (iter) {
            if (mpi_me==0) Debug("forwarding signatures");
            id_exchange_all();
        }
        iter++;
        oldcount=newcount;
        SetClear(SET_TAG_UNDEF);
        if (mpi_me==0) Debug("computing signatures");
        for(uint32_t i=0;i<my_states;i++){
            int s=EMPTY_SET;
            for(uint32_t j=dest_begin[i];j<dest_begin[i+1];j++){
                s=SetInsert(s,dest_label[j],map[dest_seg[j]][dest_edge[j]]);
            }
            set[i]=s;
        }
        event_barrier_wait(barrier);
        if (mpi_me==0) Debug("synchronizing signatures");
        synch_reset();
        for(uint32_t i=0;i<my_states;i++){
            if(SetGetTag(set[i])==SET_TAG_UNDEF){
                SetSetTag(set[i],SET_TAG_ASKED);
                synch_set_tag(set[i]);
            }
        }
        synch_barrier();
        MPI_Allreduce(&synch_next,&newcount,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
        if (mpi_me==0) Warning(info,"count of iteration %d is %d",iter,newcount);
    }
    for(uint32_t i=0;i<my_states;i++){
        oldid[i]=SetGetTag(set[i]);
    }
    SetFree();
    if ((uint32_t)mpi_me==0) {
        Warning(info,"reduced state space has %d states",newcount);
    }
    if (sizeof(sig_id_t)!=sizeof(int)){
        Abort("size mismatch sig_id_t and int");
    }
    return (sig_id_t*)oldid;
}

/********** edge marking  *********/

static int *em_recv_buffer=NULL;
static int *em_send_offset=NULL;
static int **em_send_buffer=NULL;
static int em_expect=0;
static bitset_t* em_marking=NULL;


static void em_callback(void* context,MPI_Status *recv_status){
    (void)context;
    int i;
    int len;

    MPI_Get_count(recv_status,MPI_INT,&len);
    if (len==0) em_expect--;
    for(i=0;i<len;i++){
        bitset_set(em_marking[recv_status->MPI_SOURCE],em_recv_buffer[i]);	
    }
    event_Irecv(mpi_queue,em_recv_buffer,EM_BUFFER_SIZE,MPI_INT,
                MPI_ANY_SOURCE,EM_TAG,MPI_COMM_WORLD,em_callback,NULL);
}

static void em_init(bitset_t*marking){
    if (em_recv_buffer==NULL) {
        em_send_offset=(int*)RTmallocZero(mpi_nodes*sizeof(int));
        em_send_buffer=(int**)RTmallocZero(mpi_nodes*sizeof(int*));
        for(int i=0;i<mpi_nodes;i++){
            if (i==mpi_me) continue;
            em_send_buffer[i]=(int*)RTmallocZero(EM_BUFFER_SIZE*sizeof(int));
        }
        em_recv_buffer=(int*)RTmallocZero(EM_BUFFER_SIZE*sizeof(int));
        event_Irecv(mpi_queue,em_recv_buffer,EM_BUFFER_SIZE,MPI_INT,
                    MPI_ANY_SOURCE,EM_TAG,MPI_COMM_WORLD,em_callback,NULL);
    }
    em_marking=marking;
    em_expect=mpi_nodes-1;
    event_barrier_wait(barrier);
}

static void em_set(int dst_seg,int edge){
    if(dst_seg==mpi_me) {
        bitset_set(em_marking[mpi_me],edge);
    } else {
        em_send_buffer[dst_seg][em_send_offset[dst_seg]]=edge;
        em_send_offset[dst_seg]++;
        if(em_send_offset[dst_seg]==EM_BUFFER_SIZE){
            event_Send(mpi_queue,em_send_buffer[dst_seg],EM_BUFFER_SIZE,MPI_INT,
                       dst_seg,EM_TAG,MPI_COMM_WORLD);
            em_send_offset[dst_seg]=0;
        }
    }
}

static void em_fini(){
    for(int i=0;i<mpi_nodes;i++){
        if (i==mpi_me) continue;
        if (em_send_offset[i]!=0) {
            event_Send(mpi_queue,em_send_buffer[i],em_send_offset[i],MPI_INT,i,EM_TAG,MPI_COMM_WORLD);
            em_send_offset[i]=0;
        }
        event_Send(mpi_queue,NULL,0,MPI_INT,i,EM_TAG,MPI_COMM_WORLD); // signal last entry.
    }
    event_while(mpi_queue,&em_expect);
    // the last entry signalling causes this function to behave as a barrier.
}


static int level_count=0;
static uint32_t* level_begin=NULL;
static uint32_t* level_member=NULL;
static bitset_t* level_visited=NULL;

static void build_tau_levels(){
    rt_timer_t timer=NULL;
    if(mpi_me==0){
        timer=RTcreateTimer();
        RTstartTimer(timer);
        Debug("computing tau levels");
    }
    event_barrier_wait(barrier);
    level_visited=(bitset_t*)RTmallocZero(mpi_nodes*sizeof(bitset_t));
    bitset_t level_done=bitset_create(128,128);
    for(int i=0;i<mpi_nodes;i++) level_visited[i]=bitset_create_shared(level_done);
    level_begin=(uint32_t*)RTmallocZero((my_states+1)*sizeof(uint32_t));
    level_begin[0]=0;
    level_member=(uint32_t*)RTmallocZero((my_states)*sizeof(uint32_t));
    for(;;){
        //set_label("level %d (%d/%d)",level_count,mpi_me,mpi_nodes);
        Debug("scanning for states without unmarked tau edges on level %d",level_count);
        level_count++;
        level_begin[level_count]=level_begin[level_count-1];
        long long int local_found=0;
        for(uint32_t i=0;i<my_states;i++){
            Debug("state %u",i);
            if (bitset_test(level_done,i)) continue;
            int has_unmarked=0;
            Debug("scanning %u [%d-%d]",i,dest_begin[i],dest_begin[i+1]);
            for(uint32_t j=dest_begin[i];j<dest_begin[i+1];j++){
                Debug("%u --%d-> %u:%u",i,dest_label[j],dest_seg[j],dest_edge[j]);
                if (dest_label[j]==tau && !bitset_test(level_visited[dest_seg[j]],dest_edge[j])) {
                    has_unmarked=1;
                    //Debug("found unmarked edge");
                    //break;
                }
            }
            if (has_unmarked) continue;
            Debug("no unmarked edges");
            bitset_set(level_done,i);
            local_found++;
            level_member[level_begin[level_count]]=i;
            level_begin[level_count]++;
        }
        em_init(level_visited);
        for(uint32_t i=level_begin[level_count-1];i<level_begin[level_count];i++){
            uint32_t s=level_member[i];
            for(uint32_t j=in_begin[s];j<in_begin[s+1];j++){
                if (in_label[j]==tau) {
                    em_set(in_seg[j],in_edge[j]);
                }
            }
        }
        em_fini();
        Debug("%lld new %d/%d states handled",local_found,level_begin[level_count],my_states);
        long long int global_found;
        MPI_Allreduce(&local_found,&global_found,1,MPI_LONG_LONG_INT,MPI_SUM,MPI_COMM_WORLD);
        if (global_found==0) {
            level_count--;
            break;
        }
        if (mpi_me==0){
            Warning(infoLong,"tau level %d has %lld states",level_count,global_found);
        }
    }
    if (level_begin[level_count]==my_states) {
        if(mpi_me==0) Warning(info,"The LTS has %d tau levels",level_count);
    } else {
        Abort("the LTS is not tau cycle free");
    }
    // the following line causes crashes....
    //level_begin=(uint32_t*)realloc(level_begin,(level_count+1)*sizeof(uint32_t));
    for(int i=0;i<mpi_nodes;i++) bitset_destroy(level_visited[i]);
    bitset_destroy(level_done);
    RTfree(level_visited);
    level_visited=NULL;
    event_barrier_wait(barrier);
    if(mpi_me==0){
        RTstopTimer(timer);
        RTprintTimer(info,timer,"building the tau levels took");
        RTdeleteTimer(timer);
    }
}

sig_id_t *sigmin_set_strong_tcf(event_queue_t queue,seg_lts_t lts,uint32_t tau_arg){
    mpi_queue=queue;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
    barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);
    tau=tau_arg;
    map_lts(lts);
    sig_map_init();
    event_barrier_wait(barrier);
    build_tau_levels();
    rt_timer_t timer=NULL;
    if(mpi_me==0){
        timer=RTcreateTimer();
        Debug("starting inductive reduction");
        RTstartTimer(timer);
    }
    event_barrier_wait(barrier);
    int iter=0;
    int oldcount=0;
    int newcount=1;
    while(oldcount!=newcount){
        if(iter){
            eid_init();
            for(uint32_t i=0;i<my_states;i++){
                oldid[i]=SetGetTag(set[i]);
                if (oldid[i]<0) Abort("bad ID");
                for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
                    if(in_label[j]!=tau) eid_set(in_seg[j],in_edge[j],oldid[i]);
                }
            }
            eid_fini();
        }
        MPI_Barrier(MPI_COMM_WORLD);
        if (mpi_me==0) Debug("main loop");
        MPI_Barrier(MPI_COMM_WORLD);
        iter++;
        oldcount=newcount;
        SetClear(SET_TAG_UNDEF);
        synch_reset();
        MPI_Barrier(MPI_COMM_WORLD);
        for(int L=0;L<level_count;L++){
            event_barrier_wait(barrier);
            //if (mpi_me==0) Debug("processing level %d",L);
            for(uint32_t i=level_begin[L];i<level_begin[L+1];i++){
                int sig=EMPTY_SET;
                int s=level_member[i];
                for(uint32_t j=dest_begin[s];j<dest_begin[s+1];j++){
                    sig=SetInsert(sig,dest_label[j],map[dest_seg[j]][dest_edge[j]]);
                }
                set[s]=sig;
                if(SetGetTag(sig)==SET_TAG_UNDEF){
                    SetSetTag(sig,SET_TAG_ASKED);
                    synch_set_tag(sig);
                }
            }
            synch_barrier();
            MPI_Barrier(MPI_COMM_WORLD);
            eid_init();
            for(uint32_t k=level_begin[L];k<level_begin[L+1];k++){
                int i=level_member[k];
                int id=SetGetTag(set[i]);
                if (id<0) Abort("bad ID");
                for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
                    if (in_label[j]==tau) eid_set(in_seg[j],in_edge[j],id);
                }
            }
            eid_fini();
            event_barrier_wait(barrier);
        }
        MPI_Allreduce(&synch_next,&newcount,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
        if (mpi_me==0) Warning(info,"count of iteration %d is %d",iter,newcount);
    }
    for(uint32_t i=0;i<my_states;i++){
        oldid[i]=SetGetTag(set[i]);
        if (oldid[i]<0) Abort("bad ID");
    }
    SetFree();
    if(mpi_me==0){
        RTstopTimer(timer);
        RTprintTimer(info,timer,"cycle free iterations took");
        RTdeleteTimer(timer);
    }
    if ((uint32_t)mpi_me==0) {
        Warning(info,"reduced state space has %d states",newcount);
    }
    if (sizeof(sig_id_t)!=sizeof(int)){
        Abort("size mismatch sig_id_t and int");
    }
    return (sig_id_t*)oldid;
}

sig_id_t *sigmin_set_branching_tcf(event_queue_t queue,seg_lts_t lts,uint32_t tau_arg){
    mpi_queue=queue;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
    barrier=event_barrier_create(mpi_queue,MPI_COMM_WORLD,BARRIER_TAG);
    tau=tau_arg;
    map_lts(lts);
    sig_map_init();
    event_barrier_wait(barrier);
    build_tau_levels();
    rt_timer_t timer=NULL;
    if(mpi_me==0){
        timer=RTcreateTimer();
        Debug("starting inductive reduction");
        RTstartTimer(timer);
    }
    event_barrier_wait(barrier);
    int iter=0;
    int oldcount=0;
    int newcount=1;
    while(oldcount!=newcount){
        if(iter){
            eid_init();
            for(uint32_t i=0;i<my_states;i++){
                oldid[i]=SetGetTag(set[i]);
                if (oldid[i]<0) Abort("bad ID");
                for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
                    if(in_label[j]!=tau) eid_set(in_seg[j],in_edge[j],oldid[i]);
                }
            }
            eid_fini();
        }
        MPI_Barrier(MPI_COMM_WORLD);
        if (mpi_me==0) Debug("main loop");
        MPI_Barrier(MPI_COMM_WORLD);
        iter++;
        oldcount=newcount;
        SetClear(SET_TAG_UNDEF);
        synch_reset();
        MPI_Barrier(MPI_COMM_WORLD);
        for(int L=0;L<level_count;L++){
            event_barrier_wait(barrier);
            for(uint32_t i=level_begin[L];i<level_begin[L+1];i++){
                int sig=EMPTY_SET;
                int s=level_member[i];
                for(uint32_t j=dest_begin[s];j<dest_begin[s+1];j++){
                    if (dest_label[j]==tau) {
                        int dest_sig=map[dest_seg[j]][dest_edge[j]];
                        if (SetGetLabel(dest_sig)!=OLD_ID_LABEL){
                            Abort("bad assumption");
                        }
                        //int dest_old=SetGetDest(dest_sig);
                        sig=SetInsert(sig,dest_label[j],SetGetTag(dest_sig));
                    } else {
                        sig=SetInsert(sig,dest_label[j],map[dest_seg[j]][dest_edge[j]]);
                    }
                }
                for(uint32_t j=dest_begin[s];j<dest_begin[s+1];j++){
                    if (dest_label[j]==tau) {
                        int dest_sig=map[dest_seg[j]][dest_edge[j]];
                        if (SetGetLabel(dest_sig)!=OLD_ID_LABEL){
                            Abort("bad assumption");
                        }
                        int dest_old=SetGetDest(dest_sig);
                        int dest_new=SetGetTag(dest_sig);
                        dest_sig=SetGetParent(dest_sig);
                        if (dest_old==oldid[s]){
                            int test_sig=SetInsert(dest_sig,tau,dest_new);
                            if (test_sig==SetUnion(test_sig,sig)) {
                                sig=dest_sig;
                                break;
                            }
                        }
                    }
                }
                sig=SetInsert(sig,OLD_ID_LABEL,oldid[s]);
                set[s]=sig;
                if(SetGetTag(sig)==SET_TAG_UNDEF){
                    SetSetTag(sig,SET_TAG_ASKED);
                    synch_set_tag(sig);
                }
            }
            Debug("propagating signatures");
            sig_init();
            for(uint32_t k=level_begin[L];k<level_begin[L+1];k++){
                int i=level_member[k];
                for(uint32_t j=in_begin[i];j<in_begin[i+1];j++){
                    if (in_label[j]==tau) sig_set(in_seg[j],in_edge[j],set[i]);
                }
            }
            sig_fini();
            synch_barrier();
            MPI_Barrier(MPI_COMM_WORLD);
        }
        MPI_Allreduce(&synch_next,&newcount,1,MPI_INT,MPI_SUM,MPI_COMM_WORLD);
        if (mpi_me==0) Warning(info,"count of iteration %d is %d",iter,newcount);
    }
    for(uint32_t i=0;i<my_states;i++){
        oldid[i]=SetGetTag(set[i]);
        if (oldid[i]<0) Abort("bad ID");
    }
    SetFree();
    if(mpi_me==0){
        RTstopTimer(timer);
        RTprintTimer(info,timer,"cycle free iterations took");
        RTdeleteTimer(timer);
    }
    if ((uint32_t)mpi_me==0) {
        Warning(info,"reduced state space has %d states",newcount);
    }
    if (sizeof(sig_id_t)!=sizeof(int)){
        Abort("size mismatch sig_id_t and int");
    }
    return (sig_id_t*)oldid;
}
