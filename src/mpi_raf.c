#include "raf_object.h"
#include "runtime.h"
#include <mpi.h>
#include <stdlib.h>

struct raf_struct_s {
	struct raf_object shared;
	MPI_File f;
	MPI_Request rq;
};

static void read_at(raf_t raf,void*buf,size_t len,off_t ofs){
	MPI_File_read_at(raf->f,ofs,buf,len,MPI_CHAR,MPI_STATUS_IGNORE);
}
static void write_at(raf_t raf,void*buf,size_t len,off_t ofs){
	MPI_File_write_at(raf->f,ofs,buf,len,MPI_CHAR,MPI_STATUS_IGNORE);
}
static void Iwrite_at(raf_t raf,void*buf,size_t len,off_t ofs){
	MPI_File_iwrite_at(raf->f,ofs,buf,len,MPI_CHAR,&(raf->rq));
}
static void mpi_wait(raf_t raf){
	MPI_Wait(&(raf->rq),MPI_STATUS_IGNORE);
}

static off_t mpi_size(raf_t raf){
	MPI_Offset size;
	MPI_File_get_size(raf->f,&size);
	return size;
}
static void mpi_resize(raf_t raf,off_t size){
	MPI_File_set_size(raf->f,size);
}
static void mpi_close(raf_t *raf){
	MPI_File_close(&((*raf)->f));
	free(*raf);
	*raf=NULL;
}

raf_t MPI_Create_raf(char *name,MPI_Comm comm){
	raf_t raf=(raf_t)RTmalloc(sizeof(struct raf_struct_s));
	raf_init(raf,name);
	int e=MPI_File_open(comm,name,MPI_MODE_RDWR|MPI_MODE_CREATE,MPI_INFO_NULL,&(raf->f));
	if(e){
		int i=1024;
		char msg[1024];
		MPI_Error_string(e,msg,&i);
		Fatal(0,error,"err is %s\n",msg);
	}
	MPI_File_set_errhandler(raf->f,MPI_ERRORS_ARE_FATAL);
	raf->shared.read=read_at;
	raf->shared.write=write_at;
	raf->shared.awrite=Iwrite_at;
	raf->shared.await=mpi_wait;
	raf->shared.size=mpi_size;
	raf->shared.resize=mpi_resize;
	raf->shared.close=mpi_close;
	return raf;
}

