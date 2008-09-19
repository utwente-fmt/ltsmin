#include "config.h"
#include "runtime.h"
#include "raf.h"
#include <stdio.h>

#define BUFSIZE 512


int main(int argc,char*argv[]){
	runtime_init();
	set_label("%s",argv[0]);
	int len,i;
	char buf[BUFSIZE];
	for(i=0;i<BUFSIZE;i++) buf[i]=atoi(argv[2]);
	raf_t raf=raf_unistd(argv[1]);
	printf("size is %d\n",(int)raf_size(raf));
	raf_write(raf,buf,BUFSIZE,0);
	raf_resize(raf,atoi(argv[3]));
	raf_close(&raf);
	return 0;
}

