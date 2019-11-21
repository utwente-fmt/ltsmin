// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>
#include <stdlib.h>

#include <hre/provider.h>
#include <hre/internal.h>
#include <util-lib/dynamic-array.h>

/* Type definitions */

static void comm_resize(void*arg,void*old_array,int old_size,void*new_array,int new_size){
    (void)arg;(void)old_array;
    if (new_size<old_size) Abort("unimplemented");
    struct comm* comm=(struct comm*)new_array;
    for(int i=old_size;i<new_size;i++){
        comm[i].action_man=create_manager(64);
        comm[i].action=NULL;
        ADD_ARRAY(comm[i].action_man,comm[i].action,struct action);
    }
}

array_manager_t HREcommManager(hre_context_t context){
    return USR2SYS(context)->comm_man;
}

/* generic link methods */

static const char* single_class="single thread";

hre_context_t HREcreateSingle(){
    hre_context_t ctx=HREctxCreate(0,1,single_class,0);
    HREctxComplete(ctx);
    return ctx;
}

static void nop_destroy(hre_context_t ctx){
    (void)ctx;
}

/* abstract class methods */

hre_context_t HREctxCreate(int me,int peers,const char* class_name,size_t user_size){
    hre_context_t ctx=(hre_context_t)HREmallocZero(hre_heap,system_size+user_size);
    ctx->me=me;
    ctx->peers=peers;
    ctx->class_name=class_name;
    ctx->comm_man=create_manager(1);
    ctx->comm=NULL;
    ADD_ARRAY_CB(ctx->comm_man,ctx->comm,struct comm,comm_resize,NULL);
    return SYS2USR(ctx);
}

