#include "stream_object.h"
#include "runtime.h"
#include <malloc.h>
#include <string.h>
#include <zlib.h>



struct stream_s {
	struct stream_obj procs;
	stream_t s;
	int bufsize;
	z_stream wr;
	void *wr_buf;
	z_stream rd;
	void *rd_buf;
};

/*
static int free_read(stream_t stream,int used){
	if(stream->rd_held){
		return 1;
	} else {
		stream->rd_ofs+=used;
		stream->rd_len-=used;
		stream->rd_held=1;
		return 0;
	}
}

static int get_read(stream_t stream,void**buf,int*offset,int*len){
	if(stream->rd_held){
		if (stream->rd_len==0){
			stream->rd.next_out=(unsigned char*)stream->rd_buf;
			stream->rd.avail_out=stream->bufsize;
			int res;
			do {
				if(stream->rd.avail_in==0){
					if(stream->rdc_buf!=NULL){
						if (stream_free_read_buffer(stream->compressed,stream->rdc_len)){
							return 1;
						}
					}
					if (stream_get_read_buffer(stream->compressed,&stream->rdc_buf,&stream->rdc_ofs,&stream->rdc_len)){
						return 1;
					}
					stream->rd.next_in=(unsigned char*)(stream->rdc_buf+stream->rdc_ofs);
					stream->rd.avail_in=stream->rdc_len;
				}
				res=inflate(&stream->rd, Z_NO_FLUSH);
				if(res!=Z_OK && res!=Z_STREAM_END){
					Fatal(1,error,"inflate error");
					if(res==Z_NEED_DICT) Fatal(1,error,"Z_NEED_DICT");
					if(res==Z_DATA_ERROR) Fatal(1,error,"Z_DATA_ERROR");
					if(res==Z_STREAM_ERROR) Fatal(1,error,"Z_STREAM_ERROR");
					if(res==Z_MEM_ERROR) Fatal(1,error,"Z_MEM_ERROR");
					if(res==Z_BUF_ERROR) Fatal(1,error,"Z_BUF_ERROR");
					return 1;
				}
			} while (stream->rd.avail_out && res!=Z_STREAM_END);
			stream->rd_ofs=0;
			stream->rd_len=stream->bufsize-stream->rd.avail_out;
			if(stream->rd_len==0) {
				// end of file.
				return 1;
			}
		}
		*buf=stream->rd_buf;
		*offset=stream->rd_ofs;
		*len=stream->rd_len;
		stream->rd_held=0;
		return 0;
	} else {
		return 1;
	}
}


static int gzip_flush(stream_t stream){
	return 0;
}

static int free_write(stream_t stream,int used){
	if (stream->wr_held) {
		Fatal(1,error,"buffer was not taken");
		return 1;
	}
	stream->wr_held=1;
	if ((stream->wr_ofs+used) > stream->bufsize){
		Fatal(1,error,"buffer overflow");
		return 1;
	}
	stream->wr_ofs+=used;
	if (stream->wr_ofs==stream->bufsize) {
		return write_flush(stream,Z_NO_FLUSH);
	}
	return 0;
}


static int get_write(stream_t stream,void**buf,int*offset,int*len){
	if (stream->wr_held) {
		stream->wr_held=0;
		*buf=stream->wr_buf;
		*offset=stream->wr_ofs;
		*len=stream->bufsize-stream->wr_ofs;
		return 0;
	} else {
		Fatal(1,error,"buffer already taken");
		return 1;
	}
}

static void write_flush(stream_t stream,int flush){
	int res;
	do {
		switch(
		void *buf;
		int ofs;
		int len;
		if(stream_get_write_buffer(stream->compressed,&buf,&ofs,&len)){
			return 1;
		}
		stream->wr.next_out=(unsigned char*)(buf+ofs);
		stream->wr.avail_out=len;
		res=deflate(&stream->wr, flush);
		if(res!=Z_OK && res!=Z_STREAM_END){
			Fatal(1,error,"deflate error");
			return 1;
		}
		if(stream_free_write_buffer(stream->compressed,len-stream->wr.avail_out)){
			return 1;
		}
	} while (stream->wr.avail_in || (flush == Z_FINISH && res!=Z_STREAM_END));
	stream->wr_ofs=0;
	return 0;
}


*/



