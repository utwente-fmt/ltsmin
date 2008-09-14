

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
#include "runtime.h"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>


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

int serversocket(int port) {
	int sd;
	struct sockaddr_in addr;

	sd=socket(AF_INET,SOCK_STREAM,0);
	if(sd == -1)
	{
		FatalCall(1,error,"No socket");
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(sd,&addr,sizeof(struct sockaddr))==-1) {
		FatalCall(1,error,"Bad Bind");
	}
	if (listen(sd,4)==-1) {
		FatalCall(1,error,"Bad Listen");
	}
	return sd;	
}


int clientsocket(char *hostname,int port){
	struct  hostent *hostinfo;
	struct sockaddr_in addr;
	int sd;

	hostinfo=gethostbyname(hostname);
        if(!hostinfo)
        {
                Fatal(1,error,"DNS error");
        }
	memcpy(&addr.sin_addr,*(hostinfo->h_addr_list),hostinfo->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	sd=socket(AF_INET,SOCK_STREAM,0);
	if(sd == -1)
	{
		FatalCall(1,error,"No socket");
	}
	if(connect(sd,&addr,sizeof(struct sockaddr)) == -1) {
		FatalCall(1,error,"Connection failed");
	}
	return sd;
}

struct listener {
	int sd;
	void(*setup)(int sd,void*arg);
	void *arg;
};

static void* tcp_listener(void *arg){
#define l ((struct listener*)arg)
	int client;
	set_label("tcp_listener");
	for(;;){
		client=accept(l->sd,NULL,NULL);
		if (client<0 && errno!=EAGAIN){
			FatalCall(1,error,"accept");
		}
		l->setup(client,l->arg);
	}
	return NULL;
#undef l
}

void tcp_listen(pthread_t *thr,int port,void(*setup)(int sd,void*arg),void*arg){
	int e;
	struct listener *l=RTmalloc(sizeof(struct listener));
	l->sd=serversocket(port);
	l->setup=setup;
	l->arg=arg;
	if ((e=pthread_create(thr,NULL,tcp_listener,l))){
		errno=e;
		FatalCall(1,error,"creation of listener thread");
	}
}	

