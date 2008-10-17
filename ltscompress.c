#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#include <libgen.h>
#include "ltsmeta.h"

static int blocksize=65536;

static void stream_copy(archive_t src,archive_t dst,char*name,char*decode,char*encode){
	stream_t is=arch_read(src,name,decode);
	stream_t os=arch_write(dst,name,encode);
	char buf[blocksize];
	for(;;){
		int len=stream_read_max(is,buf,blocksize);
		if (len) stream_write(os,buf,len);
		if(len<blocksize) break;
	}
	stream_close(&is);
	stream_close(&os);	
}

int main(int argc, char *argv[]){
	runtime_init_args(&argc,&argv);
	blocksize=prop_get_U32("bs",blocksize);
	char *appl=basename(argv[0]);
	archive_t ar_in,ar_out;
	if(argc!=3){
		Fatal(1,error,"usage %s <input> <output>",appl);
	}
	char*in_code=NULL;
	int compress=0;
	if(strcmp(appl,"ltsuncompress")==0){
		in_code="auto";
	} else if (strcmp(appl,"ltscompress")==0){
		compress=1;
	} else {
		Fatal(1,error,"basename should be ltscompress or ltsuncompress, not %s",appl);
	}
	if (strstr(argv[1],"%s")){
		ar_in=arch_fmt(argv[1],file_input,file_output,prop_get_U32("bs",blocksize));
	} else {
		ar_in=arch_gcf_read(raf_unistd(argv[1]));
	}
	if (strstr(argv[2],"%s")){
		ar_out=arch_fmt(argv[2],file_input,file_output,prop_get_U32("bs",blocksize));
	} else {
		uint32_t bc=prop_get_U32("bc",128);
		ar_out=arch_gcf_create(raf_unistd(argv[2]),blocksize,blocksize*bc,0,1);
	}
	stream_t ds;
	ds=arch_read(ar_in,"info",in_code);
	lts_t lts=lts_read(ds);
	DSclose(&ds);
	int N=lts_get_segments(lts);
	ds=arch_write(ar_out,"info",compress?"":NULL);
	lts_write_info(lts,ds,DIR_INFO);
	DSclose(&ds);
	stream_copy(ar_in,ar_out,"TermDB",in_code,compress?"gzip":NULL);
	for(int i=0;i<N;i++){
		for(int j=0;j<N;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,in_code,compress?"diff32|gzip":NULL);
			sprintf(name,"label-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,in_code,compress?"gzip":NULL);
			sprintf(name,"dest-%d-%d",i,j);
			stream_copy(ar_in,ar_out,name,in_code,compress?"diff32|gzip":NULL);
		}
	}
	arch_close(&ar_in);
	arch_close(&ar_out);
	return 0;
}


