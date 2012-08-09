// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <hre/dir_ops.h>
#include <hre/user.h>

/* printable comments
 *
 * defining CPRINTF(args...) printf(args)
 * will make them visible.
 */
//#define CPRINTF(args...) printf(args)

#define CPRINTF(args...)

int create_empty_dir(const char *name,int delete){
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
        char fname[LTSMIN_PATHNAME_MAX*2+2];

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
        // this is apparently wrong: return (errno==0)?0:-1; but why was it there in the first place?
        return 0;
    }
    CPRINTF("%s is unknown type\n",name);
    errno=EINVAL;
    return -1;
}

int is_a_dir(const char *name){
    struct stat info;
    if ((lstat(name,&info)==0)&&(S_ISDIR(info.st_mode))) return 1 ; else return 0;
}

int is_a_file(const char *name){
    struct stat info;
    if ((lstat(name,&info)==0)&&(S_ISREG(info.st_mode))) return 1 ; else return 0;
}


dir_enum_t get_dir_enum(const char *name){
    DIR *dir=opendir(name);
    if (dir==NULL) {
        AbortCall("opendir failed");
    }
    return dir;
}

char* get_next_dir(dir_enum_t dir){
    struct dirent *file;
    while ((file=readdir(dir))) {
        if(!strcmp(file->d_name,".")) continue;
        if(!strcmp(file->d_name,"..")) continue;
        return file->d_name;
    }
    closedir(dir);
    return NULL;
}

void del_dir_enum(dir_enum_t dir){
    closedir(dir);
}

void recursive_erase(const char *name){
    if (is_a_file(name)){
        if(unlink(name)==-1) AbortCall("could not erase %s",name);
    } else if (is_a_dir(name)){
        DIR *dir=opendir(name);
        if (dir==NULL) {
            AbortCall("could not open directory %s",name);
        }
        struct dirent *file;
        while((file=readdir(dir))){
            char fname[LTSMIN_PATHNAME_MAX];
            if(!strcmp(file->d_name,".")) continue;
            if(!strcmp(file->d_name,"..")) continue;
            sprintf(fname,"%s/%s",name,file->d_name);
            recursive_erase(fname);
        }
        if(unlink(name)==-1) AbortCall("could not remove %s",name);
    } else {
        Abort("cannot erase %s: neither a file nor a directory",name);
    }
}

