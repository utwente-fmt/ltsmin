#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "archive.h"
#include "runtime.h"
#ifdef MCRL
#include "mcrl-greybox.h"
#endif
#ifdef MCRL2
#include "mcrl2-greybox.h"
#endif
#include "chunk-table.h"
#include "treedbs.h"
#include "ltsman.h"
#include "options.h"
#include "stringindex.h"

static char *outputarch=NULL;
static int plain=0;
static int blackbox=0;
static int verbosity=1;
static int write_lts=1;

struct option options[]={
	{"-out",OPT_REQ_ARG,assign_string,&outputarch,"-out <archive>",
		"Specifiy the name of the output archive.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,reset_int,&verbosity,NULL,"be silent",NULL,NULL,NULL},
	{"-nolts",OPT_NORMAL,reset_int,&write_lts,NULL,
		"disable writing of the LTS",NULL,NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",NULL,NULL,NULL},
	{"-black",OPT_NORMAL,set_int,&blackbox,NULL,
		"use the black box call (TransitionsAll)",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static int label_count=0;
static lts_t lts;
static stream_t termdb_stream;
static stream_t src_stream;
static stream_t lbl_stream;
static stream_t dst_stream;

static string_index_t si;

chunk_table_t CTcreate(char *name){
	if (!strcmp(name,"action")) {
		return (void*)1;
	}
	if (!strcmp(name,"leaf")) {
		return (void*)2;
	}
	Fatal(1,error,"CT support incomplete canniot deal with table %s",name);
	return NULL;
}

void CTsubmitChunk(chunk_table_t table,size_t len,void* chunk,chunk_add_t cb,void* context){
	if (table == (void*)1) {
		SIputAt(si,chunk,label_count);
		if (write_lts){
			DSwrite(termdb_stream,chunk,len);
			DSwrite(termdb_stream,"\n",1);
		}
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
	if (write_lts){
		DSwriteU32(src_stream,*((int*)arg));
		DSwriteU32(lbl_stream,lbl[0]);
		DSwriteU32(dst_stream,tmp[1]);
	}
	if (tmp[1]>=visited) visited=tmp[1]+1;
}

int main(int argc, char *argv[]){
	void* stackbottom=&argv;
	RTinit(argc,&argv);
	take_vars(&argc,argv);
#ifdef MCRL
	MCRLinitGreybox(argc,argv,stackbottom);
#endif
#ifdef MCRL2
	MCRL2initGreybox(argc,argv,stackbottom);
#endif
	parse_options(options,argc,argv);
	if (!outputarch && write_lts) Fatal(1,error,"please specify the output archive with -out");
	Warning(info,"opening %s",argv[argc-1]);
#ifdef MCRL
	model_t model=MCRLcreateGreyboxModel(argv[argc-1]);
#endif
#ifdef MCRL2
	model_t model=MCRL2createGreyboxModel(argv[argc-1]);
#endif
	lts_struct_t ltstype=GBgetLTStype(model);
	N=ltstype->state_length;
	edge_info_t e_info=GBgetEdgeInfo(model);
	K=e_info->groups;
	TreeDBSinit(N,1);
	Warning(info,"length is %d, there are %d groups",N,K);
	Warning(info,"Using %s mode",blackbox?"black box":"grey box");	
	int src[N+N];
	GBgetInitialState(model,src+N);
	Warning(info,"got initial state");
	archive_t arch;
	if (write_lts) {
		if (strstr(outputarch,"%s")) {
			arch=arch_fmt(outputarch,file_input,file_output,prop_get_U32("bs",65536));
		} else {
			uint32_t bs=prop_get_U32("bs",65536);
			uint32_t bc=prop_get_U32("bc",128);
			arch=arch_gcf_create(raf_unistd(outputarch),bs,bs*bc,0,1);
		}
	} else {
		arch=NULL;
	}
	lts=lts_new();
	lts_set_root(lts,0,0);
	lts_set_segments(lts,1);
	Fold(src);
	si=lts_get_string_index(lts);
	if (write_lts){
		termdb_stream=arch_write(arch,"TermDB",plain?NULL:"gzip",1);
		src_stream=arch_write(arch,"src-0-0",plain?NULL:"diff32|gzip",1);
		lbl_stream=arch_write(arch,"label-0-0",plain?NULL:"gzip",1);
		dst_stream=arch_write(arch,"dest-0-0",plain?NULL:"diff32|gzip",1);
	}
	int limit=0;
	int level=0;
	while(explored<visited){
		if (limit==explored){
			Warning(info,"level %d has %d states, explored %d states %d trans",
				level,(visited-explored),explored,trans);
			limit=visited;
			level++;
		}
		src[1]=explored;
		Unfold(src);
		int c;
		if(blackbox){
			c=GBgetTransitionsAll(model,src+N,print_next,&(src[1]));
		} else {
			for(int i=0;i<K;i++){
				c=GBgetTransitionsLong(model,i,src+N,print_next,&(src[1]));
			}
		}
		explored++;
		if(explored%1000 ==0) Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
	}
	Warning(info,"state space has %d states %d transitions",visited,trans);		
	if (write_lts){
		lts_set_states(lts,0,visited);
		lts_set_trans(lts,0,0,trans);
		stream_t ds=arch_write(arch,"info",plain?NULL:"",1);
		lts_write_info(lts,ds,LTS_INFO_DIR);
		DSclose(&ds);
		DSclose(&termdb_stream);
		DSclose(&src_stream);
		DSclose(&lbl_stream);
		DSclose(&dst_stream);
		arch_close(&arch);
	}
	return 0;
}

