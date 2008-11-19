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
#include "treedbs.h"
#include "ltsman.h"
#include "options.h"
#include "vector_set.h"

static treedbs_t dbs;
static char *outputarch=NULL;
static int plain=0;
static int blackbox=0;
static int greybox=0;
static int verbosity=1;
static int write_lts=1;
static int cache=0;
static int use_vset=0;

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
		"use the black box call (TransitionsAll)","this is the default",NULL,NULL},
	{"-grey",OPT_NORMAL,set_int,&greybox,NULL,
		"use the box call (TransitionsLong)",NULL,NULL,NULL},
	{"-cache",OPT_NORMAL,set_int,&cache,NULL,
		"Add the caching wrapper around the model",NULL,NULL,NULL},
	{"-vset",OPT_NORMAL,set_int,&use_vset,NULL,
		"Use vector sets instead of tree compression",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

static vdom_t domain;
static vset_t visited_set;
static vset_t next_set;

static lts_t lts;

static stream_t src_stream;
static stream_t lbl_stream;
static stream_t dst_stream;

static int N;
static int K;
static int visited=1;
static int explored=0;
static int trans=0;

static void print_next(void*arg,int*lbl,int*dst){
	int tmp=TreeFold(dbs,dst);
	trans++;
	if (write_lts){
		DSwriteU32(src_stream,*((int*)arg));
		DSwriteU32(lbl_stream,lbl[0]);
		DSwriteU32(dst_stream,tmp);
	}
	if (tmp>=visited) visited=tmp+1;
}

static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}

static void set_next(void*arg,int*lbl,int*dst){
	(void)arg;
	(void)lbl;
	trans++;
	if (vset_member(visited_set,dst)) return;
	visited++;
	vset_add(visited_set,dst);
	vset_add(next_set,dst);
}

static void explore_elem(void*context,int*src){
	model_t model=(model_t)context;
	int c;
	if(blackbox){
		c=GBgetTransitionsAll(model,src,set_next,NULL);
	} else {
		for(int i=0;i<K;i++){
			c=GBgetTransitionsLong(model,i,src,set_next,NULL);
		}
	}
	explored++;
	if(explored%1000==0) Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
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
	if (write_lts && use_vset) Fatal(1,error,"writing in vector set mode is future work");

	switch(blackbox+greybox){
	case 0:
		blackbox=1;
		break;
	case 1:
		break;
	default:
		Fatal(1,error,"cannot use blackbox and greybox at the same time.");
	}

	Warning(info,"opening %s",argv[argc-1]);
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);
#ifdef MCRL
	MCRLloadGreyboxModel(model,argv[argc-1]);
#endif
#ifdef MCRL2
	MCRL2loadGreyboxModel(model,argv[argc-1]);
#endif

	if (cache) model=GBaddCache(model);

	lts_struct_t ltstype=GBgetLTStype(model);
	N=ltstype->state_length;
	edge_info_t e_info=GBgetEdgeInfo(model);
	K=e_info->groups;
	if (use_vset) {
		domain=vdom_create(N);
		visited_set=vset_create(domain,0,NULL);
		next_set=vset_create(domain,0,NULL);
	} else {
		dbs=TreeDBScreate(N);
	}
	Warning(info,"length is %d, there are %d groups",N,K);
	Warning(info,"Using %s mode",blackbox?"black box":"grey box");	
	int src[N];
	GBgetInitialState(model,src);
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
	if (use_vset) {
		vset_add(visited_set,src);
		vset_add(next_set,src);
	} else {
		if(TreeFold(dbs,src)!=0){
			Fatal(1,error,"root should be 0");
		}
	}
	if (write_lts){
		src_stream=arch_write(arch,"src-0-0",plain?NULL:"diff32|gzip",1);
		lbl_stream=arch_write(arch,"label-0-0",plain?NULL:"gzip",1);
		dst_stream=arch_write(arch,"dest-0-0",plain?NULL:"diff32|gzip",1);
	}
	int level=0;
	if (use_vset){
		vset_t current_set=vset_create(domain,0,NULL);
		while (!vset_is_empty(next_set)){
			Warning(info,"level %d has %d states, explored %d states %d trans",
				level,(visited-explored),explored,trans);
			level++;
			vset_copy(current_set,next_set);
			vset_clear(next_set);
			vset_enum(current_set,explore_elem,model);
		}
	} else {
		int limit=0;
		while(explored<visited){
			if (limit==explored){
				Warning(info,"level %d has %d states, explored %d states %d trans",
					level,(visited-explored),explored,trans);
				limit=visited;
				level++;
			}
			TreeUnfold(dbs,explored,src);
			//printf("explore");
			//for(int i=0;i<N;i++){
			//	printf("%2d ",src[i]);
			//}
			//printf("\n");
			int c;
			if(blackbox){
				c=GBgetTransitionsAll(model,src,print_next,&explored);
			} else {
				for(int i=0;i<K;i++){
					c=GBgetTransitionsLong(model,i,src,print_next,&explored);
				}
			}
			explored++;
			if(explored%1000==0) Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
		}
	}
	Warning(info,"state space has %d levels %d states %d transitions",level,visited,trans);		
	if (write_lts){
		lts_set_states(lts,0,visited);
		lts_set_trans(lts,0,0,trans);
		stream_t ds;
		ds=arch_write(arch,"TermDB",plain?NULL:"gzip",1);
		int label_count=GBchunkCount(model,ltstype->edge_label_type[0]);
		Warning(info,"%d action labels",label_count);
		string_index_t si=lts_get_string_index(lts);
		for(int i=0;i<label_count;i++){
			chunk c=GBchunkGet(model,ltstype->edge_label_type[0],i);
			SIputCAt(si,c.data,c.len,i);
			DSwrite(ds,c.data,c.len);
			DSwrite(ds,"\n",1);
		}
		DSclose(&ds);
		ds=arch_write(arch,"info",plain?NULL:"",1);
		lts_write_info(lts,ds,LTS_INFO_DIR);
		DSclose(&ds);
		DSclose(&src_stream);
		DSclose(&lbl_stream);
		DSclose(&dst_stream);
		arch_close(&arch);
	}
	return 0;
}

