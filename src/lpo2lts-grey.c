#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "archive.h"
#include "runtime.h"
#include "mcrl-greybox.h"
#include "chunk-table.h"
#include "treedbs.h"
#include "ltsmeta.h"
#include "options.h"

static char *outputarch=NULL;
//static int plain=0;

struct option options[]={
	{"-out",OPT_REQ_ARG,assign_string,&outputarch,"-out <archive>",
		"Specifiy the name of the output archive.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
//	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
//		"disable compression of the output",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static int label_count=0;
static lts_t lts;
static stream_t termdb_stream;
static stream_t src_stream;
static stream_t lbl_stream;
static stream_t dst_stream;

chunk_table_t CTcreate(char *name){
	if (!strcmp(name,"action")) {
		return (void*)1;
	} else {
		return NULL;
	}
}

void CTsubmitChunk(chunk_table_t table,size_t len,void* chunk,chunk_add_t cb,void* context){
	if ((int)table) {
		if(!strcmp(chunk,"tau")) {
			lts_set_tau(lts,label_count);
		}
		if(!strcmp(chunk,"\"tau\"")) {
			lts_set_tau(lts,label_count);
		}
		DSwrite(termdb_stream,chunk,len);
		DSwrite(termdb_stream,"\n",1);
		label_count++;
	}
	cb(context,len,chunk);
}

void CTupdateTable(chunk_table_t table,uint32_t wanted,chunk_add_t cb,void* context){
	(void)table;(void)wanted;(void)cb;(void)context;
}

static int N;
static int K;
static int visited=1;
static int explored=0;
static int trans=0;

void print_next(void*arg,int*lbl,int*dst){
	int tmp[N+N];
	for(int i=0;i<N;i++) {
		tmp[N+i]=dst[i];
	}
	Fold(tmp);
	trans++;
	DSwriteU32(src_stream,*((int*)arg));
	DSwriteU32(lbl_stream,lbl[0]);
	DSwriteU32(dst_stream,tmp[1]);
	if (tmp[1]>=visited) visited=tmp[1]+1;
}

int main(int argc, char *argv[]){
	void* stackbottom=&argv;
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	MCRLinitGreybox(argc,argv,stackbottom);
	parse_options(options,argc,argv);
	GBcreateModel(argv[argc-1]);
	N=GBgetStateLength(NULL);
	K=GBgetGroupCount(NULL);
	TreeDBSinit(N,1);
	Warning(info,"length is %d",N);
	int src[N+N];
	GBgetInitialState(NULL,src+N);
	archive_t arch;
	if (!outputarch) Fatal(1,error,"please specify the output archive with -out");
	if (strstr(outputarch,"%s")) {
		arch=arch_fmt(outputarch,file_input,file_output,prop_get_U32("bs",65536));
	} else {
		uint32_t bs=prop_get_U32("bs",65536);
		uint32_t bc=prop_get_U32("bc",128);
		arch=arch_gcf_create(raf_unistd(outputarch),bs,bs*bc,0,1);
	}
	lts=lts_create();
	lts_set_root(lts,0);
	Fold(src);
	termdb_stream=arch_write(arch,"TermDB","gzip");
	src_stream=arch_write(arch,"src-0-0","diff32|gzip");
	lbl_stream=arch_write(arch,"label-0-0","gzip");
	dst_stream=arch_write(arch,"dest-0-0","diff32|gzip");
	while(explored<visited){
		src[1]=explored;
		Unfold(src);
		for(int i=0;i<K;i++){
			int c=GBgetTransitionsLong(NULL,i,src+N,print_next,&(src[1]));
		}
		explored++;
		if(explored%1000 ==0) Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
	}
	Warning(info,"state space has %d states %d transitions",visited,trans);		
	lts_set_states(lts,visited);
	lts_set_trans(lts,trans);
	lts_set_labels(lts,label_count);
	stream_t ds=arch_write(arch,"info","");
	lts_write_info(lts,ds,DIR_INFO);
	DSclose(&ds);
	DSclose(&termdb_stream);
	DSclose(&src_stream);
	DSclose(&lbl_stream);
	DSclose(&dst_stream);
	arch_close(&arch);
	return 0;
}

