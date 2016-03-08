// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>

#include <stdlib.h>

#include <hre/internal.h>
#include <hre/provider.h>
#include <util-lib/dynamic-array.h>

#define REDUCE_SIZE 16384

static void reduce_action(void* context,hre_msg_t msg){
    hre_context_t ctx=USR2SYS(context);
    int turn=ctx->reduce_turn[msg->source];
    ctx->reduce_turn[msg->source]=1-turn;
    if (ctx->reduce_count[turn]<=0 || ctx->reduce_count[turn]>ctx->peers)
        Abort("inconsistency count: %d",ctx->reduce_count[turn]);
    uint32_t*tmp=(uint32_t*)msg->buffer;
    unit_t type=tmp[0];
    operand_t op=tmp[1];
    if (type == Pointer) {
        type = sizeof(char *) == 4 ? UInt32 : UInt64;
    } else if(type == SizeT) {
        type = sizeof(size_t) == 4 ? UInt32 : UInt64;
    }
    if (ctx->reduce_count[turn]==ctx->peers) {
        ctx->reduce_op[turn]=op;
        ctx->reduce_type[turn]=type;
    } else {
        if (ctx->reduce_op[turn]!=op) Abort("inconsistent op");
        if (ctx->reduce_type[turn]!=type) Abort("inconsistent type");
    }
    ctx->reduce_count[turn]--;
   switch(type){
    case UInt32:
    {
        int len=msg->tail/4-2;
        uint32_t *out=(uint32_t *)ctx->reduce_out[turn];
        uint32_t *in=(uint32_t *)&msg->buffer[8];
        switch(op){
        case Sum:
            for(int i=0;i<len;i++) out[i]+=in[i];
            break;
        case Max:
            for(int i=0;i<len;i++) if (in[i]>out[i]) out[i]=in[i];
            break;
        default: Abort("missing case");
        }
        break;
    }
    case UInt64:
    {
        int len=msg->tail/8-1;
        uint64_t *out=(uint64_t *)ctx->reduce_out[turn];
        uint64_t *in=(uint64_t *)&msg->buffer[8];
        switch(op){
        case Sum:
            for(int i=0;i<len;i++) out[i]+=in[i];
            break;
        case Max:
            for(int i=0;i<len;i++) if (in[i]>out[i]) out[i]=in[i];
            break;
        default: Abort("missing case");
        }
        break;
    }
    default: Abort("missing case");
    }
    HREmsgReady(msg);
}

void hre_init_reduce(hre_context_t ctx){
    ctx=USR2SYS(ctx);
    ctx->reduce_turn=(int*)HREmallocZero(NULL,sizeof(int)*ctx->peers);
    ctx->reduce_count[0]=ctx->peers;
    ctx->reduce_count[1]=ctx->peers;
    ctx->reduce_msg=(hre_msg_t*)HREmalloc(NULL,sizeof(hre_msg_t)*ctx->peers);
    ctx->reduce_out[0]=HREmallocZero(NULL,REDUCE_SIZE);
    ctx->reduce_out[1]=HREmallocZero(NULL,REDUCE_SIZE);
    ctx->reduce_tag=HREactionCreateUnchecked(SYS2USR(ctx),0,REDUCE_SIZE,reduce_action,SYS2USR(ctx));
    for(int i=0;i<ctx->peers;i++) {
        ctx->reduce_msg[i]=HREnewMessage(SYS2USR(ctx),REDUCE_SIZE);
        ctx->reduce_msg[i]->source=ctx->me;
        ctx->reduce_msg[i]->target=i;
        ctx->reduce_msg[i]->comm=0;
        ctx->reduce_msg[i]->tag=ctx->reduce_tag;
        ctx->reduce_msg[i]->ready=hre_ready_decr;
    }
}

void HREreduce(hre_context_t ctx,int len,void*in,void*out,unit_t type,operand_t op){
    ctx=USR2SYS(ctx);
    size_t size;
    switch(type){
    case UInt32:
        size=4*len;
        break;
    case UInt64:
        size=8*len;
        break;
    case Pointer:
        size=sizeof(void*)*len;
        break;
    case SizeT:
        size=sizeof(size_t)*len;
        break;
    default:
        Abort("missing case");
    }
    if (ctx->peers==1) {
        memmove(out,in,size);
        return;
    }
    if (size>REDUCE_SIZE-8) Abort("array too big in reduce");
    Debug("enter reduce");
    int turn=ctx->reduce_turn[ctx->me];
    /* prepare the receive for the next turn */
    ctx->reduce_count[1-turn]=ctx->peers;
    memset(ctx->reduce_out[1-turn],0,REDUCE_SIZE);
    /* send out our addition to everyone, this clears the next turn */
    int count=ctx->peers;
    for(int i=0;i<ctx->peers;i++) {
        uint32_t*tmp=(uint32_t*)ctx->reduce_msg[i]->buffer;
        tmp[0]=type;
        tmp[1]=op;
        memcpy(&ctx->reduce_msg[i]->buffer[8],in,size);
        ctx->reduce_msg[i]->tail=size+8;
        ctx->reduce_msg[i]->ready_ctx=&count;
        HREpostSend(ctx->reduce_msg[i]);
    }
    Debug("waiting for sends")
    HREyieldWhile(SYS2USR(ctx),&count);
    Debug("waiting for reductions")
    HREyieldWhile(SYS2USR(ctx),&ctx->reduce_count[turn]);
    /* copy the result of this turn */
    memcpy(out,ctx->reduce_out[turn],size);
    Debug("leave reduce");
}


