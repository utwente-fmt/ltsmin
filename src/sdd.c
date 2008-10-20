#include "config.h"
#include "runtime.h"
#include "stream.h"
#include <stdio.h>

int main(int argc,char**argv){
	runtime_init_args(&argc,&argv);
	Warning(info,"start");
	char*name;
	name=prop_get_S("in",NULL);
	FILE*in;
	if(name){
		in=fopen(name,"r");
	} else {
		in=stdin;
	}
	name=prop_get_S("out",NULL);
	FILE*out;
	if (name) {
		out=fopen(name,"w");
	} else {
		out=stdout;
	}
	stream_t is=stream_input(in);
	stream_t os=stream_output(out);
	int N=prop_get_U32("bs",4096);
	char*code;
	if ((code=prop_get_S("decode",NULL))){
		Warning(info,"decoding with %s",code);
		is=stream_setup(is,code);
	}
	if ((code=prop_get_S("code",NULL))){
		Warning(info,"encoding with %s",code);
		os=stream_setup(os,code);
	}
	char buf[N];
	for(;;){
		int len=stream_read_max(is,buf,N);
		if (len) stream_write(os,buf,len);
		if(len<N) break;
	}
	Warning(info,"end");
	stream_close(&is);
	stream_close(&os);
	return 0;
}

