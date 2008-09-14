#include "config.h"
#include "runtime.h"
#include "stream.h"
#include <stdio.h>



int main(int argc,char**argv){
	runtime_init();
	set_label("stream test");
	Warning(info,"start");
	FILE*in=fopen(argv[1],"r");
	FILE*out=fopen(argv[2],"w");
	stream_t is=stream_input(in);
	stream_t os=stream_output(out);
	char buf[4096];
	for(;;){
		int len=stream_read_max(is,buf,4096);
		if(len==0) break;
		stream_write(os,buf,len);
	}
	if (!stream_empty(is)){
		fprintf(stderr,"empty inconsistent\n");
	}
	stream_close(&is);
	stream_close(&os);
	return 0;
}