static void gzip_close(stream_t *stream){
	if ((*stream)->wr_buf) {
		int res;
		do {
			res=deflate(&(*stream)->wr,Z_FINISH);
			if(res!=Z_OK && res!=Z_STREAM_END){
				Fatal(1,error,"deflate error %d %s",res,zError(res));
			}
			int len=((*stream)->bufsize) - ((*stream)->wr.avail_out);
			if (len>0) {
				stream_write((*stream)->s,(*stream)->wr_buf,len);
				(*stream)->wr.next_out=(*stream)->wr_buf;
				(*stream)->wr.avail_out=(*stream)->bufsize;
			}
		} while (res!=Z_STREAM_END);
		switch(deflateEnd(&(*stream)->wr)){
		case Z_OK: break;
		default:
			Fatal(1,error,"cleanup failed");
		}
		free((*stream)->wr_buf);
	}
	if ((*stream)->rd_buf) {
		switch(inflateEnd(&(*stream)->rd)){
		case Z_OK: break;
		default:
			Fatal(1,error,"cleanup failed");
		}
		free((*stream)->rd_buf);
	}
	stream_close(&(*stream)->s);
	free (*stream);
	*stream=NULL;
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
		res=inflate(&stream->rd, Z_NO_FLUSH);
		if(res!=Z_OK && res!=Z_STREAM_END){
			if(res==Z_NEED_DICT) Fatal(1,error,"Z_NEED_DICT");
			if(res==Z_DATA_ERROR) Fatal(1,error,"Z_DATA_ERROR");
			if(res==Z_STREAM_ERROR) Fatal(1,error,"Z_STREAM_ERROR");
			if(res==Z_MEM_ERROR) Fatal(1,error,"Z_MEM_ERROR");
			if(res==Z_BUF_ERROR) Fatal(1,error,"Z_BUF_ERROR");
			Fatal(1,error,"inflate error");
		}
	} while(stream->rd.avail_out && res!=Z_STREAM_END);
	return (count-stream->rd.avail_out);
}

static void gzip_read(stream_t stream,void*buf,size_t count){
	size_t res=gzip_read_max(stream,buf,count);
	if (res<count) Fatal(0,error,"short read %d instead of %d",res,count);
}

static void gzip_write(stream_t stream,void*buf,size_t count){
	stream->wr.next_in=buf;
	stream->wr.avail_in=count;
	while(stream->wr.avail_in){
		int res=deflate(&stream->wr,Z_NO_FLUSH);
		if(res != Z_OK) {
			Fatal(1,error,"deflate error %d: %s.",res,zError(res));
		}
		if (stream->wr.avail_out==0){
			stream_write(stream->s,stream->wr_buf,stream->bufsize);
			stream->wr.next_out=stream->wr_buf;
			stream->wr.avail_out=stream->bufsize;
		}
	}
	stream->wr.next_in=Z_NULL;
}

stream_t stream_gzip(stream_t compressed,int level,int bufsize){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->bufsize=bufsize;
	s->s=compressed;
	if(stream_writable(compressed)){
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
		if (deflateInit(&s->wr, level)!= Z_OK) {
			Fatal(0,error,"gzip init failed");
		}
	} else {
		s->wr_buf=NULL;
	}
	if(stream_readable(compressed)){
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
		if (inflateInit(&s->rd)!= Z_OK) {
			Fatal(1,error,"inflateInit failed");
		}
	} else {
		s->rd_buf=NULL;
	}
	s->procs.close=gzip_close;
	return s;
}

