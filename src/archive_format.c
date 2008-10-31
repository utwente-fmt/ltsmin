#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include "arch_object.h"
#include "runtime.h"

struct archive_s {
	struct archive_obj procs;
	stream_create_t crd;
	stream_create_t cwr;
	int buf;
	char format[LTSMIN_PATHNAME_MAX];
};


static stream_t dir_read(archive_t archive,char *name){
	char fname[LTSMIN_PATHNAME_MAX*2+2];
	sprintf(fname,archive->format,name);
	return stream_buffer(archive->crd(fname),archive->buf);
}

static stream_t dir_write(archive_t archive,char *name){
	char fname[LTSMIN_PATHNAME_MAX*2+2];
	sprintf(fname,archive->format,name);
	return stream_buffer(archive->cwr(fname),archive->buf);
}

static void dir_close(archive_t *archive){
	free(*archive);
	*archive=NULL;
};

archive_t arch_fmt(char*format,stream_create_t crd,stream_create_t cwr,int buf){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	strncpy(arch->format,format,LTSMIN_PATHNAME_MAX-1);
	arch->format[LTSMIN_PATHNAME_MAX-1]=0;
	arch_init(arch);
	arch->procs.read=dir_read;
	arch->procs.write=dir_write;
	arch->procs.close=dir_close;
	arch->crd=crd;
	arch->cwr=cwr;
	arch->buf=buf;
	return arch;
}


