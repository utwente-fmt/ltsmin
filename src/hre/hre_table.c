// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>
#include <string.h>

#include <hre/user.h>
#include <hre/table.h>
#include <hre/stringindex.h>

#define CHUNK_BUFFER_SIZE 65536
#define MAX_CHUNK_SIZE (CHUNK_BUFFER_SIZE - 4)

struct value_table_s {
    string_index_t index;
    hre_msg_t msg;
    hre_context_t ctx;
    int msg_pending;
    int task_pending;
    int count;
    int task;
};

/**
 * Command values:
 * INT_MIN -- -2    = PutAt(2 - value)
 * -1               = Put
 * 0 -- INT_MAX     = Get(value)
 */
static void request_action(void* context,hre_msg_t msg){
    value_table_t vt=(value_table_t)context;
    int32_t task;
    memcpy(&task,msg->buffer,4);
    if (task < -1) {
        int idx = -2 - task;
        Debug ("receive [%s at %d]",msg->buffer+4,idx);
        SIputCAt(vt->index,msg->buffer+4,msg->tail-4,idx);
    } else if (task==-1) {
        Debug("receive [%s]",msg->buffer+4);
        int32_t idx=SIputC(vt->index,msg->buffer+4,msg->tail-4);
        Debug("reply (%d) to %d",idx,msg->source);
        if (vt->msg_pending) HREyieldWhile(vt->ctx,&vt->msg_pending);
        vt->msg->target=msg->source;
        memcpy(vt->msg->buffer,&idx,4);
        vt->msg->tail=4;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
    } else {
        Debug("receive (%d)",task);
        int len;
        char *data=SIgetC(vt->index,task,&len);
        Debug("reply [%s] to %d",data,msg->source);
        if (data==NULL) Abort("index %d unknown",task);
        if (len>MAX_CHUNK_SIZE) Abort("chunk length %d exceeds maximum (%d).",len,MAX_CHUNK_SIZE);
        if (vt->msg_pending) HREyieldWhile(vt->ctx,&vt->msg_pending);
        vt->msg->target=msg->source;
        int32_t tmp=-1;
        memcpy(vt->msg->buffer,&tmp,4);
        memcpy(vt->msg->buffer+4,data,len);
        vt->msg->tail=len+4;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
    }
    HREmsgReady(msg);
}

static void answer_action(void* context,hre_msg_t msg){
    value_table_t vt=(value_table_t)context;
    int32_t task;
    memcpy(&task,msg->buffer,4);
    if (task<0) {
        if (vt->task<0) Abort("inconsistency");
        msg->buffer[msg->tail]=0;
        Debug("receive [%s]",msg->buffer+4);
        SIputCAt(vt->index,msg->buffer+4,msg->tail-4,vt->task);
        vt->task_pending=0;
    } else {
        if (vt->task>=0) Abort("inconsistency");
        Debug("receive (%d)",task);
        vt->task=task;
        vt->task_pending=0;
    }
    HREmsgReady(msg);
}

static void destroy(value_table_t vt){
// TODO
    (void)vt;
}

static void put_at_chunk(value_table_t vt,chunk item,value_index_t index){
    SIputCAt(vt->index,item.data,item.len,index);

    if (HREme(vt->ctx)!=0) {
        Debug("validating at owner chunk %s at %u",item.data, index);
        if (vt->msg_pending) {
            Debug("waiting for msg (%x/%d) %s",vt->msg_pending,vt->msg_pending,vt->msg->buffer+4);
            HREyieldWhile(vt->ctx,&vt->msg_pending);
        }
        Debug("preparing message");
        int32_t tmp = -2 - index;
        if (item.len>MAX_CHUNK_SIZE) Abort("chunk length %d exceeds maximum (%d).",item.len,MAX_CHUNK_SIZE);
        memcpy(vt->msg->buffer,&tmp,4);
        memcpy(vt->msg->buffer+4,item.data,item.len);
        vt->msg->tail=item.len+4;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
    }
}

