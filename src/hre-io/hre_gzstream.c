// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <hre-io/stream_object.h>
#include <hre-io/user.h>

struct stream_s {
    struct stream_obj procs;
    stream_t s;
    int bufsize;
    int compress;
    z_stream wr;
    void *wr_buf;
    uint64_t orig_size;
    z_stream rd;
    void *rd_buf;
};

static void gzip_close_z(stream_t *stream,uint64_t orig_size){
    int res;
    if ((*stream)->wr_buf) {
        do {
            if ((*stream)->compress) {
                res=deflate(&(*stream)->wr,Z_FINISH);
            } else {
                res=inflate(&(*stream)->wr,Z_FINISH);
            }
            if(res!=Z_OK && res!=Z_STREAM_END){
                Abort("compression error %d %s",res,zError(res));
            }
            int len=((*stream)->bufsize) - ((*stream)->wr.avail_out);
            if (len>0) {
                stream_write((*stream)->s,(*stream)->wr_buf,len);
                (*stream)->wr.next_out=(*stream)->wr_buf;
                (*stream)->wr.avail_out=(*stream)->bufsize;
            }
        } while (res!=Z_STREAM_END);
        if ((*stream)->compress) {
            res=deflateEnd(&(*stream)->wr);
        } else {
            res=inflateEnd(&(*stream)->wr);
        }
        switch(res){
        case Z_OK: break;
        default:
            Abort("cleanup failed");
        }
        RTfree((*stream)->wr_buf);
    }
    if ((*stream)->rd_buf) {
        if ((*stream)->compress) {
            res=inflateEnd(&(*stream)->rd);
        } else {
            res=deflateEnd(&(*stream)->rd);
        }
        switch(res){
        case Z_OK: break;
        default:
            Abort("cleanup failed");
        }
        RTfree((*stream)->rd_buf);
    }
    if (stream_writable(*stream)) {
        stream_close_z(&(*stream)->s,orig_size);
    } else {
        stream_close(&(*stream)->s);
    }
    RTfree (*stream);
    *stream=NULL;
}

static void gzip_close(stream_t *stream){
    gzip_close_z(stream,(*stream)->orig_size);
}

static size_t gzip_read_max(stream_t stream,void*buf,size_t count){
    stream->rd.next_out=buf;
    stream->rd.avail_out=count;
    int res;
    do {
        if (stream->rd.avail_in==0){
            stream->rd.next_in=stream->rd_buf;
            stream->rd.avail_in=stream_read_max(stream->s,stream->rd_buf,stream->bufsize);
        }
        if (stream->compress){
            res=inflate(&stream->rd, Z_NO_FLUSH);
        } else {
            res=deflate(&stream->rd, Z_NO_FLUSH);
        }
        if(res!=Z_OK && res!=Z_STREAM_END){
            Abort("compression error %d %s",res,zError(res));
        }
    } while(stream->rd.avail_out && res!=Z_STREAM_END);
    return (count-stream->rd.avail_out);
}

static void gzip_read(stream_t stream,void*buf,size_t count){
    size_t res=gzip_read_max(stream,buf,count);
    if (res<count) Abort("short read %zu instead of %zu",res,count);
}

static void gzip_write(stream_t stream,void*buf,size_t count){
    stream->wr.next_in=buf;
    stream->wr.avail_in=count;
    stream->orig_size+=count;
    while(stream->wr.avail_in){
        int res;
        if (stream->compress){
            res=deflate(&stream->wr,Z_NO_FLUSH);
        } else {
            res=inflate(&stream->wr,Z_NO_FLUSH);
        }
        if(res != Z_OK && res!=Z_STREAM_END) {
            Abort("compression error %d: %s.",res,zError(res));
        }
        if (stream->wr.avail_out==0){
            stream_write(stream->s,stream->wr_buf,stream->bufsize);
            stream->wr.next_out=stream->wr_buf;
            stream->wr.avail_out=stream->bufsize;
        }
    }
    stream->wr.next_in=Z_NULL;
}

static stream_t stream_zip(stream_t parent,int compress,int level,int bufsize){
    stream_t s=(stream_t)RTmallocZero(sizeof(struct stream_s));
    stream_init(s);
    s->bufsize=bufsize;
    s->s=parent;
    s->compress=compress;
    if(stream_writable(parent)){
        s->procs.write=gzip_write;
        //s->procs.flush= TO DO
        s->wr.zalloc = Z_NULL;
        s->wr.zfree = Z_NULL;
        s->wr.opaque = Z_NULL;
        s->wr_buf=(char*)RTmalloc(bufsize);
        s->wr.next_in=Z_NULL;
        s->wr.avail_in=0;
        s->wr.next_out=s->wr_buf;
        s->wr.avail_out=bufsize;
        if (compress) {
            if (deflateInit(&s->wr, level)!= Z_OK) {
                Abort("gzip init failed");
            }
        } else {
            if (inflateInit(&s->wr)!= Z_OK) {
                Abort("gzip init failed");
            }
        }
    } else {
        s->wr_buf=NULL;
    }
    if(stream_readable(parent)){
        s->procs.read_max=gzip_read_max;
        s->procs.read=gzip_read;
        //s->procs.empty= TO DO
        s->rd.zalloc = Z_NULL;
        s->rd.zfree = Z_NULL;
        s->rd.opaque = Z_NULL;
        s->rd.next_in=Z_NULL;
        s->rd.avail_in=0;
        s->rd.next_out=Z_NULL;
        s->rd.avail_out=0;
        s->rd_buf=(char*)RTmalloc(bufsize);
        if (compress) {
            if (inflateInit(&s->rd)!= Z_OK) {
                Abort("gzip init failed");
            }
        } else {
            if (deflateInit(&s->rd, level)!= Z_OK) {
                Abort("gzip init failed");
            }
        }
    } else {
        s->rd_buf=NULL;
    }
    s->procs.close=gzip_close;
    s->procs.close_z=gzip_close_z;
    return stream_buffer(s,4096);
}

stream_t stream_gzip(stream_t compressed,int level,int bufsize){
    return stream_zip(compressed,1,level,bufsize);
}
stream_t stream_gunzip(stream_t expanded,int level,int bufsize){
    return stream_zip(expanded,0,level,bufsize);
}

