#include "raf_object.h"
#include "runtime.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct raf_struct_s {
	struct raf_object shared;
	int fd;
};

void raf_read(raf_t raf,void*buf,size_t len,off_t ofs){
	raf->shared.read(raf,buf,len,ofs);
}
void raf_write(raf_t raf,void*buf,size_t len,off_t ofs){
	raf->shared.write(raf,buf,len,ofs);
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
static void Pread(raf_t raf,void*buf,size_t len,off_t ofs){
	size_t res=pread(raf->fd,buf,len,ofs);
	if (res<0) {
		FatalCall(1,error,"could not read %s",raf->shared.name);
	}
	if (res!=len) {
		FatalCall(1,error,"short read from %s",raf->shared.name);
	}
}
static void Pwrite(raf_t raf,void*buf,size_t len,off_t ofs){
	size_t res=pwrite(raf->fd,buf,len,ofs);
	if (res<0) {
		FatalCall(1,error,"could not write %s",raf->shared.name);
	}
	if (res!=len) {
		FatalCall(1,error,"short write to %s",raf->shared.name);
	}
}
static off_t Psize(raf_t raf){
	struct stat info;
	if (fstat(raf->fd,&info)==-1){
		FatalCall(1,error,"could not get size of %s",raf->shared.name);
	}
	return info.st_size;
}
static void Presize(raf_t raf,off_t size){
	if (ftruncate(raf->fd,size)==-1){
		FatalCall(1,error,"could not resize %s",raf->shared.name);
	}
}
static void Pclose(raf_t *raf){
	if (close((*raf)->fd)==-1){
		FatalCall(1,error,"could not close %s",(*raf)->shared.name);
	}
	
}
void raf_illegal_read(raf_t raf,void*buf,size_t len,off_t ofs){
	Fatal(1,error,"read not supported for raf %s",raf->shared.name);
}
void raf_illegal_write(raf_t raf,void*buf,size_t len,off_t ofs){
	Fatal(1,error,"write not supported for raf %s",raf->shared.name);
}
off_t raf_illegal_size(raf_t raf){
	Fatal(1,error,"size not supported for raf %s",raf->shared.name);
	return -1;
}
void raf_illegal_resize(raf_t raf,off_t size){
	Fatal(1,error,"resize not supported for raf %s",raf->shared.name);
}
void raf_illegal_close(raf_t *raf){
	Fatal(1,error,"close not supported for raf %s",(*raf)->shared.name);
}

void raf_init(raf_t raf,char*name){
	raf->shared.read=raf_illegal_read;
	raf->shared.write=raf_illegal_write;
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
	raf->shared.read=Pread;
	raf->shared.write=Pwrite;
	raf->shared.size=Psize;
	raf->shared.resize=Presize;
	raf->shared.close=Pclose;
	return raf;
}

