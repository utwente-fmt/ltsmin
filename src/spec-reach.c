#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "archive.h"
#include <runtime.h>
#include "treedbs.h"
#include <stringindex.h>
#include "vector_set.h"
#include "scctimer.h"

#if defined(MCRL)
#include "mcrl-greybox.h"
#endif
#if defined(MCRL2)
#include "mcrl2-greybox.h"
#endif
#if defined(NIPS)
#include "nips-greybox.h"
#endif
#if defined(ETF)
#include "etf-greybox.h"
#endif

static char* etf_output=NULL;

static enum { BFS , BFS2 , Chain } strategy = BFS;

static char* order="bfs";
static si_map_entry strategies[]={
	{"bfs",BFS},
	{"bfs2",BFS2},
	{"chain",Chain},
	{NULL,0}
};

static void reach_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		Fatal(1,error,"unexpected call to vset_popt");
	case POPT_CALLBACK_REASON_POST: {
		int res=linear_search(strategies,order);
		if (res<0) {
			Warning(error,"unknown exploration order %s",order);
			RTexitUsage(EXIT_FAILURE);
		} else {
			Warning(info,"Exploration order is %s",order);
		}
		strategy = res;
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		Fatal(1,error,"unexpected call to reach_popt");
	}
}


static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)reach_popt , 0 , NULL , NULL },
	{ "order" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &order , 0 , "select the exploration strategy to a specific order" ,"<bfs|bfs2|chain>" },
#if defined(MCRL)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl_options , 0 , "mCRL options",NULL},
#endif
#if defined(MCRL2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl2_options , 0 , "mCRL2 options",NULL},
#endif
#if defined(NIPS)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, nips_options , 0 , "NIPS options",NULL},
#endif
#if defined(ETF)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, etf_options , 0 , "ETF options",NULL},
#endif
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options",NULL},
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_full_options , 0 , "Vector set options",NULL},
	POPT_TABLEEND
};

static lts_type_t ltstype;
static int N;
static int eLbls;
static edge_info_t e_info;
static int nGrps;
static vdom_t domain;
static vset_t visited;
static long max_count=0;
static model_t model;
static vrel_t *group_rel;
static vset_t *group_explored;
static vset_t *group_tmp;
static int explored;

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
	explored++;
	if (explored%1000 ==0 && RTverbosity >=2) {
		Warning(info,"explored %d short vectors for group %d",explored,ctx->group);
	}
}

static inline void expand_group_rel(int group,vset_t set){
	struct group_add_info ctx;
	explored=0;
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
		if (RTverbosity >= 1) {
			vset_count(current_level,&n_count,&e_count);
			fprintf(stderr,"level %d has %lld states (%ld nodes)\n",level,e_count,n_count);
		}
		vset_count(visited,&n_count,&e_count);
		if (n_count>max_count) max_count=n_count;
		if (RTverbosity >= 1) {
			fprintf(stderr,"visited %d has %lld states (%ld nodes)\n",level,e_count,n_count);
		}
		if(vset_is_empty(current_level)) break;
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rexploring group %4d/%d",i+1,nGrps);
			expand_group_rel(i,current_level);
			eg_count++;
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rexploration complete             \n");
		vset_clear(next_level);
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rlocal next %4d/%d",i+1,nGrps);
			next_count++;
			vset_next(temp,current_level,group_rel[i]);
			vset_minus(temp,visited);
			vset_union(next_level,temp);
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rlocal next complete       \n");
		vset_union(visited,next_level);
		vset_copy(current_level,next_level);
	}
	Warning(info,"Exploration took %ld group checks and %ld next state calls",eg_count,next_count);
}

