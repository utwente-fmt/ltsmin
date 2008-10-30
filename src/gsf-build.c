#include "archive.h"
#include <stdio.h>
#include "runtime.h"



int main(int argc, char *argv[]){
	runtime_init_args(&argc,&argv);
	archive_t dir=NULL;

	char *ar;
	int blocksize=prop_get_U32("bs",4096);
	ar=prop_get_S("gsf",NULL);
	if(ar==NULL){
		dir=arch_gsf_write(stream_output(stdout));
	} else {
		dir=arch_gsf_write(file_output(ar));
	}
	Warning(info,"copying files to %s",ar?ar:"stdout");
	char *code;
	if ((code=prop_get_S("code",NULL))){
		Warning(info,"applying %s to output streams",code);
	}
	for(int i=1;i<argc;i++){
		Warning(info,"writing %s",argv[i]);
		stream_t is=file_input(argv[i]);
		stream_t os=arch_write(dir,argv[i],code,0);
		char buf[blocksize];
		for(;;){
			int len=stream_read_max(is,buf,blocksize);
			if (len) stream_write(os,buf,len);
			if(len<blocksize) break;
		}
		stream_close(&is);
		stream_close(&os);	
	}
	arch_close(&dir);
}


