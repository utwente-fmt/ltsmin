#include "config.h"
#include <stdlib.h>
#include <unistd.h>

#include "unix.h"
#include "stream_object.h"
#include "runtime.h"

struct stream_s {
    struct stream_obj procs;
    int fd_in;
    int fd_out;
};

size_t fd_read_max(stream_t stream,void*buf,size_t count){
    ssize_t res=read(stream->fd_in,buf,count);
    if (res<0){
        FatalCall(1,error,"read failed");
    }
    return (size_t)res;
}
void fd_write(stream_t stream,void*buf,size_t count){
    ssize_t res=write(stream->fd_out,buf,count);
    if (res<0){
        FatalCall(1,error,"write failed");
    }
}
void fd_flush(stream_t stream){
    if (fsync(stream->fd_out)){ /* might be unneeded, check with TCP sockets */
        FatalCall(1,error,"fdatasync failed");
    }
}
void fd_close(stream_t *stream){
    if (((*stream)->fd_in)>=0 && close((*stream)->fd_in)){
        FatalCall(1,error,"close failed");
    }
    if (((*stream)->fd_out)>=0
        && ((*stream)->fd_out)!=((*stream)->fd_in)
        && close((*stream)->fd_out)){
        FatalCall(1,error,"close failed");
    }
    RTfree(*stream);
    *stream=NULL;
}

stream_t fd_input(int fd){
    stream_t stream=(stream_t)RT_NEW(struct stream_s);
    stream_init(stream);
    stream->fd_in=fd;
    stream->fd_out=-1;
    stream->procs.read_max=fd_read_max;
    stream->procs.read=stream_default_read;
    stream->procs.close=fd_close;
    return stream;
}

stream_t fd_output(int fd){
    stream_t stream=(stream_t)RT_NEW(struct stream_s);
    stream_init(stream);
    stream->fd_in=-1;
    stream->fd_out=fd;
    stream->procs.write=fd_write;
    stream->procs.flush=fd_flush;
    stream->procs.close=fd_close;
    return stream;
}

stream_t fd_stream(int fd){
    stream_t stream=(stream_t)RT_NEW(struct stream_s);
    stream_init(stream);
    stream->fd_in=fd;
    stream->fd_out=fd;
    stream->procs.read_max=fd_read_max;
    stream->procs.read=stream_default_read;
    stream->procs.write=fd_write;
    stream->procs.flush=fd_flush;
    stream->procs.close=fd_close;
    return stream;
}

stream_t fd_stream_pair(int fd_in,int fd_out){
    stream_t stream=(stream_t)RT_NEW(struct stream_s);
    stream_init(stream);
    stream->fd_in=fd_in;
    stream->fd_out=fd_out;
    stream->procs.read_max=fd_read_max;
    stream->procs.read=stream_default_read;
    stream->procs.write=fd_write;
    stream->procs.flush=fd_flush;
    stream->procs.close=fd_close;
    return stream;
}
