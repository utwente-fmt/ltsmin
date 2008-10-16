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


struct archive_s {
	struct archive_obj procs;
	int buf;
	char dir[NAME_MAX];
};


static stream_t dir_read(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,"%s/%s",archive->dir,name);
	return stream_buffer(fs_read(fname),archive->buf);
}

static stream_t dir_write(archive_t archive,char *name){
	char fname[NAME_MAX*2+2];
	sprintf(fname,"%s/%s",archive->dir,name);
	return stream_buffer(fs_write(fname),archive->buf);
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

static int IsADir(const char *name){
   struct stat info;
   return ((stat(name,&info)==0)&&(S_ISDIR(info.st_mode)))?1:0;
   }

        
static int CreateDir(const char *pathname) {
       return mkdir(pathname, S_IRWXU|S_IRWXG|S_IRWXO);
       }

archive_t arch_dir(char*dirname,int buf){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	arch_init(arch);
	if(!IsADir(dirname)){
		if(CreateDir(dirname)){
			Fatal(0,error,"could not create directory %s",dirname);
			return NULL;
		}
	}
	strncpy(arch->dir,dirname,NAME_MAX-1);
	arch->dir[NAME_MAX-1]=0;
	arch->procs.read=dir_read;
	arch->procs.write=dir_write;
	arch->procs.list=dir_list;
	arch->procs.play=arch_play;
	arch->procs.close=dir_close;
	arch->buf=buf;
	return arch;
}


