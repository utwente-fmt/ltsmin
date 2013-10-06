// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>

#include <hre-io/stream_object.h>
#include <hre-io/user.h>

struct stream_s {
    struct stream_obj procs;
    char*buf;
    size_t *used;
    size_t len;
};


static size_t mem_read_max(stream_t stream,void*buf,size_t count){
    size_t res=(stream->len) - (*(stream->used));
    if (res<count) count=res;
    memcpy(buf,(stream->buf)+(*(stream->used)),count);
    *(stream->used)+=count;
    return res;
}

static void mem_read(stream_t stream,void*buf,size_t count){
    size_t res=mem_read_max(stream,buf,count);
    if (res<count) Abort("short read");
}

static int mem_empty(stream_t stream){
    return (stream->len==(*(stream->used)));
}

static void mem_write(stream_t stream,void*buf,size_t count){
    size_t res=(stream->len) - (*(stream->used));
    if (res>count) res=count;
    memcpy((stream->buf)+(*(stream->used)),buf,res);
    *(stream->used)+=res;
    if (res<count) Abort("short write");
}

static void mem_close(stream_t *stream){
    RTfree(*stream);
    *stream=NULL;
}

static void mem_flush(stream_t stream){
    (void)stream;
}


stream_t stream_write_mem(void*buf,size_t len,size_t *used){
    stream_t s=(stream_t)HREmalloc(NULL,sizeof(struct stream_s));
    stream_init(s);
    s->procs.write=mem_write;
    s->procs.flush=mem_flush;
    s->procs.close=mem_close;
    *used=0;
    s->buf=buf;
    s->len=len;
    s->used=used;
    return s;
}

stream_t stream_read_mem(void*buf,size_t len,size_t *used){
    stream_t s=(stream_t)HREmalloc(NULL,sizeof(struct stream_s));
    stream_init(s);
    s->procs.read_max=mem_read_max;
    s->procs.read=mem_read;
    s->procs.empty=mem_empty;
    s->procs.close=mem_close;
    *used=0;
    s->buf=buf;
    s->len=len;
    s->used=used;
    return s;
}
