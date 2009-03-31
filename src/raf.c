#include "config.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include "amconfig.h"
#ifdef HAVE_LIBRT
#include <aio.h>
#endif
#include "raf_object.h"
#include "runtime.h"

struct raf_struct_s {
	struct raf_object shared;
	int fd;
#ifdef HAVE_LIBRT
	struct aiocb request;
	int pending;
#endif
};

void raf_read(raf_t raf,void*buf,size_t len,off_t ofs){
	raf->shared.read(raf,buf,len,ofs);
}
void raf_write(raf_t raf,void*buf,size_t len,off_t ofs){
	raf->shared.write(raf,buf,len,ofs);
}
void raf_async_write(raf_t raf,void*buf,size_t len,off_t offset){
	raf->shared.awrite(raf,buf,len,offset);
}
void raf_wait(raf_t raf){
	raf->shared.await(raf);
}
off_t raf_size(raf_t raf){
	return raf->shared.size(raf);
}
void raf_resize(raf_t raf,off_t size){
	raf->shared.resize(raf,size);
}
void raf_close(raf_t *raf){
	(*raf)->shared.close(raf);
}



static void RAFread(raf_t raf,void*buf,size_t len,off_t ofs){
	ssize_t res=pread(raf->fd,buf,len,ofs);
	if (res<0) {
		FatalCall(1,error,"could not read %s",raf->shared.name);
	}
	if (res!=(ssize_t)len) {
		FatalCall(1,error,"short read %u/%u from %s at %llu",res,len,raf->shared.name,ofs);
	}
}
static void RAFwrite(raf_t raf,void*buf,size_t len,off_t ofs){
	ssize_t res=pwrite(raf->fd,buf,len,ofs);
	if (res<0) {
		FatalCall(1,error,"could not write %s",raf->shared.name);
	}
	if (res!=(ssize_t)len) {
		FatalCall(1,error,"short write to %s",raf->shared.name);
	}
}


static void RAFwait(raf_t raf){
	(void)raf;
}


#ifdef HAVE_LIBRT

static void AIOwrite(raf_t raf,void*buf,size_t len,off_t ofs){
	if(raf->pending) {
		Fatal(1,error,"There may not be more than one asynchronous call pending.");
	}
	raf->pending=1;
	raf->request.aio_buf=buf;
	raf->request.aio_nbytes=len;
	raf->request.aio_offset=ofs;
	if (aio_write(&(raf->request))){
		FatalCall(1,error,"aio_write to %s",raf->shared.name);
	}
}
static void AIOwait(raf_t raf){
	if(raf->pending==0) return;
	const struct aiocb* list[1];
	list[0]=&(raf->request);
	// TODO check out why aio_suspend complains about 1st argument.
	if (aio_suspend((void*)list,1,NULL)){
		FatalCall(1,error,"aio_suspend for %s",raf->shared.name);
	}
	if (aio_error(list[0])){
		FatalCall(1,error,"aio_error for %s",raf->shared.name);
	}
	raf->pending=0;
}

#endif

static off_t RAFsize(raf_t raf){
	struct stat info;
	if (fstat(raf->fd,&info)==-1){
		FatalCall(1,error,"could not get size of %s",raf->shared.name);
	}
	return info.st_size;
}
static void RAFresize(raf_t raf,off_t size){
	if (ftruncate(raf->fd,size)==-1){
		FatalCall(1,error,"could not resize %s",raf->shared.name);
	}
}
static void RAFclose(raf_t *raf){
	if (close((*raf)->fd)==-1){
		FatalCall(1,error,"could not close %s",(*raf)->shared.name);
	}
	free(*raf);
	*raf=NULL;
}
void raf_illegal_read(raf_t raf,void*buf,size_t len,off_t ofs){
	(void)buf;(void)len;(void)ofs;
	Fatal(1,error,"read not supported for raf %s",raf->shared.name);
}
void raf_illegal_write(raf_t raf,void*buf,size_t len,off_t ofs){
	(void)buf;(void)len;(void)ofs;
	Fatal(1,error,"write not supported for raf %s",raf->shared.name);
}
void raf_illegal_awrite(raf_t raf,void*buf,size_t len,off_t ofs){
	(void)buf;(void)len;(void)ofs;
	Fatal(1,error,"asynchronous write not supported for raf %s",raf->shared.name);
}
void raf_illegal_await(raf_t raf){
	Fatal(1,error,"asynchronous wait not supported for raf %s",raf->shared.name);
}
off_t raf_illegal_size(raf_t raf){
	Fatal(1,error,"size not supported for raf %s",raf->shared.name);
	return -1;
}
void raf_illegal_resize(raf_t raf,off_t size){
	(void)size;
	Fatal(1,error,"resize not supported for raf %s",raf->shared.name);
}
void raf_illegal_close(raf_t *raf){
	Fatal(1,error,"close not supported for raf %s",(*raf)->shared.name);
}

void raf_init(raf_t raf,char*name){
	raf->shared.read=raf_illegal_read;
	raf->shared.write=raf_illegal_write;
	raf->shared.awrite=raf_illegal_awrite;
	raf->shared.await=raf_illegal_await;
	raf->shared.size=raf_illegal_size;
	raf->shared.resize=raf_illegal_resize;
	raf->shared.close=raf_illegal_close;
	raf->shared.name=strdup(name);
}

raf_t raf_unistd(char *name){
	int fd=open(name,O_RDWR|O_CREAT,DEFFILEMODE);
	if (fd==-1) FatalCall(1,error,"could not open %s",name);
	raf_t raf=(raf_t)RTmalloc(sizeof(struct raf_struct_s));
	raf_init(raf,name);
	raf->fd=fd;
	raf->shared.read=RAFread;
	raf->shared.write=RAFwrite;
	raf->shared.awrite=RAFwrite;
	raf->shared.await=RAFwait;
	raf->shared.size=RAFsize;
	raf->shared.resize=RAFresize;
	raf->shared.close=RAFclose;
	return raf;
}

#ifdef HAVE_LIBRT
raf_t raf_aio(char *name){
	int fd=open(name,O_RDWR|O_CREAT,DEFFILEMODE);
	if (fd==-1) FatalCall(1,error,"could not open %s",name);
	raf_t raf=(raf_t)RTmalloc(sizeof(struct raf_struct_s));
	raf_init(raf,name);
	raf->fd=fd;
	raf->shared.read=RAFread;
	raf->shared.write=RAFwrite;
	raf->shared.awrite=AIOwrite;
	raf->shared.await=AIOwait;
	raf->shared.size=RAFsize;
	raf->shared.resize=RAFresize;
	raf->shared.close=RAFclose;
	raf->request.aio_fildes=fd;
	raf->request.aio_reqprio=0;
	raf->pending=0;
	return raf;
}
#else
raf_t raf_aio(char *name){
	Fatal(1,error,"AIO not supported");
	return NULL;
}
#endif

