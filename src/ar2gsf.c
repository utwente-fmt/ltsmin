#include "archive.h"
#include <stdio.h>
#include "runtime.h"
#include <libgen.h>
#include "ltsman.h"

static int blocksize=65536;
static int plain=0;
static int decode=0;

#define STR(x) XSTR(x)
#define XSTR(x) #x

struct option options[]={
#if INPUTTYPE == dir
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: " STR(INPUTTYPE) "2gsf options input [output]",NULL,NULL,NULL},
#endif
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static void stream_copy(archive_t src,archive_t dst,char*name,char*decode,char*encode){
	stream_t is=arch_read(src,name,decode);
	stream_t os=arch_write(dst,name,encode,0);
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
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	blocksize=prop_get_U32("bs",blocksize);
	char *appl=basename(argv[0]);
	archive_t ar_in,ar_out;
	if(argc!=3 && argc!=2){
		Fatal(1,error,"usage " STR(INPUTTYPE) "2gsf [options] input [output]",appl);
	}
#if INPUTTYPE == dir
	ar_in=arch_dir_open(argv[1],blocksize);
#elif INPUTTYPE == gcf
	ar_in=arch_gcf_read(raf_unistd(argv[1]));
#elif INPUTTUPE == fmt
	ar_in=arch_fmt(argv[1],file_input,file_output,prop_get_U32("bs",blocksize));
#endif
	if (argc==3) {
		ar_out=arch_gsf_write(file_output(argv[2]));
	} else {
		ar_out=arch_gsf_write(stream_output(stdout));
	}
	Warning(info,"streaming %s",argv[1]);
	stream_t ds;
	ds=arch_read(ar_in,"info",NULL);
	lts_t lts=lts_read_info(ds,&decode);
	DSclose(&ds);
	ds=arch_write(ar_out,"info",NULL,0);
	lts_write_info(lts,ds,LTS_INFO_PACKET);
	DSclose(&ds);
	int N=0;
	stream_t dsin=arch_read(ar_in,"TermDB",decode?"auto":NULL);
	ds=arch_write(ar_out,"actions",NULL,0);
	for(;;){
		char*lbl=DSreadLN(dsin);
		if (strlen(lbl)==0) break;
		//Warning(info,"label %d is %s",N,lbl);
		DSwriteS(ds,lbl);
		free(lbl);
		N++;
	}
	DSclose(&dsin);
	DSclose(&ds);
	Warning(info,"read %d labels",N);
	N=lts_get_segments(lts);
	for(int i=0;i<N;i++){
		for(int j=0;j<N;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_t src_in=arch_read(ar_in,name,decode?"auto":NULL);
			stream_t src_out=arch_write(ar_out,name,NULL,0);
			sprintf(name,"label-%d-%d",i,j);
			stream_t lbl_in=arch_read(ar_in,name,decode?"auto":NULL);
			stream_t lbl_out=arch_write(ar_out,name,NULL,0);
			sprintf(name,"dest-%d-%d",i,j);
			stream_t dst_in=arch_read(ar_in,name,decode?"auto":NULL);
			stream_t dst_out=arch_write(ar_out,name,NULL,0);
			for(;;){
				if (DSempty(src_in)) break;
				uint32_t s=DSreadU32(src_in);
				uint32_t l=DSreadU32(lbl_in);
				uint32_t d=DSreadU32(dst_in);
				DSwriteU32(src_out,s);
				DSwriteU32(lbl_out,l);
				DSwriteU32(dst_out,d);
			}

		}
	}
	arch_close(&ar_in);
	arch_close(&ar_out);
	return 0;
}


