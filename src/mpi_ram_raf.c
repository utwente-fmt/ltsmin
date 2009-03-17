#include "raf_object.h"
#include "runtime.h"
#include <mpi.h>
#include <stdlib.h>
#include "mpi_core.h"

struct raf_struct_s {
	struct raf_object shared;
	uint32_t blocksize;
	void* rqbuf;
	int rqlen;
	void* data;
	MPI_Offset size;
	int rank;
	int workers;
	int rq_tag;
	int ack_tag;
};

struct read_request {
	off_t ofs;
	size_t len;
};

static void read_at(raf_t raf,void*buf,size_t len,off_t ofs){
	uint64_t ofs_block=ofs/(raf->blocksize);
	if (((uint64_t)ofs+len)>((ofs_block+(uint32_t)1)*(raf->blocksize))) Fatal(0,error,"read not within one block");
	raf->rqbuf=buf;
	raf->rqlen=len;
	uint64_t where=ofs_block%(raf->workers);
	struct read_request rq;
	rq.ofs=((ofs_block/(raf->workers))*(raf->blocksize))+(ofs-(ofs_block*(raf->blocksize)));
	rq.len=len;
	//Warning(info,"sending request %llx %d to %d as %llx (%x)",ofs,len,where,rq.ofs,(raf->blocksize));
	MPI_Send(&rq,sizeof(struct read_request),MPI_CHAR,where,raf->rq_tag,MPI_COMM_WORLD);
	core_wait(raf->ack_tag);
	//Warning(info,"request completed");
}

static void request_service(void *arg,MPI_Status*probe_status){
#define raf ((raf_t)arg)
	(void)probe_status;
	MPI_Status status;
	struct read_request rq;
	MPI_Recv(&rq,sizeof(struct read_request),MPI_CHAR,MPI_ANY_SOURCE,raf->rq_tag,MPI_COMM_WORLD,&status);
	//Warning(info,"got request for %x %d from %d",(int)rq.ofs,(int)rq.len,status.MPI_SOURCE);
	MPI_Request request;
	MPI_Isend((raf->data)+(rq.ofs),rq.len,MPI_CHAR,status.MPI_SOURCE,raf->ack_tag,MPI_COMM_WORLD,&request);
	for(;;){
		core_yield();
		int ready;
		MPI_Test(&request,&ready,MPI_STATUS_IGNORE);
		if (ready) break;
	}
	//Warning(info,"finished request for %x %d",(int)rq.ofs,(int)rq.len);
#undef raf
}

static void receive_service(void *arg,MPI_Status*probe_status){
#define raf ((raf_t)arg)
	(void)probe_status;
	MPI_Status status;
	MPI_Recv(raf->rqbuf,raf->rqlen,MPI_CHAR,MPI_ANY_SOURCE,raf->ack_tag,MPI_COMM_WORLD,&status);
	//Warning(info,"got data");
#undef raf
}

static off_t mpi_size(raf_t raf){
	return raf->size;
}

static void mpi_close(raf_t *raf){
	free((*raf)->data);
	free(*raf);
	*raf=NULL;
}

raf_t MPI_Load_raf(char *name,MPI_Comm comm){
	raf_t raf=(raf_t)RTmalloc(sizeof(struct raf_struct_s));
	raf_init(raf,name);
	raf->blocksize=65536;
	MPI_File f;
	MPI_Comm_size(comm,&(raf->workers));
	MPI_Comm_rank(comm,&(raf->rank));
	int e=MPI_File_open(comm,name,MPI_MODE_RDONLY,MPI_INFO_NULL,&f);
	if(e){
		int i=1024;
		char msg[1024];
		MPI_Error_string(e,msg,&i);
		Fatal(0,error,"err is %s\n",msg);
	}
	MPI_File_set_errhandler(f,MPI_ERRORS_ARE_FATAL);
	MPI_File_get_size(f,&(raf->size));
	if ((raf->size)%(raf->blocksize)) Fatal(0,error,"file not multiple of block size");
	if (((raf->size)/(raf->blocksize))%(raf->workers)) Fatal(0,error,"block count not multiple of worker count");
	//Warning(info,"my share is %d",(raf->size)/(raf->workers));
	raf->data=RTmalloc((raf->size)/(raf->workers));
	if (1) {
		Warning(info,"using MPI_File_read_all");
		MPI_Datatype ftype;
		MPI_Type_vector((raf->size)/(raf->blocksize),(raf->blocksize),(raf->blocksize)*(raf->workers),MPI_CHAR,&ftype);
		MPI_Type_commit(&ftype);
		MPI_File_set_view(f,(raf->blocksize)*(raf->rank),MPI_CHAR,ftype,"native",MPI_INFO_NULL);
		MPI_File_read_all(f,raf->data,(raf->size)/(raf->workers),MPI_CHAR,MPI_STATUS_IGNORE);
		MPI_File_close(&f);
		MPI_Type_free(&ftype);
	} else {
		Warning(info,"using MPI_File_read_at");
		int blockcount=((raf->size)/(raf->blocksize))/(raf->workers);
		for(int i=0;i<blockcount;i++){
			MPI_File_read_at(f,((i*(raf->workers)+(raf->rank))*(raf->blocksize)),
				(raf->data)+(i*(raf->blocksize)),(raf->blocksize),MPI_CHAR,MPI_STATUS_IGNORE);
		}
		MPI_File_close(&f);
	}
	raf->rq_tag=core_add(raf,request_service);
	raf->ack_tag=core_add(raf,receive_service);
	raf->shared.read=read_at;
	raf->shared.size=mpi_size;
	raf->shared.close=mpi_close;
	//Warning(info,"file loaded");
	return raf;
}