void reach_bfs2(){
	int level,i;
	long long e_count;
	long n_count;
	long eg_count=0;
	long next_count=0;

	level=0;
	vset_t old_vis=vset_create(domain,0,NULL);
	vset_t temp=vset_create(domain,0,NULL);
	for(;;){
		vset_copy(old_vis,visited);
		vset_count(visited,&n_count,&e_count);
		if (n_count>max_count) max_count=n_count;
		if (RTverbosity >= 1) {
			fprintf(stderr,"visited %d has %lld states (%ld nodes)\n",level,e_count,n_count);
		}
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rexploring group %4d/%d",i+1,nGrps);
			expand_group_rel(i,visited);
			eg_count++;
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rexploration complete             \n");
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rlocal next %4d/%d",i+1,nGrps);
			next_count++;
			vset_next(temp,old_vis,group_rel[i]);
			vset_union(visited,temp);
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rlocal next complete       \n");
		if (vset_equal(visited,old_vis)) break;
	}
	Warning(info,"Exploration took %ld group checks and %ld next state calls",eg_count,next_count);
}

void reach_sat(){
	Fatal(1,error,"Saturation not implemented yet");
}

void reach_chain(){
	int level,i;
	long long e_count;
	long n_count;
	long eg_count=0;
	long next_count=0;

	level=0;
	vset_t old_vis=vset_create(domain,0,NULL);
	vset_t temp=vset_create(domain,0,NULL);
	for(;;){
		vset_copy(old_vis,visited);
		vset_count(visited,&n_count,&e_count);
		if (n_count>max_count) max_count=n_count;
		if (RTverbosity >= 1) {
			fprintf(stderr,"visited %d has %lld states (%ld nodes)\n",level,e_count,n_count);
		}
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rgroup %4d/%d",i+1,nGrps);
			expand_group_rel(i,visited);
			eg_count++;
			next_count++;
			vset_next(temp,visited,group_rel[i]);
			vset_union(visited,temp);
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rround %d complete       \n",level);
		if (vset_equal(visited,old_vis)) break;
	}
	Warning(info,"Exploration took %ld group checks and %ld next state calls",eg_count,next_count);
}

static FILE* table_file;

static int table_count=0;

typedef struct {
	int group;
	int *src;
} output_context;

static void etf_edge(void*context,int*labels,int*dst){
	output_context* ctx=(output_context*)context;
	table_count++;
	int k=0;
	for(int i=0;i<N;i++) {
		if (k<e_info->length[ctx->group] && e_info->indices[ctx->group][k]==i){
			fprintf(table_file," %d/%d",ctx->src[k],dst[k]);
			k++;
		} else {
			fprintf(table_file," *");
		}
	}
	for(int i=0;i<eLbls;i++) {
		fprintf(table_file," %d",labels[i]);
	}
	fprintf(table_file,"\n");
}

static void enum_edge(void*context,int *src){
	output_context* ctx=(output_context*)context;
	ctx->src=src;
	GBgetTransitionsShort(model,ctx->group,ctx->src,etf_edge,context);
}

typedef struct {
	int mapno;
	int len;
	int*used;
} map_context;


static void enum_map(void*context,int *src){
	map_context*ctx=(map_context*)context;
	int val=GBgetStateLabelShort(model,ctx->mapno,src);
	int k=0;
	for(int i=0;i<N;i++){
		if (k<ctx->len && ctx->used[k]==i){
			fprintf(table_file,"%d ",src[k]);
			k++;
		} else {
			fprintf(table_file,"* ");
		}
	}
	fprintf(table_file,"%d\n",val);
}


