#include "config.h"
#include "runtime.h"
#include "raf.h"
#include <stdio.h>

int main(int argc,char*argv[]){
	runtime_init_args(&argc,&argv);
	set_label("%s",argv[0]);
	Warning(info,"testing");
	uint32_t BUFSIZE=prop_get_U32("bs",512);
	int len,i;
	char buf[BUFSIZE];
	int val=prop_get_U32("val",0);
	for(i=0;i<BUFSIZE;i++) buf[i]=val;
	char*name=prop_get_S("raf",NULL);
	if (name==NULL) {
		Fatal(0,error,"please declare raf=file");
	}
	raf_t raf=raf_unistd(name);
	printf("size is %d\n",(int)raf_size(raf));
	raf_write(raf,buf,BUFSIZE,0);
	uint32_t fs=prop_get_U32("fs",BUFSIZE);
	raf_resize(raf,fs);
	raf_close(&raf);
	return 0;
}

