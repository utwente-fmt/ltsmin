#include "mpi_io_stream.h"
#include "stream_object.h"
#include "runtime.h"
#include <mpi.h>
#include <stdlib.h>

struct stream_s {
	struct stream_obj procs;
	MPI_File f;
};


static void file_read(stream_t stream,void*buf,size_t count){
	MPI_File_read(stream->f,buf,count,MPI_CHAR,MPI_STATUS_IGNORE);
}

static size_t file_read_max(stream_t stream,void*buf,size_t count){
	MPI_File_read(stream->f,buf,count,MPI_CHAR,MPI_STATUS_IGNORE);
	return count;
}

static void file_write(stream_t stream,void*buf,size_t count){
	MPI_File_write(stream->f,buf,count,MPI_CHAR,MPI_STATUS_IGNORE);
}

static void file_close(stream_t *stream){
	MPI_File_close(&((*stream)->f));
	free(*stream);
	*stream=NULL;
}

static void file_flush(stream_t stream){
	(void)stream;
}

stream_t mpi_io_read(char *name){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	int e=MPI_File_open(MPI_COMM_SELF,name,MPI_MODE_RDONLY,MPI_INFO_NULL,&(s->f));
	if(e){
		int i=1024;
		char msg[1024];
		MPI_Error_string(e,msg,&i);
		Fatal(0,error,"err is %s\n",msg);
	}
	MPI_File_set_errhandler(s->f,MPI_ERRORS_ARE_FATAL);
	s->procs.read_max=file_read_max;
	s->procs.read=file_read;
	s->procs.close=file_close;
	return s;
}

stream_t mpi_io_write(char*name){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	int e=MPI_File_open(MPI_COMM_SELF,name,MPI_MODE_WRONLY|MPI_MODE_CREATE,MPI_INFO_NULL,&(s->f));
	if(e){
		int i=1024;
		char msg[1024];
		MPI_Error_string(e,msg,&i);
		Fatal(0,error,"err is %s\n",msg);
	}
	MPI_File_set_errhandler(s->f,MPI_ERRORS_ARE_FATAL);
	MPI_File_set_size(s->f,0);
	s->procs.write=file_write;
	s->procs.flush=file_flush;
	s->procs.close=file_close;
	return s;
}

