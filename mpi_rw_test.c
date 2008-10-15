#include "runtime.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <aio.h>
#include <malloc.h>
#include <sys/times.h>
#include <unistd.h>
#include <mpi.h>
#include <malloc.h>
#include <stdio.h>

int main(int argc,char*argv[]){
	int mpi_me,mpi_nodes;
        MPI_Init(&argc, &argv);
        MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);
        MPI_Comm_rank(MPI_COMM_WORLD, &mpi_me);
	runtime_init_args(&argc,&argv);
	set_label("mpi read/write test (%2d/%2d)",mpi_me,mpi_nodes);
	int tick=sysconf(_SC_CLK_TCK);

	uint64_t size=prop_get_U64("size",0);
	if (!size) Fatal(0,error,"missing size= argument");
	char* fmt=prop_get_S("file",NULL);
	if (!fmt) Fatal(0,error,"missing file= argument");
	int mode=prop_get_U32("mode",0);
	int write=prop_get_U32("write",0);
	uint64_t share=size/mpi_nodes;
	uint32_t bs=prop_get_U32("bs",65536);
	int N=share/bs;
	void*buf=calloc(bs,1);
	if (!buf) Fatal(0,error,"out of memory");
	memset(buf,mpi_me,bs);
	if (mpi_me==0) {
		Warning(info,"total size: %lld",size);
		Warning(info,"share: %lld",share);
		Warning(info,"block size: %d",bs);
	}
	clock_t begin,end;
	switch(mode){
	case 1:{
		MPI_File f;
		MPI_Datatype ftype;
		char filename[1024];
		snprintf(filename,1024,"%s",fmt);
		int e=MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE|MPI_MODE_RDWR,MPI_INFO_NULL,&f);
		if(e){
			char msg[1024];
			int i=1024;
			MPI_Error_string(e,msg,&i);
			printf("err is %s\n",msg);
			return 0;
		}
		uint32_t lbs=prop_get_U32("lbs",bs/2);
		if (mpi_me==0) Warning(info,"logical block size is %d",lbs);
		MPI_File_set_errhandler(f,MPI_ERRORS_ARE_FATAL);
		if (write) MPI_File_set_size(f,0);
		MPI_Type_vector(share/lbs,lbs,lbs*mpi_nodes,MPI_CHAR,&ftype);
		MPI_Type_commit(&ftype);
		MPI_File_set_view(f,lbs*mpi_me,MPI_CHAR,ftype,"native",MPI_INFO_NULL);
		MPI_Barrier(MPI_COMM_WORLD);
		if (mpi_me==0) Warning(info,"%s with %d collective requests",write?"writing":"reading",N);
		begin=times(NULL);
		MPI_Barrier(MPI_COMM_WORLD);
		for(int i=0;i<N;i++){
			if (write) MPI_File_write_all(f,buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
			else MPI_File_read_all(f,buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		if (write) MPI_File_sync(f);
		MPI_File_close(&f);
		MPI_Type_free(&ftype);
		MPI_Barrier(MPI_COMM_WORLD);
		end=times(NULL);
		if (mpi_me==0) Warning(info,"bandwidth %3.2f MB/s",((float)size * (float)tick)/(1048576.0 * (float)(end-begin)));
		break;
		}
	case 2:{
		MPI_File f;
		char filename[1024];
		snprintf(filename,1024,"%s",fmt);
		int e=MPI_File_open(MPI_COMM_WORLD,filename,MPI_MODE_CREATE|MPI_MODE_RDWR,MPI_INFO_NULL,&f);
		if(e){
			char msg[1024];
			int i=1024;
			MPI_Error_string(e,msg,&i);
			printf("err is %s\n",msg);
			return 0;
		}
		MPI_File_set_errhandler(f,MPI_ERRORS_ARE_FATAL);
		if (write) MPI_File_set_size(f,0);
		MPI_Barrier(MPI_COMM_WORLD);
		if (mpi_me==0) Warning(info,"%s with %d local requests",write?"writing":"reading",N);
		begin=times(NULL);
		MPI_Barrier(MPI_COMM_WORLD);
		for(int i=0;i<N;i++){
			if(write) MPI_File_write_at(f,((uint64_t)bs)*((uint64_t)((i*mpi_nodes)+mpi_me)),
				buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
			else MPI_File_read_at(f,((uint64_t)bs)*((uint64_t)((i*mpi_nodes)+mpi_me)),
				buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		if (write) MPI_File_sync(f);
		MPI_File_close(&f);
		MPI_Barrier(MPI_COMM_WORLD);
		end=times(NULL);
		if (mpi_me==0) Warning(info,"bandwidth %3.2f MB/s",((float)size * (float)tick)/(1048576.0 * (float)(end-begin)));
		break;
		}
	case 3:{
		MPI_File f;
		char filename[1024];
		snprintf(filename,1024,fmt,mpi_me);
		int e=MPI_File_open(MPI_COMM_SELF,filename,MPI_MODE_CREATE|MPI_MODE_RDWR,MPI_INFO_NULL,&f);
		if(e){
			char msg[1024];
			int i=1024;
			MPI_Error_string(e,msg,&i);
			printf("err is %s\n",msg);
			return 0;
		}
		MPI_File_set_errhandler(f,MPI_ERRORS_ARE_FATAL);
		if (write) MPI_File_set_size(f,0);
		MPI_Barrier(MPI_COMM_WORLD);
		if (mpi_me==0) Warning(info,"%s %d files with %d local requests",write?"writing":"reading",mpi_nodes,N);
		begin=times(NULL);
		MPI_Barrier(MPI_COMM_WORLD);
		for(int i=0;i<N;i++){
			if (write) MPI_File_write_at(f,((uint64_t)bs)*((uint64_t)i),
				buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
			else MPI_File_read_at(f,((uint64_t)bs)*((uint64_t)i),
				buf,bs,MPI_CHAR,MPI_STATUS_IGNORE);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		if (write) MPI_File_sync(f);
		MPI_File_close(&f);
		MPI_Barrier(MPI_COMM_WORLD);
		end=times(NULL);
		if (mpi_me==0) Warning(info,"bandwidth %3.2f MB/s",((float)size * (float)tick)/(1048576.0 * (float)(end-begin)));
		break;
		}
	default:
		Fatal(0,error,"invalid or missing mode= argument");
	}
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
	return 0;
}


