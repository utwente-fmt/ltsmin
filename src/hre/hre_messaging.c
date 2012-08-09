// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#include <hre/config.h>

#include <hre/internal.h>

struct hre_buffer_s {
    int tag;
    hre_context_t context;
    struct msg_queue_s*fifo;
    int *empty;
};

void hre_put_msg(msg_queue_t queue, hre_msg_t msg){
    msg->next=NULL;
    if (queue->head==NULL) {
        queue->head=msg;
    } else {
        queue->tail->next=msg;
    }
    queue->tail=msg;
}

hre_msg_t hre_get_msg(msg_queue_t queue){
    hre_msg_t msg=queue->head;
    if (msg) queue->head=msg->next;
    return msg;
}

hre_msg_t HRErecv(hre_buffer_t buf,int from){
    HREyieldWhile(buf->context,&buf->empty[from]);
    hre_msg_t msg=hre_get_msg(&buf->fifo[from]);
    if (buf->fifo[from].head==NULL){
        buf->empty[from]=1;
    }
    return msg;
}

int HREbufferTag(hre_buffer_t buf){
    return buf->tag;
}

static void enqueue(void* context,hre_msg_t msg){
    hre_buffer_t buf=(hre_buffer_t)context;
    hre_put_msg(&buf->fifo[msg->source],msg);
    buf->empty[msg->source]=0;
}

hre_buffer_t HREbufferCreate(hre_context_t context,uint32_t prio,uint32_t size){
    hre_buffer_t buf=RT_NEW(struct hre_buffer_s);
    buf->context=context;
    int peers=HREpeers(context);
    buf->empty=RTmalloc(peers*sizeof(int));
    for(int i=0;i<peers;i++) buf->empty[i]=1;
    buf->fifo=RTmallocZero(peers*sizeof(struct msg_queue_s));
    buf->tag=HREactionCreate(context,prio,size,enqueue,buf);
    return buf;
}

