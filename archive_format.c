#include "arch_object.h"
#include "misc.h"
#include "runtime.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>


struct archive_s {
	struct archive_obj procs;
	stream_create_t crd;
	stream_create_t cwr;
	int buf;
	char format[NAME_MAX];
};


static stream_t dir_read(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,archive->format,name);
	return stream_buffer(archive->crd(fname),archive->buf);
}

static stream_t dir_write(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,archive->format,name);
	return stream_buffer(archive->cwr(fname),archive->buf);
}

static void dir_close(archive_t *archive){
	free(*archive);
	*archive=NULL;
};

archive_t arch_fmt(char*format,stream_create_t crd,stream_create_t cwr,int buf){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	strncpy(arch->format,format,NAME_MAX-1);
	arch->format[NAME_MAX-1]=0;
	arch_init(arch);
	arch->procs.read=dir_read;
	arch->procs.write=dir_write;
	arch->procs.play=arch_play;
	arch->procs.close=dir_close;
	arch->crd=crd;
	arch->cwr=cwr;
	arch->buf=buf;
	return arch;
}


