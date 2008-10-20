

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
#include <stdlib.h>
#include "runtime.h"

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>


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

