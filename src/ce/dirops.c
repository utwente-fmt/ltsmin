#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "dirops.h"
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

/* printable comments
 *
 * defining CPRINTF(args...) printf(args)
 * will make them visible.
 */
//#define CPRINTF(args...) printf(args)

#ifdef SGI
#define CPRINTF //
#else
#define CPRINTF(args...)
#endif

int CreateEmptyDir(char *name,int delete){
	struct stat info;

	if (lstat(name,&info)==-1){
		switch(errno){
			default:
				return -1;
			case ENOENT:
				CPRINTF("%s does not exist creating now...\n",name);
				return mkdir(name,S_IRWXU|S_IRWXG|S_IRWXO);
		}
	}
	if (S_ISREG(info.st_mode)){
		CPRINTF("%s is a regular file\n",name);
		if (!(delete&DELETE_FILE)) {
			errno=EPERM;
			return -1;
		}
		if(unlink(name)==-1){
			return -1;
		}
		return mkdir(name,S_IRWXU|S_IRWXG|S_IRWXO);
	}
	if (S_ISDIR(info.st_mode)){
		DIR *dir;
		struct dirent *file;
		char fname[NAME_MAX*2+2];

		CPRINTF("%s is a directory\n",name);
		if (!(delete&DELETE_DIR)) {
			errno=EPERM;
			return -1;
		}
		dir=opendir(name);
		if (dir==NULL) return -1;
		while((file=readdir(dir))){
			CPRINTF("entry %s\n",file->d_name);
			if(!strcmp(file->d_name,".")) continue;
			if(!strcmp(file->d_name,"..")) continue;
			sprintf(fname,"%s/%s",name,file->d_name);
			CPRINTF("unlinking %s\n",fname);
			if(unlink(fname)==-1) return -1;
		}
		return (errno==0)?0:-1;
	}
	CPRINTF("%s is unknown type\n",name);
	errno=EINVAL;
	return -1;
}

int IsADir(char *name){
	struct stat info;
	if ((lstat(name,&info)==0)&&(S_ISDIR(info.st_mode))) return 1 ; else return 0;
}
