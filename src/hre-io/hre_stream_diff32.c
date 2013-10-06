// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>

#include <hre/unix.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>

typedef int32_t diff_t ;
#define hton hton_32
#define ntoh ntoh_32

struct stream_s {
    struct stream_obj procs;
    stream_t s;
    diff_t prev_rd;
    diff_t prev_wr;
};


static size_t diff_read_max(stream_t stream,void*buf,size_t count){
    size_t res=stream_read_max(stream->s,buf,count);
    if (res%sizeof(diff_t)) {
        Abort("misaligned read");
    }
    int len=res/sizeof(diff_t);
    diff_t prev=stream->prev_rd;
    for(int i=0;i<len;i++){
        prev+=ntoh(((diff_t*)buf)[i]);
        ((diff_t*)buf)[i]=hton(prev);
    }
    stream->prev_rd=prev;
    return res;
}

static void diff_read(stream_t stream,void*buf,size_t count){
    size_t res=diff_read_max(stream,buf,count);
    if (res<count) Abort("short read");
}

static int diff_empty(stream_t stream){
    return stream_empty(stream->s);
}

static void diff_write(stream_t stream,void*buf,size_t count){
    if (count%sizeof(diff_t)) {
        Abort("misaligned write");
    }
    int len=count/sizeof(diff_t);
    diff_t dbuf[len];
    diff_t prev=stream->prev_wr;
    for(int i=0;i<len;i++){
        diff_t current=ntoh(((diff_t*)buf)[i]);
        dbuf[i]=hton(current-prev);
        prev=current;
    }
    stream->prev_wr=prev;
    stream_write(stream->s,dbuf,count);
}

static void diff_close(stream_t *stream){
    stream_close(&((*stream)->s));
    RTfree(*stream);
    *stream=NULL;
}

static void diff_flush(stream_t stream){
    stream_flush(stream->s);
}

stream_t stream_diff32(stream_t s){
    stream_t ds=(stream_t)RTmalloc(sizeof(struct stream_s));
    stream_init(ds);
    ds->s=s;
    ds->prev_rd=0;
    ds->prev_wr=0;
    if (stream_readable(s)) {
        ds->procs.read_max=diff_read_max;
        ds->procs.read=diff_read;
        ds->procs.empty=diff_empty;
    }
    if (stream_writable(s)){
        ds->procs.write=diff_write;
        ds->procs.flush=diff_flush;
    }
    ds->procs.close=diff_close;
    return ds;
}

static size_t undiff_read_max(stream_t stream,void*buf,size_t count){
    size_t res=stream_read_max(stream->s,buf,count);
    if (res%sizeof(diff_t)) {
        Abort("misaligned read");
    }
    int len=res/sizeof(diff_t);
    diff_t prev=stream->prev_rd;
    for(int i=0;i<len;i++){
        diff_t current=ntoh(((diff_t*)buf)[i]);
        ((diff_t*)buf)[i]=hton(current-prev);
        prev=current;
    }
    stream->prev_rd=prev;
    return res;
}

static void undiff_read(stream_t stream,void*buf,size_t count){
    size_t res=undiff_read_max(stream,buf,count);
    if (res<count) Abort("short read");
}

static void undiff_write(stream_t stream,void*buf,size_t count){
    if (count%sizeof(diff_t)) {
        Abort("misaligned write");
    }
    int len=count/sizeof(diff_t);
    diff_t dbuf[len];
    diff_t prev=stream->prev_wr;
    for(int i=0;i<len;i++){
        prev+=ntoh(((diff_t*)buf)[i]);
        dbuf[i]=hton(prev);
    }
    stream->prev_wr=prev;
    stream_write(stream->s,dbuf,count);
}

stream_t stream_undiff32(stream_t s){
    stream_t ds=(stream_t)RTmalloc(sizeof(struct stream_s));
    stream_init(ds);
    ds->s=s;
    ds->prev_rd=0;
    ds->prev_wr=0;
    if (stream_readable(s)) {
        ds->procs.read_max=undiff_read_max;
        ds->procs.read=undiff_read;
        ds->procs.empty=diff_empty;
    }
    if (stream_writable(s)){
        ds->procs.write=undiff_write;
        ds->procs.flush=diff_flush;
    }
    ds->procs.close=diff_close;
    return ds;
}


