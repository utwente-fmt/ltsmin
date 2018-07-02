#include <hre/config.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <hre-io/user.h>

struct server_handle_s {
    int sd;
};

server_handle_t CScreateTCP(int port){
    struct sockaddr_in addr;
    int sd=socket(AF_INET,SOCK_STREAM,0);
    if(sd == -1)
    {
        AbortCall("Could not create socket");
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sd,(struct sockaddr*)(&addr),sizeof(struct sockaddr))==-1) {
        AbortCall("Could not bind socket");
    }
    if (listen(sd,4)==-1) {
        AbortCall("Could not listen on socket");
    }
    int flag=1;
    if (setsockopt(sd,IPPROTO_TCP,TCP_NODELAY,(char *) &flag,sizeof(int))){
        AbortCall("Setting TCP_NODELAY");
    }
    server_handle_t handle = HRE_NEW(hre_heap,struct server_handle_s);
    handle->sd=sd;
    return handle;
}

stream_t CSconnectTCP(const char* name,int port){
    struct  hostent *hostinfo;
    struct sockaddr_in addr;
    int sd;

    hostinfo=gethostbyname(name);
    if(!hostinfo)
    {
        Abort("DNS error");
    }
    memcpy(&addr.sin_addr,*(hostinfo->h_addr_list),hostinfo->h_length);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    sd=socket(AF_INET,SOCK_STREAM,0);
    if(sd == -1)
    {
        AbortCall("No socket");
    }
    if(connect(sd,(struct sockaddr*)(&addr),sizeof(struct sockaddr)) == -1) {
        AbortCall("Connection failed");
    }
    int flag=1;
    if (setsockopt(sd,IPPROTO_TCP,TCP_NODELAY,(char *) &flag,sizeof(int))){
        AbortCall("Setting TCP_NODELAY");
    }
    return stream_buffer(fd_stream(sd),4096);
    //return fd_stream(sd);
}


stream_t CSaccept(server_handle_t handle){
    int sd=accept(handle->sd,NULL,NULL);
    int flag=1;
    if (setsockopt(sd,IPPROTO_TCP,TCP_NODELAY,(char *) &flag,sizeof(int))){
        AbortCall("Setting TCP_NODELAY");
    }
    return stream_buffer(fd_stream(sd),4096);
    //return fd_stream(sd);
}
