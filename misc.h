#ifndef MISC_H
#define MISC_H

#include "config.h"
#include <pthread.h>

int serversocket(int port);
void tcp_listen(pthread_t *thr,int port,void(*setup)(int sd,void*arg),void*arg);
int clientsocket(char *hostname,int port);


#endif

