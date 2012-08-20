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

static void request_action(void* context,hre_msg_t msg){
    value_table_t vt=(value_table_t)context;
    int32_t task;
    memcpy(&task,msg->buffer,4);
    if (task<0) {
        //Print(infoShort,"receive [%s]",msg->buffer+4);
        int32_t idx=SIputC(vt->index,msg->buffer+4,msg->tail-4);
        //Print(infoShort,"reply (%d) to %d",idx,msg->source);
        if (vt->msg_pending) HREyieldWhile(vt->ctx,&vt->msg_pending);
        vt->msg->target=msg->source;
        memcpy(vt->msg->buffer,&idx,4);
        vt->msg->tail=4;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
    } else {
        //Print(infoShort,"receive (%d)",task);
        int len;
        char *data=SIgetC(vt->index,task,&len);
        //Print(infoShort,"reply [%s] to %d",data,msg->source);
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
        //Print(infoShort,"receive [%s]",msg->buffer+4);
        SIputCAt(vt->index,msg->buffer+4,msg->tail-4,vt->task);
        vt->task_pending=0;
    } else {
        if (vt->task>=0) Abort("inconsistency");
        //Print(infoShort,"receive (%d)",task);
        vt->task=task;
        vt->task_pending=0;
    }
    HREmsgReady(msg);
}

static void destroy(value_table_t vt){
// TODO
    (void)vt;
}

static value_index_t put_chunk(value_table_t vt,chunk item){
    if (HREme(vt->ctx)==0) {
        return SIputC(vt->index,item.data,item.len);
    }
    int res=SIlookupC(vt->index,item.data,item.len);
    if (res==SI_INDEX_FAILED) {
        //Warning(info,"looking up chunk %s",item.data);
        if (vt->msg_pending) {
            //Print(infoShort,"waiting for msg (%x/%d) %s",vt->msg_pending,vt->msg_pending,&vt->msg_pending);
            HREyieldWhile(vt->ctx,&vt->msg_pending);
        }
        //Warning(info,"preparing message");
        vt->task=-1;
        int32_t tmp=-1;
        if (item.len>MAX_CHUNK_SIZE) Abort("chunk length %d exceeds maximum (%d).",item.len,MAX_CHUNK_SIZE);
        memcpy(vt->msg->buffer,&tmp,4);
        memcpy(vt->msg->buffer+4,item.data,item.len);
        vt->msg->tail=item.len+4;
        vt->task_pending=1;
        vt->msg_pending=1;
        HREpostSend(vt->msg);
        //Warning(info,"waiting for result");
        HREyieldWhile(vt->ctx,&vt->task_pending);
        res=vt->task;
        SIputCAt(vt->index,item.data,item.len,res);
        //Warning(info,"got %d",res);
    }
    return res;
}

static chunk get_chunk(value_table_t vt,value_index_t idx){
    int len;
    char* data=SIgetC(vt->index,idx,&len);
    if (data==NULL) {
        if (HREme(vt->ctx)==0) Abort("chunk %d does not exist",idx);
        //Warning(info,"looking up index %d",idx);
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
        //Warning(info,"got %s (%d)",data,vt->msg_pending);
    }
    return chunk_ld(len,data);
}


static int get_count(value_table_t vt){
    return SIgetCount(vt->index);
}

value_table_t HREcreateTable(hre_context_t ctx,const char* name){
    if (HREpeers(ctx)==1) return chunk_table_create(ctx,(char*)name);
    value_table_t vt=VTcreateBase((char*)name,sizeof(struct value_table_s));
    VTdestroySet(vt,destroy);
    VTputChunkSet(vt,put_chunk);
    VTgetChunkSet(vt,get_chunk);
    VTgetCountSet(vt,get_count);
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

void* HREgreyboxNewmap(void*newmap_context){
    return HREcreateTable((hre_context_t)newmap_context,"<greybox table>");
}

int HREgreyboxC2I(void*map,void*data,int len){
    return VTputChunk((value_table_t)map,chunk_ld(len,data));
}

void* HREgreyboxI2C(void*map,int idx,int*len){
    chunk c=VTgetChunk((value_table_t)map,idx);
    *len=c.len;
    return c.data;
}

int HREgreyboxCount(void*map){
    return VTgetCount((value_table_t)map);
}


