#include <stdlib.h>

#include "struct_io.h"
#include "runtime.h"

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

void DSreadStruct(struct_stream_t stream,void *data){
	uint32_t *vec=(uint32_t*)data;
	for(int i=0;i<stream->len;i++){
		vec[i]=DSreadU32(stream->ds[i]);
	}
}

void DSstructClose(struct_stream_t *stream){
	for(int i=0;i<(*stream)->len;i++){
		DSclose(((*stream)->ds)+i);
	}
	free((*stream)->ds);
	free(*stream);
	*stream=NULL;
}

struct_stream_t arch_read_vec_U32(archive_t archive,char*fmt,int len,char*code){
	struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
	char temp[1024];
	stream->len=len;
	stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
	for(int i=0;i<len;i++){
		sprintf(temp,fmt,i);
		stream->ds[i]=arch_read(archive,temp,code);
	}
	return stream;
}

struct_stream_t arch_write_vec_U32(archive_t archive,char*fmt,int len,char*code,int hdr){
	struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
	char temp[1024];
	stream->len=len;
	stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
	for(int i=0;i<len;i++){
		sprintf(temp,fmt,i);
		stream->ds[i]=arch_write(archive,temp,code,hdr);
	}
	return stream;
}

struct_stream_t arch_read_vec_U32_named(archive_t archive,char*fmt,int len,char **name,char*code){
	struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
	char temp[1024];
	stream->len=len;
	stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
	for(int i=0;i<len;i++){
		sprintf(temp,fmt,name[i]);
		stream->ds[i]=arch_read(archive,temp,code);
	}
	return stream;
}

struct_stream_t arch_write_vec_U32_named(archive_t archive,char*fmt,int len,char **name,char*code,int hdr){
	struct_stream_t stream=(struct_stream_t)RTmalloc(sizeof(struct struct_stream_s));
	char temp[1024];
	stream->len=len;
	stream->ds=(stream_t*)RTmalloc(len*sizeof(stream_t));
	for(int i=0;i<len;i++){
		sprintf(temp,fmt,name[i]);
		stream->ds[i]=arch_write(archive,temp,code,hdr);
	}
	return stream;
}