static void process_abort(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void process_abort(hre_context_t ctx,int code){
    (void)ctx;
    exit(code);
}

static void process_exit(hre_context_t ctx,int code) __attribute__ ((noreturn));
static void process_exit(hre_context_t ctx,int code){
    (void)ctx;
    if (code) {
        exit(code);
    } else {
        exit(HRE_EXIT_SUCCESS);
    }
}

void hre_ready_decr(hre_msg_t self,void* ready_ctx){
    (*((int*)ready_ctx))--;
    Debug("decrease on %d -> %d : %d",self->source,self->target,*((int*)ready_ctx));
    (void) self;
}


void HREctxComplete(hre_context_t ctx){
    ctx=USR2SYS(ctx);
    if (ctx->send==NULL && ctx->peers>1) Abort("send mandatory for more than 1 peer.");
    if (ctx->recv==NULL){
        if (ctx->peers>1) Abort("recv mandatory for more than 1 peer.");
        ctx->recv_type=HRErecvActive;
    }
    if(ctx->destroy==NULL) {
        ctx->destroy=nop_destroy;
    }
    if(ctx->abort==NULL) ctx->abort=process_abort;
    if(ctx->exit==NULL) ctx->exit=process_exit;
    hre_init_reduce(SYS2USR(ctx));
}

void HREsetDestroy(hre_context_t ctx,hre_destroy_m method){
    USR2SYS(ctx)->destroy=method;
}

void HREctxDestroy(hre_context_t ctx){
    USR2SYS(ctx)->destroy(ctx);
    HREfree(hre_heap,USR2SYS(ctx));
}

int HREme(hre_context_t ctx){
    return USR2SYS(ctx)->me;
}

int HREpeers(hre_context_t ctx){
    return USR2SYS(ctx)->peers;
}

int HREleader(hre_context_t ctx){
    return USR2SYS(ctx)->me == 0;
}

int HREcheckAny(hre_context_t ctx,int arg){
    uint32_t in=arg?1:0;
    uint32_t res;
    HREreduce(ctx,1,&in,&res,UInt32,Max);
    return res>0;
}

void HREsetAbort(hre_context_t ctx,hre_abort_m method){
    USR2SYS(ctx)->abort=method;
}

void HREctxAbort(hre_context_t ctx,int code){
    USR2SYS(ctx)->abort(ctx,code);
}

void HREsetExit(hre_context_t ctx,hre_exit_m method){
    USR2SYS(ctx)->exit=method;
}

void HREctxExit(hre_context_t ctx,int code){
    USR2SYS(ctx)->exit(ctx,code);
}

void HREbarrier(hre_context_t ctx){
    HREcheckAny(ctx,0);
    Debug ("BARRIER OUT %d", HREme(HREglobal()));
}

const char* HREclass(hre_context_t ctx){
    return USR2SYS(ctx)->class_name;
}

void HREyieldSet(hre_context_t ctx,hre_yield_m method){
    USR2SYS(ctx)->yield=method;
}

void HREyield(hre_context_t ctx){
    if (USR2SYS(ctx)->peers==1) return;
    if (USR2SYS(ctx)->yield) USR2SYS(ctx)->yield(ctx);
}

void HREyieldWhileSet(hre_context_t ctx,hre_yield_while_m method){
    USR2SYS(ctx)->yield_while=method;
}

void HREyieldWhile(hre_context_t ctx,int *condition){
    if (*condition) {
        if (USR2SYS(ctx)->peers==1) Abort("deadlock detected");
        USR2SYS(ctx)->yield_while(ctx,condition);
    }
}

void HREcondSignalSet(hre_context_t ctx, hre_cond_signal_m method){
    USR2SYS(ctx)->cond_signal=method;
}

void HREcondSignal(hre_context_t ctx, int id){
    if (USR2SYS(ctx)->cond_signal) USR2SYS(ctx)->cond_signal(ctx, id);
}

hre_msg_t HREnewMessage(hre_context_t context,uint32_t size){
    hre_msg_t res=(hre_msg_t)HREmallocZero(USR2SYS(context)->msg_region,sizeof(struct hre_msg_s)+size);
    res->context=context;
    res->size=size;
    return res;
}

void HREdestroyMessage(hre_msg_t msg){
    HREfree(NULL,msg);
}

void HREpostSend(hre_msg_t msg){
    Debug("post send of message %d -> %d comm %d tag %d",msg->source,msg->target,msg->comm,msg->tag);
    if (USR2SYS(msg->context)->me==(int)msg->target){
        HREdeliverMessage(msg);
    } else {
        USR2SYS(msg->context)->send(msg->context,msg);
    }
}

void HREsend(hre_msg_t msg){
    Debug("blocking send of message %d -> %d comm %d tag %d",msg->source,msg->target,msg->comm,msg->tag);
    if (USR2SYS(msg->context)->me==(int)msg->target){
        HREdeliverMessage(msg);
    } else {
        // replace callback with our own.
        void* ready_ctx=msg->ready_ctx;
        void (*ready)(hre_msg_t self,void* ready_ctx)=msg->ready;
        int pending=1;
        msg->ready_ctx=&pending;
        msg->ready=hre_ready_decr;
        // post send and wait.
        HREpostSend(msg);
        HREyieldWhile(msg->context,&pending);
        // restore and invoke original callback.
        msg->ready_ctx=ready_ctx;
        msg->ready=ready;
        HREmsgReady(msg);
    }
}

void HREdeliverMessage(hre_msg_t msg){
    Debug("delivery of message %d -> %d comm %d tag %d",msg->source,msg->target,msg->comm,msg->tag);
    struct action* act=&(USR2SYS(msg->context)->comm[msg->comm].action[msg->tag]);
    if (act->action) {
        act->action(act->arg,msg);
    } else {
        Abort("missing action on priority %d tag %d",msg->comm,msg->tag);
    }
}

static uint32_t next_tag(struct comm* comm){
    uint32_t max=array_size(comm->action_man);
    uint32_t tag;
    for(tag=0;tag<max;tag++){
        if (comm->action[tag].action==NULL) return tag;
    }
    ensure_access(comm->action_man,tag);
    return tag;
}

uint32_t HREactionCreateUnchecked(hre_context_t context,uint32_t comm,uint32_t size,hre_receive_cb response,void* response_arg){
    context=USR2SYS(context);
    ensure_access(context->comm_man,comm);
    uint32_t tag=next_tag(&context->comm[comm]);
    struct action* act=&context->comm[comm].action[tag];
    act->action=response;
    act->arg=response_arg;
    if (context->recv_type==HRErecvPassive){
        for(int i=0;i<context->peers;i++) if (i!=context->me){
            hre_msg_t msg=HREnewMessage(SYS2USR(context),size);
            msg->comm=comm;
            msg->source=i;
            msg->target=context->me;
            msg->tag=tag;
            context->recv(SYS2USR(context),msg);
        }
    }
    return tag;
}

uint32_t HREactionCreate(hre_context_t context,uint32_t comm,uint32_t size,hre_receive_cb response,void* response_arg){
    uint32_t tag=HREactionCreateUnchecked(context,comm,size,response,response_arg);
    uint32_t tmp;
    HREreduce(context,1,&tag,&tmp,UInt32,Max);
    if (tmp!=tag) Abort("inconsistent tags %d and %d",tmp,tag);
    return tag;
}

void HREactionDelete(hre_context_t context,uint32_t comm,uint32_t tag){
    if (context->recv) {
        Print(lerror,"cancellation mechanism needed!");
    } else {
        context->comm[comm].action[tag].action=NULL;
    }
}

void HREsendSet(hre_context_t context,hre_xfer_m method){
    USR2SYS(context)->send=method;
}

void HRErecvSet(hre_context_t context,hre_xfer_m method,hre_recv_t semantics){
    USR2SYS(context)->recv_type=semantics;
    USR2SYS(context)->recv=method;
}

void HREmsgReady(hre_msg_t msg){
    Debug("finished processing message %d -> %d comm %d tag %d",msg->source,msg->target,msg->comm,msg->tag);
    if (USR2SYS(msg->context)->me==(int)msg->source){
        Debug("local message ready");
        if (msg->ready) msg->ready(msg,msg->ready_ctx);
    } else {
        Debug("remote message %d -> %d ready",msg->source,msg->target);
        USR2SYS(msg->context)->recv(msg->context,msg);
    }
}

hre_region_t HREdefaultRegion(hre_context_t context){
    return USR2SYS(context)->msg_region;
}

void HREmsgRegionSet(hre_context_t context,hre_region_t region){
    USR2SYS(context)->msg_region=region;
}

void HREshmGetSet(hre_context_t context,hre_shm_get_m method){
    USR2SYS(context)->shm_get=method;
}

void* HREshmGet(hre_context_t context,size_t size){
    if (USR2SYS(context)->shm_get) {
        return USR2SYS(context)->shm_get(context,size);
    } else if (HREpeers(context)==1) {
        return RTmalloc(size);
    } else {
        return NULL;
    }
}


