#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "archive.h"
#include "runtime.h"
#if defined(MCRL)
#include "mcrl-greybox.h"
#elif defined(MCRL2)
#include "mcrl2-greybox.h"
#elif defined(NIPS)
#include "nips-greybox.h"
#else
#error "Unknown greybox provider."
#endif
#include "treedbs.h"
#include "ltsman.h"
#include "options.h"
#include "vector_set.h"
#include "scctimer.h"

static int verbosity=1;
static int bfs=0;
static int sat=0;
static int chain=0;

static struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: inst_lps [options] model.lps",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"--help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,reset_int,&verbosity,NULL,"be silent",NULL,NULL,NULL},
	{"",OPT_NORMAL,NULL,NULL,NULL,"exploration order options (default is BFS):",NULL,NULL,NULL},
	{"-bfs",OPT_NORMAL,set_int,&bfs,NULL,"enable saturation",NULL,NULL,NULL},
	{"-sat",OPT_NORMAL,set_int,&sat,NULL,"enable saturation",NULL,NULL,NULL},
	{"-chain",OPT_NORMAL,set_int,&chain,NULL,"enable chaining",NULL,NULL,NULL},
 	{0,0,0,0,0,0,0,0,0}
};

static lts_struct_t ltstype;
static int N;
static edge_info_t e_info;
static int nGrps;
static vdom_t domain;
static vset_t visited;
static model_t model;
static vrel_t *group_rel;
static vset_t *group_explored;
static vset_t *group_tmp;

static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}

struct group_add_info {
	int group;
	int *src;
};

static void group_add(void*context,int*labels,int*dst){
	(void)labels;
	struct group_add_info* ctx=(struct group_add_info*)context;
	vrel_add(group_rel[ctx->group],ctx->src,dst);
}

static void explore_cb(void*context,int *src){
	struct group_add_info* ctx=(struct group_add_info*)context;
	ctx->src=src;
	GBgetTransitionsShort(model,ctx->group,src,group_add,context);
}

static inline void expand_group_rel(int group,vset_t set){
	struct group_add_info ctx;
	ctx.group=group;
	vset_project(group_tmp[group],set);
	vset_zip(group_explored[group],group_tmp[group]);
	vset_enum(group_tmp[group],explore_cb,&ctx);
	vset_clear(group_tmp[group]);
}

void reach_bfs(){
	int level,i;
	long long e_count;
	long n_count;
	long eg_count=0;
	long next_count=0;

	level=0;
	vset_t current_level=vset_create(domain,0,NULL);
	vset_t next_level=vset_create(domain,0,NULL);
	vset_t temp=vset_create(domain,0,NULL);
	vset_copy(current_level,visited);
	for(;;){
		if (verbosity >= 1) {
			vset_count(current_level,&n_count,&e_count);
			fprintf(stderr,"level %d has %ld nodes and %lld elements\n",level,n_count,e_count);
			vset_count(visited,&n_count,&e_count);
			fprintf(stderr,"visited %d has %ld nodes and %lld elements\n",level,n_count,e_count);
		}
		if(vset_is_empty(current_level)) break;
		level++;
		for(i=0;i<nGrps;i++){
			if (verbosity >= 2) fprintf(stderr,"exploring group %4d/%d\r",i+1,nGrps);
			expand_group_rel(i,current_level);
			eg_count++;
		}
		if (verbosity >= 2) fprintf(stderr,"\n");
		vset_clear(next_level);
		for(i=0;i<nGrps;i++){
			if (verbosity >= 2) fprintf(stderr,"local next %4d/%d\r",i+1,nGrps);
			next_count++;
			vset_next(temp,current_level,group_rel[i]);
			vset_minus(temp,visited);
			vset_union(next_level,temp);
		}
		if (verbosity >= 2) fprintf(stderr,"\n");
		vset_union(visited,next_level);
		vset_copy(current_level,next_level);
	}
	Warning(info,"Exploration took %ld group checks and %ld next state calls",eg_count,next_count);
}

void reach_sat(){
	Fatal(1,error,"Saturation not implemented yet");
}

void reach_chain(){
	Fatal(1,error,"Chaining not implemented yet");
}

int main(int argc, char *argv[]){
	void* stackbottom=&argv;
	RTinit(argc,&argv);
	take_vars(&argc,argv);
	parse_options(options,argc,argv);
	switch(bfs+sat+chain){
	case 0:
		bfs=1;
		break;
	case 1:
		break;
	default:
		Fatal(1,error,"please select a unique exploration order.");
	}
	if (verbosity==0) {
		log_set_flags(info,LOG_IGNORE);
	}
#if defined(MCRL)
	MCRLinitGreybox(argc,argv,stackbottom);
#elif defined(MCRL2)
	MCRL2initGreybox(argc,argv,stackbottom);
#elif defined(NIPS)
        (void)stackbottom;
	NIPSinitGreybox(argc,argv);
#endif
	Warning(info,"opening %s",argv[argc-1]);
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);
#if defined(MCRL)
	MCRLloadGreyboxModel(model,argv[argc-1]);
#elif defined(MCRL2)
	MCRL2loadGreyboxModel(model,argv[argc-1]);
#elif defined(NIPS)
	NIPSloadGreyboxModel(model,argv[argc-1]);
#endif

	ltstype=GBgetLTStype(model);
	N=ltstype->state_length;
	edge_info_t e_info=GBgetEdgeInfo(model);
	nGrps=e_info->groups;
	domain=vdom_create(N);
	visited=vset_create(domain,0,NULL);
	group_rel=(vrel_t*)RTmalloc(nGrps*sizeof(vrel_t));
	group_explored=(vset_t*)RTmalloc(nGrps*sizeof(vset_t));
	group_tmp=(vset_t*)RTmalloc(nGrps*sizeof(vset_t));
	for(int i=0;i<nGrps;i++){
		group_rel[i]=vrel_create(domain,e_info->length[i],e_info->indices[i]);
		group_explored[i]=vset_create(domain,e_info->length[i],e_info->indices[i]);
		group_tmp[i]=vset_create(domain,e_info->length[i],e_info->indices[i]);
	}
	Warning(info,"length is %d, there are %d groups",N,nGrps);
	int src[N];
	GBgetInitialState(model,src);
	vset_add(visited,src);
	Warning(info,"got initial state");
	mytimer_t timer=SCCcreateTimer();
	SCCstartTimer(timer);
	if(bfs) {
		reach_bfs();
		SCCstopTimer(timer);
		SCCreportTimer(timer,"BFS reachability took");
		fprintf(stderr,"\n");
	}
	if(sat) {
		reach_sat();
		SCCstopTimer(timer);
		SCCreportTimer(timer,"Saturation took");
		fprintf(stderr,"\n");
	}
	if (chain) {
		reach_chain();
		SCCstopTimer(timer);
		SCCreportTimer(timer,"Chaining took");
		fprintf(stderr,"\n");
	}
	long long e_count;
	long n_count;
	vset_count(visited,&n_count,&e_count);
	printf("state space has %ld nodes and %lld elements\n",n_count,e_count);
	return 0;
}

