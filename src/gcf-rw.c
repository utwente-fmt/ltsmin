#include "archive.h"
#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include "runtime.h"
#include <dir_ops.h>

/********************************************************************************/

typedef struct copy_context {
	archive_t src;
	archive_t dst;
	char* decode;
	char* (*encode)(char*);
	int bs;
} *copy_context_t;

static int copy_item(void*arg,int id,char*name){
	(void)id;
	copy_context_t ctx=(copy_context_t)arg;
	Warning(info,"copying %s",name);
	char*compression=ctx->encode?ctx->encode(name):NULL;
	Warning(debug,"compression method is %s",compression);
	stream_t is=arch_read(ctx->src,name,ctx->decode);
	stream_t os=arch_write(ctx->dst,name,compression,1);
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

static void archive_copy(archive_t src,char*decode,archive_t dst,char*(*encode)(char*),int blocksize){
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

#define GCF_FILE 0
#define GCF_DIR 1
#define GCF_EXTRACT 2

static char* policy="gzip";
static int blocksize=32768;
static int blockcount=32;
static int operation=GCF_FILE;
static int force=0;

static  struct poptOption options[] = {
	{ "create",'c', POPT_ARG_VAL , &operation , GCF_FILE , "create a new archive (default)" , NULL },
	{ "create-dz",0, POPT_ARG_VAL , &operation , GCF_DIR , "create a compressed directory instead of an archive file" , NULL },
	{ "extract",'x', POPT_ARG_VAL , &operation , GCF_EXTRACT , "extract files from an archive" , NULL },
	{ "force",'f' ,  POPT_ARG_VAL , &force , 1 , "force creation of a directory for output" , NULL },
	{ "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "set the size of a block in bytes" , "<bytes>" },
	{ "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "set the number of blocks in a cluster" , "<blocks>"},
	{ "compression",'z',POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT,
		&policy,0,"set the compression policy used in the archive","<policy>"},
	POPT_TABLEEND
};

char *pattern[1024];
char *code[1024];
int pattern_count=0;

static void compression_policy_compile(char *policy){
	char*delim;
	while((delim=strchr(policy,';'))){
		pattern[pattern_count]=policy;
		delim[0]=0;
		char*tmp=strrchr(policy,':');
		if (!tmp){
			Fatal(1,error,"bad policy entry %s",policy);
		}
		tmp[0]=0;
		code[pattern_count]=tmp+1;
		policy=delim+1;
		Warning(debug,"rule %d: %s : %s",pattern_count,pattern[pattern_count],code[pattern_count]);
		pattern_count++;
	}
	pattern[pattern_count]="*";
	code[pattern_count]=policy;
	Warning(debug,"rule %d: %s : %s",pattern_count,pattern[pattern_count],code[pattern_count]);
	pattern_count++;
}

static char* get_compression(char*filename){
	for(int i=0;i<pattern_count;i++){
		if (!fnmatch(pattern[i],filename,0)) {
			if (strcmp(code[i],"none")) return code[i]; else return "";
		}
	}
	Fatal(1,error,"file %s doesn't match a policy rule",filename);
	return NULL;
}

int main(int argc, char *argv[]){
	char*gcf_name;
	archive_t gcf=NULL;
	RTinitPopt(&argc,&argv,options,1,-1,&gcf_name,NULL,"([-c] <gcf> (<dir>|<file>)*) | (-x <gcf> [<dir>|<pattern>])",
		"Tool for creating and extracting GCF archives\n\nOptions");
	compression_policy_compile(policy);
	if (operation==GCF_EXTRACT) {
		char *out_name=RTinitNextArg();
		archive_t dir;
		if(out_name){
			if(RTinitNextArg()){
				Fatal(1,error,"extraction uses gcf -x <gcf> [<dir>|<pattern>]");
			}
			if (strstr(out_name,"%s")) {
				dir=arch_fmt(out_name,file_input,file_output,blocksize);
			} else {
				dir=arch_dir_create(out_name,blocksize,force?DELETE_ALL:DELETE_NONE);
			}
		} else {
			dir=arch_dir_open(".",blocksize);
		}
		if (is_a_dir(gcf_name)){
			gcf=arch_dir_open(gcf_name,blocksize);
		} else {
			gcf=arch_gcf_read(raf_unistd(gcf_name));
		}
		archive_copy(gcf,"auto",dir,NULL,blocksize);
		arch_close(&dir);
		arch_close(&gcf);
	} else {
		if (operation==GCF_FILE){
			gcf=arch_gcf_create(raf_unistd(gcf_name),blocksize,blocksize*blockcount,0,1);
		} else {
			gcf=arch_dir_create(gcf_name,blocksize,force?DELETE_ALL:DELETE_NONE);
		}
		for(;;){
			char *input_name=RTinitNextArg();
			if (!input_name) break;
			if (is_a_dir(input_name)){
				Warning(info,"copying contents of %s",input_name);
				archive_t dir=arch_dir_open(input_name,blocksize);
				archive_copy(dir,NULL,gcf,get_compression,blocksize);
				arch_close(&dir);
			} else {
				Warning(info,"copying %s",input_name);
				stream_t is=file_input(input_name);
				stream_t os=arch_write(gcf,input_name,get_compression(input_name),1);
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


