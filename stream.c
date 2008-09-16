
#include "stream_object.h"
#include "runtime.h"
#include <malloc.h>

struct stream_s {
	struct stream_obj procs;
	FILE *f;
};


/*************************************************************************/
/* Wrapper functions.                                                    */
/*************************************************************************/


void stream_read(stream_t stream,void* buf,size_t count){
	stream->procs.read(stream,buf,count);
}

size_t stream_read_max(stream_t stream,void* buf,size_t count){
	return stream->procs.read_max(stream,buf,count);
}

int stream_empty(stream_t stream){
	return stream->procs.empty(stream);
}

void stream_write(stream_t stream,void* buf,size_t count){
	stream->procs.write(stream,buf,count);
}

void stream_flush(stream_t stream){
	stream->procs.flush(stream);
}

void stream_close(stream_t *stream){
	(*stream)->procs.close(stream);
}

int stream_readable(stream_t stream){
	return (stream->procs.read!=stream_illegal_io_op);
}

int stream_writable(stream_t stream){
	return (stream->procs.write!=stream_illegal_io_op);
}


/*************************************************************************/
/* FILE IO functions.                                                    */
/*************************************************************************/

void stream_illegal_op(stream_t *stream){
	Fatal(0,error,"illegal operation");
}

void stream_illegal_void(stream_t stream){
	Fatal(0,error,"illegal operation");
}

int stream_illegal_int(stream_t stream){
	Fatal(0,error,"illegal operation");
	return 0;
}

void stream_illegal_io_op(stream_t stream,void*buf,size_t count){
	Fatal(0,error,"illegal operation");
}

size_t stream_illegal_io_try(stream_t stream,void*buf,size_t count){
	Fatal(0,error,"illegal operation");
	return 0;
}

static void file_read(stream_t stream,void*buf,size_t count){
	size_t res=fread(buf,1,count,stream->f);
	if (res<count) Fatal(0,error,"short read");
}

static size_t file_read_max(stream_t stream,void*buf,size_t count){
	size_t res=fread(buf,1,count,stream->f);
	return res;
}

static int file_empty(stream_t stream){
	int c=fgetc(stream->f);
	if (c==EOF) {
		return 1;
	} else {
		if (ungetc(c,stream->f)==EOF){
			Fatal(0,error,"unexpected failure");
		};
		return 0;
	}
}

static void file_write(stream_t stream,void*buf,size_t count){
	size_t res=fwrite(buf,1,count,stream->f);
	if (res<count) Fatal(0,error,"short write");
}

static void file_close(stream_t *stream){
	fclose((*stream)->f);
	free(*stream);
	*stream=NULL;
}

static void file_flush(stream_t stream){
	fflush(stream->f);
}

stream_t stream_input(FILE*f){
	stream_t s=(stream_t)malloc(sizeof(struct stream_s));
	if (s==NULL){
		Fatal(0,error,"out of memory");
		return NULL;
	}
	setbuf(f,NULL);
	s->f=f;
	s->procs.read_max=file_read_max;
	s->procs.read=file_read;
	s->procs.empty=file_empty;
	s->procs.write=stream_illegal_io_op;
	s->procs.flush=stream_illegal_void;
	s->procs.close=file_close;
	return s;
}

stream_t fs_read(char *name){
	FILE*f=fopen(name,"r");
	if (f==NULL) Fatal(0,error,"failed to open %s for reading",name);
	return stream_input(f);
}

stream_t stream_output(FILE*f){
	stream_t s=(stream_t)malloc(sizeof(struct stream_s));
	if (s==NULL){
		Fatal(0,error,"out of memory");
		return s;
	}
	setbuf(f,NULL);
	s->f=f;
	s->procs.read_max=stream_illegal_io_try;
	s->procs.read=stream_illegal_io_op;
	s->procs.empty=stream_illegal_int;
	s->procs.write=file_write;
	s->procs.flush=file_flush;
	s->procs.close=file_close;
	return s;
}

stream_t fs_write(char *name){
	FILE*f=fopen(name,"w");
	if (f==NULL) Fatal(0,error,"failed to open %s for writing",name);
	return stream_output(f);
}


