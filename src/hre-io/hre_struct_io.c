// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>

#include <hre-io/user.h>

struct struct_stream_s{
    int len;
    stream_t *ds;
};

void DSwriteStruct(struct_stream_t stream,void *data){
    uint32_t *vec=(uint32_t*)data;
    for(int i=0;i<stream->len;i++){
        DSwriteU32(stream->ds[i],vec[i]);
    }
}

int DSreadStruct(struct_stream_t stream,void *data){
    uint32_t *vec=(uint32_t*)data;
    for(int i=0;i<stream->len;i++){
        if (DSempty(stream->ds[i])) return 0;
        vec[i]=DSreadU32(stream->ds[i]);
    }
    return 1;
}

int DSstructEmpty(struct_stream_t stream){
    return DSempty(stream->ds[0]);
}

void DSstructClose(struct_stream_t *stream){
    for(int i=0;i<(*stream)->len;i++){
        DSclose(((*stream)->ds)+i);
    }
    RTfree((*stream)->ds);
    RTfree(*stream);
    *stream=NULL;
}

struct_stream_t arch_read_vec_U32(archive_t archive,char*fmt,int len){
    struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
    char temp[1024];
    stream->len=len;
    stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
    for(int i=0;i<len;i++){
        sprintf(temp,fmt,i);
        stream->ds[i]=arch_read(archive,temp);
    }
    return stream;
}

struct_stream_t arch_write_vec_U32(archive_t archive,char*fmt,int len){
    struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
    char temp[1024];
    stream->len=len;
    stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
    for(int i=0;i<len;i++){
        sprintf(temp,fmt,i);
        stream->ds[i]=arch_write(archive,temp);
    }
    return stream;
}

struct_stream_t arch_read_vec_U32_named(archive_t archive,char*fmt,int len,char **name){
    struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
    char temp[1024];
    stream->len=len;
    stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
    for(int i=0;i<len;i++){
        sprintf(temp,fmt,name[i]);
        stream->ds[i]=arch_read(archive,temp);
    }
    return stream;
}

struct_stream_t arch_write_vec_U32_named(archive_t archive,char*fmt,int len,char **name){
    struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
    char temp[1024];
    stream->len=len;
    stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
    for(int i=0;i<len;i++){
        sprintf(temp,fmt,name[i]);
        stream->ds[i]=arch_write(archive,temp);
    }
    return stream;
}
