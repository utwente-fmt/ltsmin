#include "archive.h"
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include <dir_ops.h>

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


static  struct poptOption options[] = {
	{ "extract",'x', POPT_ARG_VAL , &extract , 1 , "Extract files from an archive" , NULL },
	{ "create",'c', POPT_ARG_VAL , &extract , 0 , "Create a new archive (default)" , NULL },
	{ "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "The size of a block in bytes" , "<bytes>" },
	{ "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "The number of block in a cluster" , "<blocks>"},
	{ "compression",'z',POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
		&code,0,"Set the compression used in the archive. To disable compression use none","<compression>"},
	POPT_TABLEEND
};

int main(int argc, char *argv[]){
	char*gcf_name;
	archive_t gcf=NULL;
	RTinitPopt(&argc,&argv,options,1,-1,&gcf_name,NULL,"([-c] <gcf> (<dir>|<file>)*) | (-x <gcf> [<dir>|<pattern>])",
		"Tool for creating and extracting GCF archives\n\nOptions");
	if (!strcmp(code,"none")) code="";
	if (extract) {
		char *out_name=RTinitNextArg();
		archive_t dir;
		if(out_name){
			if(RTinitNextArg()){
				Fatal(1,error,"extraction uses gcf -x <gcf> [<dir>|<pattern>]");
			}
			if (strstr(out_name,"%s")) {
				dir=arch_fmt(out_name,file_input,file_output,blocksize);
			} else {
				dir=arch_dir_create(out_name,blocksize,DELETE_NONE);
			}
		} else {
			dir=arch_dir_open(".",blocksize);
		}
		gcf=arch_gcf_read(raf_unistd(gcf_name));
		archive_copy(gcf,"auto",dir,NULL,blocksize);
		arch_close(&dir);
		arch_close(&gcf);
	} else {
		gcf=arch_gcf_create(raf_unistd(gcf_name),blocksize,blocksize*blockcount,0,1);
		for(;;){
			char *input_name=RTinitNextArg();
			if (!input_name) break;
			if (is_a_dir(input_name)){
				Warning(info,"copying contents of %s",input_name);
				archive_t dir=arch_dir_open(argv[2],blocksize);
				archive_copy(dir,NULL,gcf,code,blocksize);
				arch_close(&dir);
			} else {
				Warning(info,"copying %s",input_name);
				stream_t is=file_input(input_name);
				stream_t os=arch_write(gcf,input_name,code,1);
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


