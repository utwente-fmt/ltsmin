#include "config.h"
#include "runtime.h"
#include "aio_kernel.h"
#include "misc.h"
#include <unistd.h>
#include <stdlib.h>
#include <aio.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/socket.h>

//aio_kernel_t k;

void* server(void*arg){
	int sd=(int)arg;
	char buf[1024];
	int lowwat=-1;
	if (setsockopt(sd,SOL_SOCKET,SO_RCVLOWAT,&lowwat,sizeof(lowwat))){
		FatalCall(0,error,"opt-rcv");
	}
	struct aiocb request;
	const struct aiocb* list[1];
	list[0]=&request; 
	request.aio_fildes=sd;
	request.aio_buf=buf;
	request.aio_nbytes=4;
	request.aio_offset=0;
	request.aio_reqprio=0;
	if (aio_read(&request)){
		perror("aio_read");
	}
	printf("request submitted\n");
	if (aio_suspend(list,1,NULL)){
		perror("aio_suspend");
	}
	switch(aio_error(list[0])){
	case EINPROGRESS:
		printf("not finished\n");
		break;
	case ECANCELED:
		printf("cancelled\n");
		break;
	case 0:
		printf("succes\n");
		break;
	default:
		perror("aio_error");
		break;
	}
	int len=aio_return(&request);
	write(sd,buf,len);
	close(sd);
	return NULL;
}

void setup_echo(int sd,void*arg){
	Warning(info,"got new connection");
	pthread_t thr;
	pthread_create(&thr, NULL, server, (void*)sd);
};

int main(int argc,char**argv){
	pthread_t thr;
	runtime_init();
	set_label("echo_server");
	//k=aio_kernel();
	tcp_listen(&thr,atoi(argv[1]),setup_echo,NULL);
	pthread_join(thr,NULL);
}