void do_output(){
	eLbls=lts_type_get_edge_label_count(ltstype);
	int state[N];
	GBgetInitialState(model,state);
	table_file=fopen(etf_output,"w");
	fprintf(table_file,"begin state\n");
	for(int i=0;i<N;i++){
		char*name=lts_type_get_state_name(ltstype,i);
		int sort=lts_type_get_state_typeno(ltstype,i);
		fprintf(table_file,"%s:%s%s",
			name?name:"_",
			(sort>=0)?lts_type_get_state_type(ltstype,i):"_",
			(i==(N-1))?"\n":" ");
	}
	fprintf(table_file,"end state\n");
	fprintf(table_file,"begin edge\n");
	for(int i=0;i<eLbls;i++){
		fprintf(table_file,"%s:%s%s",
			lts_type_get_edge_label_name(ltstype,i),
			lts_type_get_edge_label_type(ltstype,i),
			(i==(eLbls-1))?"\n":" ");
	}
	fprintf(table_file,"end edge\n");
	fprintf(table_file,"begin init\n");
	for(int i=0;i<N;i++) {
		fprintf(table_file,"%d%s",state[i],(i==(N-1))?"\n":" ");
	}
	fprintf(table_file,"end init\n");
	for(int g=0;g<nGrps;g++){
		output_context ctx;
		ctx.group=g;
		fprintf(table_file,"begin trans\n");
		vset_enum(group_explored[g],enum_edge,&ctx);
		fprintf(table_file,"end trans\n");
	}
	Warning(info,"Symbolic tables have %d reachable transitions",table_count);
	int sLbls=lts_type_get_state_label_count(ltstype);
	state_info_t s_info=GBgetStateInfo(model);
	for(int i=0;i<sLbls;i++){
		int len=s_info->length[i];
		int *used=s_info->indices[i];
		vset_t patterns=vset_create(domain,len,used);
		vset_project(patterns,visited);
		map_context ctx;
		ctx.mapno=i;
		ctx.len=len;
		ctx.used=used;
		fprintf(table_file,"begin map %s:%s\n",
			lts_type_get_state_label_name(ltstype,i),
			lts_type_get_state_label_type(ltstype,i));
		vset_enum(patterns,enum_map,&ctx);
		fprintf(table_file,"end map\n");
		vset_clear(patterns); // Should be vset_destroy, which doesn't exist.
	}
	int type_count=lts_type_get_type_count(ltstype);
	for(int i=0;i<type_count;i++){
		Warning(info,"dumping type %s",lts_type_get_type(ltstype,i));
		fprintf(table_file,"begin sort %s\n",lts_type_get_type(ltstype,i));
		int values=GBchunkCount(model,i);
		for(int j=0;j<values;j++){
			chunk c=GBchunkGet(model,i,j);
			size_t len=c.len*2+3;
			char str[len];
			chunk2string(c,len,str);
			fprintf(table_file,"%s\n",str);
		}
		fprintf(table_file,"end sort\n");
	}
	fclose(table_file);
}

int main(int argc, char *argv[]){
	char* files[2];
	RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<model> [<etf>]",
		"Perform a symbolic reachability analysis of <model>\n"
		"The optional output of this analysis is an ETF representation of the input\n\nOptions");
	etf_output=files[1];
	if (RTverbosity==0) {
		log_set_flags(info,LOG_IGNORE);
	}
	Warning(info,"opening %s",files[0]);
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

	if (RTverbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	}

	ltstype=GBgetLTStype(model);
	N=lts_type_get_state_length(ltstype);
	e_info=GBgetEdgeInfo(model);
	nGrps=e_info->groups;
	domain=vdom_create_default(N);
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
	switch(strategy){
	case BFS:
		reach_bfs();
		break;
	case BFS2:
		reach_bfs2();
		break;
	case Chain:
		reach_chain();
		break;
	}
	SCCstopTimer(timer);
	SCCreportTimer(timer,"reachability took");
	long long e_count;
	long n_count;
	vset_count(visited,&n_count,&e_count);
	if (etf_output) {
   	        fprintf(stderr,"state space has %lld states (%ld final nodes, %ld peak nodes)\n",
			e_count,n_count,max_count);
		SCCresetTimer(timer);
		SCCstartTimer(timer);
		do_output();
		SCCstopTimer(timer);
		SCCreportTimer(timer,"writing output took");
	} else {
	  printf("state space has %lld states (%ld final nodes, %ld peak nodes)\n"
		 ,e_count,n_count,max_count);
	}
	return 0;
}

