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
	char dir[NAME_MAX];
};


static stream_t dir_read(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,"%s/%s",archive->dir,name);
	return stream_input(fopen(fname,"r"));
}

static stream_t dir_write(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,"%s/%s",archive->dir,name);
	return stream_output(fopen(fname,"w"));
}

static void dir_list(archive_t archive,char *regex,string_enum_t cb,void*arg){
	DIR *dir=opendir(archive->dir);
	if (dir==NULL) {
		FatalCall(0,error,"opendir failed");
	}
	struct dirent *file;
	while ((file=readdir(dir))) {
		if(!strcmp(file->d_name,".")) continue;
		if(!strcmp(file->d_name,"..")) continue;
		cb(arg,file->d_name);
	}
	closedir(dir);
}

static void dir_close(archive_t *archive){
	free(*archive);
	*archive=NULL;
};

archive_t arch_dir(char*dirname){
	if(!IsADir(dirname)){
		if(CreateDir(dirname)){
			Fatal(0,error,"could not create directory %s",dirname);
			return NULL;
		}
	}
	archive_t arch=(archive_t)malloc(sizeof(struct archive_s));
	if (arch==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	strncpy(arch->dir,dirname,NAME_MAX-1);
	arch->dir[NAME_MAX-1]=0;
	arch->procs.read=dir_read;
	arch->procs.write=dir_write;
	arch->procs.list=dir_list;
	arch->procs.play=arch_play;
	arch->procs.close=dir_close;
	return arch;
}