static value_index_t put_chunk(value_table_t vt,chunk item){
    if (HREme(vt->ctx)==0) {
        return SIputC(vt->index,item.data,item.len);
    }
    int res=SIlookupC(vt->index,item.data,item.len);
    if (res==SI_INDEX_FAILED) {
        Debug("looking up chunk %s",item.data);
        if (vt->msg_pending) {
            Debug("waiting for msg (%x/%d) %s",vt->msg_pending,vt->msg_pending,vt->msg->buffer+4);
            HREyieldWhile(vt->ctx,&vt->msg_pending);
        }
        Debug("preparing message");
        vt->task=-1;
        int32_t tmp=-1;
        if (item.len>MAX_CHUNK_SIZE) Abort("chunk length %d exceeds maximum (%d).",item.len,MAX_CHUNK_SIZE);
        memcpy(vt->msg->buffer,&tmp,4);
        memcpy(vt->msg->buffer+4,item.data,item.len);
        vt->msg->tail=item.len+4;
        vt->task_pending=1;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
        Debug("waiting for result");
        HREyieldWhile(vt->ctx,&vt->task_pending);
        res=vt->task;
        SIputCAt(vt->index,item.data,item.len,res);
        Debug("got %d",res);
    }
    return res;
}

static chunk get_chunk(value_table_t vt,value_index_t idx){
    int len;
    char* data=SIgetC(vt->index,idx,&len);
    if (data==NULL) {
        if (HREme(vt->ctx)==0) Abort("chunk %d does not exist",idx);
        Debug("looking up index %d",idx);
        if (vt->msg_pending) HREyieldWhile(vt->ctx,&vt->msg_pending);
        vt->task=idx;
        memcpy(vt->msg->buffer,&idx,4);
        vt->msg->tail=4;
        vt->task_pending=1;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
        HREyieldWhile(vt->ctx,&vt->task_pending);
        data=SIgetC(vt->index,idx,&len);
        data[len]=0;
        Debug("got %s (%d)",data,vt->msg_pending);
    }
    return chunk_ld(len,data);
}


static int get_count(value_table_t vt){
    return SIgetCount(vt->index);
}

value_table_t HREcreateTable(hre_context_t ctx,const char* name){
    if (HREpeers(ctx)==1) return simple_chunk_table_create(ctx,(char*)name);
    value_table_t vt=VTcreateBase((char*)name,sizeof(struct value_table_s));
    VTdestroySet(vt,destroy);
    VTputChunkSet(vt,put_chunk);
    VTputAtChunkSet(vt,put_at_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
    VTiteratorSet(vt,simple_iterator_create);
    vt->index=SIcreate();
    vt->ctx=ctx;
    vt->msg_pending=0;
    uint32_t request_tag=HREactionCreate(ctx,1,CHUNK_BUFFER_SIZE,request_action,vt);
    uint32_t answer_tag=HREactionCreate(ctx,0,CHUNK_BUFFER_SIZE,answer_action,vt);
    vt->msg=HREnewMessage(ctx,CHUNK_BUFFER_SIZE);
    if (HREme(ctx)==0){
        vt->msg->tag=answer_tag;
        vt->msg->comm=0;
    } else {
        vt->msg->tag=request_tag;
        vt->msg->comm=1;
    }
    vt->msg->source=HREme(ctx);
    vt->msg->target=0;
    vt->msg->ready=hre_ready_decr;
    vt->msg->ready_ctx=&vt->msg_pending;
    HREbarrier(ctx);
    return vt;
}

void *HREgreyboxNewmap(void*newmap_context){
    return HREcreateTable (HREglobal(), "<greybox table>");
    (void) newmap_context;
}

int HREgreyboxC2I(void*map,void*data,int len){
    return VTputChunk((value_table_t)map,chunk_ld(len,data));
}

void HREgreyboxCAtI(void*map,void*data,int len,int pos){
    return VTputAtChunk((value_table_t)map,chunk_ld(len,data),pos);
}

void *HREgreyboxI2C(void*map,int idx,int*len){
    chunk c=VTgetChunk((value_table_t)map,idx);
    *len=c.len;
    return c.data;
}

int HREgreyboxCount(void*map){
    return VTgetCount((value_table_t)map);
}

table_iterator_t HREgreyboxIterator (void*map) {
    return VTiterator ((value_table_t)map);
}


struct table_factory_s {
};

table_factory_t HREgreyboxTableFactory() {
    table_factory_t tf = TFcreateBase (0);
    TFnewTableSet (tf, (tf_new_map_t) HREgreyboxNewmap);
    return tf;
}
