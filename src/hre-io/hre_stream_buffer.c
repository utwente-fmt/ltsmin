// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>

#include <hre/unix.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>

struct stream_s {
    struct stream_obj procs;
    stream_t s;
    uint32_t rd_sz;
    uint32_t rd_next;
    uint32_t rd_used;
    void *rd_buf;
    uint32_t wr_sz;
    uint32_t wr_used;
    void *wr_buf;
};

static size_t buf_read_max(stream_t stream,void*buf,size_t count){
    if (stream->rd_next + count <= stream->rd_used){
        // if we already have the data just copy
        memcpy(buf,stream->rd_buf+stream->rd_next,count);
        stream->rd_next+=count;
        return count;
    }
    // copy what we have;
    size_t len=stream->rd_used-stream->rd_next;
    size_t total=len;
    memcpy(buf,stream->rd_buf+stream->rd_next,len);
    buf+=len;
    count-=len;
    len=stream->rd_sz;
    while(count>=len){
        // directly read whole blocks
        size_t res=stream_read_max(stream->s,buf,len);
        if (res<len) return total+res;
        total+=len;
        buf+=len;
        count-=len;
    }
    // fetch a new block to the buffer and copy the rest.
    stream->rd_used=stream_read_max(stream->s,stream->rd_buf,len);
    if (stream->rd_used < count){
        stream->rd_next=stream->rd_used;
    } else {
        stream->rd_next=count;
    }
    total+=stream->rd_next;
    memcpy(buf,stream->rd_buf,stream->rd_next);
    return total;
}

static void buf_read(stream_t stream,void*buf,size_t count){
    size_t res=buf_read_max(stream,buf,count);
    if (res<count) Abort("short read %zu instead of %zu",res,count);
}

static int buf_empty(stream_t stream){
    if (stream->rd_next < stream->rd_used) {
        return 0;
    }
    stream->rd_next=0;
    stream->rd_used=stream_read_max(stream->s,stream->rd_buf,stream->rd_sz);
    return (stream->rd_used==0);
}

static void buf_write(stream_t stream,void*buf,size_t count){
    if (stream->wr_used + count < stream->wr_sz){
        // if it fits with space left just copy to buffer
        memcpy(stream->wr_buf+stream->wr_used,buf,count);
        stream->wr_used+=count;
        return;
    }
    size_t len;
    if (stream->wr_used){
        // fill the buffer and write it.
        len=stream->wr_sz-stream->wr_used;
        memcpy(stream->wr_buf+stream->wr_used,buf,len);
        stream_write(stream->s,stream->wr_buf,stream->wr_sz);
        buf+=len;
        count-=len;
    }
    len=stream->wr_sz;
    while(count>=len){
        // write whole blocks directly from the given buffer.
        stream_write(stream->s,buf,len);
        buf+=len;
        count-=len;
    }
    // copy the remaining data to the buffer.
    memcpy(stream->wr_buf,buf,count);
    stream->wr_used=count;
}

static void buf_flush(stream_t stream){
    if(stream->wr_used && (stream->s->procs.flush != stream_illegal_flush)){
        stream_write(stream->s,stream->wr_buf,stream->wr_used);
        stream->wr_used=0;
    }
    stream_flush(stream->s);
}

static void buf_close_z(stream_t *stream,uint64_t orig_size){
    if((*stream)->rd_buf){
        RTfree((*stream)->rd_buf);
    }
    if((*stream)->wr_buf){
        if((*stream)->wr_used){
            stream_write((*stream)->s,(*stream)->wr_buf,(*stream)->wr_used);
        }
        RTfree((*stream)->wr_buf);
    }
    if (orig_size==(uint64_t)-1){
        stream_close(&((*stream)->s));
    } else {
        stream_close_z(&((*stream)->s),orig_size);
    }
    RTfree(*stream);
    *stream=NULL;
}

static void buf_close(stream_t *stream){
    buf_close_z(stream,(uint64_t)-1);
}

stream_t stream_buffer(stream_t s,int size){
    stream_t bs=(stream_t)RTmalloc(sizeof(struct stream_s));
    stream_init(bs);
    if (stream_readable(s)) {
        bs->rd_buf=RTmalloc(size);
        bs->procs.read_max=buf_read_max;
        bs->procs.read=buf_read;
        bs->procs.empty=buf_empty;
    } else {
        bs->rd_buf=NULL;
    }
    bs->rd_sz=size;
    bs->rd_next=0;
    bs->rd_used=0;
    if (stream_writable(s)){
        bs->wr_buf=RTmalloc(size);
        bs->procs.write=buf_write;
        bs->procs.flush=buf_flush;
    } else {
        bs->wr_buf=NULL;
    }
    bs->wr_sz=size;
    bs->wr_used=0;
    bs->s=s;
    bs->procs.close=buf_close;
    bs->procs.close_z=buf_close_z;
    return bs;
}
