

#include "misc.h"
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

int IsADir(const char *name){
   struct stat info;
   return ((stat(name,&info)==0)&&(S_ISDIR(info.st_mode)))?1:0;
   }

int IsAReg(const char *name){
   struct stat info;
   return ((stat(name,&info)==0)&&(S_ISREG(info.st_mode)))?1:0;
   }

int CreateDir(const char *pathname) {
   return mkdir(pathname, S_IRWXU|S_IRWXG|S_IRWXO);
   }


static int cmpstringp(const void *p1, const void *p2)
       {
           /* The actual arguments to this function are "pointers to
              pointers to char", but strcmp() arguments are "pointers
              to char", hence the following cast plus dereference */

           return strcmp(* (char * const *) p1, * (char * const *) p2);
       }

static int ForEachFile(const char *path, EachFile eachFile, int directory) {
   struct dirent *file;
   char fname[NAME_MAX*2+2];
   int cnt = 0, i =0;
   char **ptr;
   DIR *dir=opendir(path);
   if (dir==NULL) return -1;
   while((file=readdir(dir))){
//      CPRINTF("entry %s\n",file->d_name);
      if(!strcmp(file->d_name,".")) continue;
      if(!strcmp(file->d_name,"..")) continue;
      sprintf(fname,"%s/%s",path ,file->d_name);
      if (directory?IsADir(fname):IsAReg(fname)) cnt++;
      }
   if (eachFile) {
      ptr = malloc(sizeof(char*)*cnt);
      rewinddir(dir);
      while ((file=readdir(dir))) {
//         CPRINTF("entry %s\n",file->d_name);
         if(!strcmp(file->d_name,".")) continue;
         if(!strcmp(file->d_name,"..")) continue;
         sprintf(fname,"%s/%s",path ,file->d_name);
         if (directory?IsADir(fname):IsAReg(fname)) {     
           ptr[i++]=strdup(fname);
           }
         }
      qsort(ptr, cnt, sizeof(char*), cmpstringp);
         for (i=0;i<cnt;i++) {
           int err= eachFile(ptr[i]);
           free(ptr[i]);
           if (err<0) return err;
           }
      free(ptr);
   }
   closedir(dir);
   return cnt;
   }
   
int ForEachFileInDir(const char *path, EachFile eachFile) {
  return ForEachFile(path, eachFile, 0);
  }
  
int ForEachDirInDir(const char *path, EachFile eachDir) {
  return ForEachFile(path, eachDir, 1);
  }

