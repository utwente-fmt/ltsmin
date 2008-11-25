#include "archive.h"
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include "dirops.h"
#include "options.h"

/********************************************************************************/

typedef struct copy_context {
	archive_t src;
	archive_t dst;
	char* decode;
	char* encode;
	int bs;
} *copy_context_t;

static int copy_item(void*arg,int id,char*name){
	(void)id;
	copy_context_t ctx=(copy_context_t)arg;
	Warning(info,"copying %s",name);
	stream_t is=arch_read(ctx->src,name,ctx->decode);
	stream_t os=arch_write(ctx->dst,name,ctx->encode,1);
	char buf[ctx->bs];
	for(;;){
		int len=stream_read_max(is,buf,ctx->bs);
		if (len) stream_write(os,buf,len);
		if(len<ctx->bs) break;
	}
	stream_close(&is);
	stream_close(&os);
	return 0;
}

static void archive_copy(archive_t src,char*decode,archive_t dst,char*encode,int blocksize){
	struct arch_enum_callbacks cb;
	cb.new_item=copy_item;
	cb.end_item=NULL;
	cb.data=NULL;
	struct copy_context ctx;
	ctx.decode=decode;
	ctx.encode=encode;
	ctx.src=src;
	ctx.dst=dst;
	ctx.bs=blocksize;
	arch_enum_t e=arch_enum(src,NULL);
	if (arch_enumerate(e,&cb,&ctx)){
		Fatal(1,error,"unexpected non-zero return");
	}
	arch_enum_free(&e);
}

/********************************************************************************/


static char* code="gzip";
static int blocksize=32768;
static int blockcount=32;
static int extract=0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: gcf <gcf> [file1 [file2 [...]]]",
		"       gcf <gcf> directory",
		"       gcf -x <gcf> <outputspec>",
		"The first form builds a GCF with the given files."},
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"The second form builds a GCF with the files in the directory.",
		"The third form extracts the files in the GCF:",
		"  * to a directory if <outputspec> is a directory",
		"  * to a pattern if <outputspec> contains %s"},
	{"-q",OPT_NORMAL,log_suppress,&info,"-q",
		"do not print info messages",NULL,NULL,NULL},
	{"-z",OPT_REQ_ARG,assign_string,&code,"-z <compression>",
		"The default compression is gzip",
		"To disable compression use -z \"\"",
		NULL,NULL},
	{"-x",OPT_NORMAL,set_int,&extract,NULL,"enable extraction",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for copying streams.",
		NULL,NULL,NULL},
	{"-bc",OPT_REQ_ARG,parse_int,&blockcount,"-bc <block count>",
		"Set the number of block in one GCF cluster.",
		NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

int main(int argc, char *argv[]){
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	take_options(options,&argc,argv);
	archive_t gcf=NULL;

	if (argc==1 || (extract && argc!=3)) {
		printoptions(options);
		exit(1);
	}
	if (extract) {
		archive_t dir;
		if (IsADir(argv[2])){
			dir=arch_dir_open(argv[2],blocksize);
		} else if (strstr(argv[2],"%s")) {
			dir=arch_fmt(argv[2],file_input,file_output,blocksize);
		} else {
			Fatal(1,error,"%s is not a directory, nor does it contain %%s",argv[2]);
		}
		gcf=arch_gcf_read(raf_unistd(argv[1]));
		archive_copy(gcf,"auto",dir,NULL,blocksize);
		arch_close(&dir);
		arch_close(&gcf);
	} else {
		gcf=arch_gcf_create(raf_unistd(argv[1]),blocksize,blocksize*blockcount,0,1);
		if (argc==3 && IsADir(argv[2])){
			archive_t dir=arch_dir_open(argv[2],blocksize);
			archive_copy(dir,NULL,gcf,code,blocksize);
			arch_close(&dir);
		} else {
			for(int i=2;i<argc;i++){
				Warning(info,"copying %s",argv[i]);
				stream_t is=file_input(argv[i]);
				stream_t os=arch_write(gcf,argv[i],code,1);
				char buf[blocksize];
				for(;;){
					int len=stream_read_max(is,buf,blocksize);
					if (len) stream_write(os,buf,len);
					if(len<blocksize) break;
				}
				stream_close(&is);
				stream_close(&os);	
			}
		}
		arch_close(&gcf);
	}
}


