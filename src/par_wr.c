#include "runtime.h"
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <aio.h>
#include <stdlib.h>
#include <sys/times.h>
#include <unistd.h>



int main(int argc,char*argv[]){
	runtime_init_args(&argc,&argv);
	int count=prop_get_U32("count",0);
	int size=prop_get_U32("size",0);
	int syncrate=prop_get_U32("sync",0);
	int delay=prop_get_U32("sleep",0);
	if (!(size&&count)){
		Fatal(1,error,"please define size and count");
	}
	int i,j,N=argc-1;
	int fd[N];
	for(i=0;i<N;i++){
		fd[i]=open(argv[i+1],O_RDWR|O_CREAT|O_TRUNC,DEFFILEMODE);
	}
	char buf[size];
	for(i=0;i<size;i++) buf[i]=0;
	clock_t begin=times(NULL);
	for(j=0;j<count;j++){
		for(i=0;i<N;i++){
			if (write(fd[i],buf,size)<size)  Fatal(1,error,"short write i=%d j=%d",i,j);
			if (delay) usleep(delay);
			if (syncrate && ((j*N+i)%syncrate)==0) sync();
		}
	}
	for(i=0;i<N;i++){
		close(fd[i]);
	}
	sync();
	clock_t end=times(NULL);
	int tick=sysconf(_SC_CLK_TCK);
	Warning(info,"%3.2f MB/s",((float)N * (float)count * (float)size *(float)tick)/(1048576.0 * (float)(end-begin)));
	return 0;
}

