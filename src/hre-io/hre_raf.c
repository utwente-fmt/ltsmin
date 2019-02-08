// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <hre-io/raf_object.h>
#include <hre-io/user.h>

// add pread/pwrite functions for WIN32, those in gnulib are buggy.
#ifdef _WIN32
#include <windows.h>

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
  OVERLAPPED o;
  memset(&o, 0, sizeof(OVERLAPPED));
  HANDLE fh = (HANDLE)_get_osfhandle(fd);
  uint64_t off = offset;
  DWORD bytes;
  BOOL ret;

  if (fh == INVALID_HANDLE_VALUE) {
    errno = EBADF;
    return -1;
  }

  o.Offset = off & 0xffffffff;
  o.OffsetHigh = (off >> 32) & 0xffffffff;

  ret = ReadFile(fh, buf, (DWORD)count, &bytes, &o);
  if (!ret) {
    errno = EIO;
    return -1;
  }

  return (ssize_t)bytes;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  OVERLAPPED o;
  memset(&o, 0, sizeof(OVERLAPPED));
  HANDLE fh = (HANDLE)_get_osfhandle(fd);
  uint64_t off = offset;
  DWORD bytes;
  BOOL ret;

  if (fh == INVALID_HANDLE_VALUE) {
    errno = EBADF;
    return -1;
  }

  o.Offset = off & 0xffffffff;
  o.OffsetHigh = (off >> 32) & 0xffffffff;

  ret = WriteFile(fh, buf, (DWORD)count, &bytes, &o);
  if (!ret) {
    errno = EIO;
    return -1;
  }

  return (ssize_t)bytes;
}
#endif

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

static void RAFread(raf_t raf,void*buf,size_t len,off_t ofs){
    ssize_t res=pread(raf->fd,buf,len,ofs);
    if (res<0) {
        AbortCall("could not read %s",raf->shared.name);
    }
    if (res!=(ssize_t)len) {
        AbortCall("short read %zd/%zu from %s at %zd",res,len,raf->shared.name,(ssize_t)ofs);
    }
}

static void RAFwrite(raf_t raf,void*buf,size_t len,off_t ofs){
    ssize_t res=pwrite(raf->fd,buf,len,ofs);
    if (res<0) {
        AbortCall("could not write %s",raf->shared.name);
    }
    if (res!=(ssize_t)len) {
        AbortCall("short write to %s",raf->shared.name);
    }
}

static off_t RAFsize(raf_t raf){
    struct stat info;
    if (fstat(raf->fd,&info)==-1){
        AbortCall("could not get size of %s",raf->shared.name);
    }
    return info.st_size;
}

static void RAFresize(raf_t raf,off_t size){
    if (ftruncate(raf->fd,size)==-1){
        AbortCall("could not resize %s",raf->shared.name);
    }
}

static void RAFclose(raf_t *raf){
    if (close((*raf)->fd)==-1){
        AbortCall("could not close %s",(*raf)->shared.name);
    }
    RTfree(*raf);
    *raf=NULL;
}

void raf_illegal_read(raf_t raf,void*buf,size_t len,off_t ofs){
    (void)buf;(void)len;(void)ofs;
    Abort("read not supported for raf %s",raf->shared.name);
}

void raf_illegal_write(raf_t raf,void*buf,size_t len,off_t ofs){
    (void)buf;(void)len;(void)ofs;
    Abort("write not supported for raf %s",raf->shared.name);
}

off_t raf_illegal_size(raf_t raf){
    Abort("size not supported for raf %s",raf->shared.name);
    return -1;
}

void raf_illegal_resize(raf_t raf,off_t size){
    (void)size;
    Abort("resize not supported for raf %s",raf->shared.name);
}

void raf_illegal_close(raf_t *raf){
    Abort("close not supported for raf %s",(*raf)->shared.name);
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
    if (fd==-1) AbortCall("could not open %s",name);
    raf_t raf=(raf_t)RTmalloc(sizeof(struct raf_struct_s));
    raf_init(raf,name);
    raf->fd=fd;
    raf->shared.read=RAFread;
    raf->shared.write=RAFwrite;
    raf->shared.size=RAFsize;
    raf->shared.resize=RAFresize;
    raf->shared.close=RAFclose;
    return raf;
}
