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
#include "dm/dm.h"

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
static int dlk_detect=0;

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
	{ "deadlock" , 'd' , POPT_ARG_VAL , &dlk_detect , 1 , "detect deadlocks" , NULL } ,
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
static int nGrps;
static vdom_t domain;
static vset_t visited;
static long max_count=0;
static long max_grp_count=0;
static long max_trans_count=0;
static model_t model;
static vrel_t *group_next;
static vrel_t *group_prev=NULL;
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
	vrel_add(group_next[ctx->group],ctx->src,dst);
	if (group_prev) vrel_add(group_prev[ctx->group],dst,ctx->src);
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

static inline void expand_group_next(int group,vset_t set){
	struct group_add_info ctx;
	explored=0;
	ctx.group=group;
	vset_project(group_tmp[group],set);
	vset_zip(group_explored[group],group_tmp[group]);
	vset_enum(group_tmp[group],explore_cb,&ctx);
	vset_clear(group_tmp[group]);
}

static void write_trace_step(int step, int*state){
	fprintf(stderr,"%4d:",step);
	for (int i=0;i<N;i++) {
		fprintf(stderr," %d",state[i]);
	}
	fprintf(stderr,"\n");
}

static int same_state(int*s1,int*s2){
	for(int i=0;i<N;i++) if (s1[i]!=s2[i]) return 0;
	return 1;
}

static int find_trace_to(int step,int *src,int *dst){
	if(same_state(src,dst))return step;
	vset_t src_reach=vset_create(domain,0,NULL);
	vset_t current=vset_create(domain,0,NULL);
	vset_t dst_reach=vset_create(domain,0,NULL);
	vset_t temp=vset_create(domain,0,NULL);
	vset_add(src_reach,src);
	vset_add(dst_reach,dst);
	int middle[N];
	int distance=0;
	//long long e_count;
	//long n_count;
	for(;;){
		//Warning(info,"from src forward");
		distance++;
		vset_copy(current,src_reach);
		//vset_count(current,&n_count,&e_count);
		//fprintf(stderr,"current set has %lld states (%ld nodes)\n",e_count,n_count);
		for(int i=0;i<nGrps;i++){
			vset_next(temp,current,group_next[i]);
			vset_union(src_reach,temp);
		}
		vset_clear(temp);
		vset_copy(current,src_reach);
		//Warning(info,"checking overlap");
		vset_minus(current,dst_reach);
		if (!vset_equal(src_reach,current)){
			vset_minus(src_reach,current);
			vset_example(src_reach,middle);
			//Warning(info,"middle found");
			break;
		}
		//Warning(info,"from dst backward");
		distance++;
		vset_copy(current,dst_reach);
		//vset_count(current,&n_count,&e_count);
		//fprintf(stderr,"current set has %lld states (%ld nodes)\n",e_count,n_count);
		for(int i=0;i<nGrps;i++){
			vset_next(temp,current,group_prev[i]);
			vset_union(dst_reach,temp);
		}
		vset_clear(temp);
		vset_copy(current,dst_reach);
		//Warning(info,"checking overlap");
		vset_minus(current,src_reach);
		if (!vset_equal(dst_reach,current)){
			vset_minus(dst_reach,current);
			vset_example(dst_reach,middle);
			//Warning(info,"middle found");
			break;
		}
	}
	vset_clear(src_reach);
	vset_clear(current);
	vset_clear(dst_reach);
	vset_clear(temp);
	switch(distance){
	case 1:
		write_trace_step(step,src);
		return step+1;
	case 2:
		write_trace_step(step,src);
		write_trace_step(step+1,middle);
		return step+2;
	default:
		step=find_trace_to(step,src,middle);
		return find_trace_to(step,middle,dst);
	}
}

static void find_trace(int *src,int *dst){
	int len=find_trace_to(0,src,dst);
	write_trace_step(len,dst);
}

static void reach_bfs(){
	int level,i;
	long long e_count;
	long n_count;
	long eg_count=0;
	long next_count=0;

	level=0;
	vset_t current_level=vset_create(domain,0,NULL);
	vset_t next_level=vset_create(domain,0,NULL);
	vset_t temp=vset_create(domain,0,NULL);
	vset_t deadlocks=dlk_detect?vset_create(domain,0,NULL):NULL;
	vset_t dlk_temp=dlk_detect?vset_create(domain,0,NULL):NULL;
	vset_copy(current_level,visited);
	for(;;){
		if (RTverbosity >= 1) {
			vset_count(current_level,&n_count,&e_count);
			fprintf(stderr,"level %d has %lld states ( %ld nodes )\n",level,e_count,n_count);
		}
		vset_count(visited,&n_count,&e_count);
		if (n_count>max_count) max_count=n_count;
		if (RTverbosity >= 1) {
			fprintf(stderr,"visited %d has %lld states ( %ld nodes )\n",level,e_count,n_count);
			if (RTverbosity >= 2) fprintf(stderr,"transition caches (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vrel_count(group_rel[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_trans_count) max_trans_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
			if (RTverbosity >= 2) fprintf(stderr,"group explored    (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vset_count(group_explored[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_grp_count) max_grp_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
		}
		if(vset_is_empty(current_level)) break;
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rexploring group %4d/%d",i+1,nGrps);
			expand_group_next(i,current_level);
			eg_count++;
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rexploration complete             \n");
		vset_clear(next_level);
		if (dlk_detect) vset_copy(deadlocks,current_level);
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rlocal next %4d/%d",i+1,nGrps);
			next_count++;
			vset_next(temp,current_level,group_next[i]);
			if (dlk_detect) {
				vset_next(dlk_temp,temp,group_prev[i]);
				vset_minus(deadlocks,dlk_temp);
				vset_clear(dlk_temp);
			}
			vset_minus(temp,visited);
			vset_union(next_level,temp);
			vset_clear(temp);
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rlocal next complete       \n");
		if (dlk_detect && !vset_is_empty(deadlocks)) {
			Warning(info,"deadlock found");
			int init_state[N];
			GBgetInitialState(model,init_state);
			int dlk_state[N];
			vset_example(deadlocks,dlk_state);
			find_trace(init_state,dlk_state);
			break;
		}
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
			fprintf(stderr,"visited %d has %lld states ( %ld nodes )\n",level,e_count,n_count);
			if (RTverbosity >= 2) fprintf(stderr,"transition caches (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vrel_count(group_rel[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_trans_count) max_trans_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
			if (RTverbosity >= 2) fprintf(stderr,"group explored    (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vset_count(group_explored[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_grp_count) max_grp_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
		}
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rexploring group %4d/%d",i+1,nGrps);
			expand_group_next(i,visited);
			eg_count++;
		}
		if (RTverbosity >= 2) fprintf(stderr,"\rexploration complete             \n");
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rlocal next %4d/%d",i+1,nGrps);
			next_count++;
			vset_next(temp,old_vis,group_next[i]);
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
			fprintf(stderr,"visited %d has %lld states ( %ld nodes )\n",level,e_count,n_count);
			if (RTverbosity >= 2) fprintf(stderr,"transition caches (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vrel_count(group_rel[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_trans_count) max_trans_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
			if (RTverbosity >= 2) fprintf(stderr,"group explored    (grp,nds,elts): ");
			for (i=0;i<nGrps;i++) 
			  {long long e;
			   long int n;
			   vset_count(group_explored[i],&n,&e);
			   if (RTverbosity >= 2) fprintf(stderr,"( %d %ld %lld ) ",i,n,e);
			   if (n>max_grp_count) max_grp_count=n;
			  }
			if (RTverbosity >= 2) fprintf(stderr,"\n");
		}
		level++;
		for(i=0;i<nGrps;i++){
			if (RTverbosity >= 2) fprintf(stderr,"\rgroup %4d/%d",i+1,nGrps);
			expand_group_next(i,visited);
			eg_count++;
			next_count++;
			vset_next(temp,visited,group_next[i]);
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
	model_t model;
	int group;
	int *src;
} output_context;

static void etf_edge(void*context,int*labels,int*dst){
	output_context* ctx=(output_context*)context;
	table_count++;
	int k=0;
	for(int i=0;i<N;i++) {
		if (dm_is_set(GBgetDMInfo(ctx->model), ctx->group, i)) {
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
	if(!table_file){
	    FatalCall(1,error,"could not open %s",etf_output);
	}
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
		ctx.model = model;
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
	nGrps=dm_nrows(GBgetDMInfo(model));
	domain=vdom_create_default(N);
	visited=vset_create(domain,0,NULL);
	group_next=(vrel_t*)RTmalloc(nGrps*sizeof(vrel_t));
	if (dlk_detect) {
		group_prev=(vrel_t*)RTmalloc(nGrps*sizeof(vrel_t));
		if (strategy!= BFS) Fatal(1,error,"to detect deadlocks, please use the BFS strategy");
	}
	group_explored=(vset_t*)RTmalloc(nGrps*sizeof(vset_t));
	group_tmp=(vset_t*)RTmalloc(nGrps*sizeof(vset_t));
	for(int i=0;i<nGrps;i++){
		int len = dm_ones_in_row(GBgetDMInfo(model), i);
		// get indices
		int tmp[len];
		// temporary replacement for e_info->indices[i]
		for(int j=0, k=0; j < dm_ncols(GBgetDMInfo(model)); j++) {
			if (dm_is_set(GBgetDMInfo(model), i,j))
				tmp[k++] = j;
		}

		group_next[i]=vrel_create(domain,len,tmp);
		if (group_prev) group_prev[i]=vrel_create(domain,len,tmp);
		group_explored[i]=vset_create(domain,len,tmp);
		group_tmp[i]=vset_create(domain,len,tmp);
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
   	        fprintf(stderr,"state space has %lld states ( %ld final nodes, %ld peak nodes)\n",
			e_count,n_count,max_count);
		SCCresetTimer(timer);
		SCCstartTimer(timer);
		do_output();
		SCCstopTimer(timer);
		SCCreportTimer(timer,"writing output took");
	} else {
	  printf("state space has %lld states ( %ld final nodes, %ld peak nodes)\n"
		 ,e_count,n_count,max_count);
	  if (RTverbosity >=1)
	    printf("peak transition cache: %ld nodes; peak group explored: %ld nodes\n",max_trans_count,max_grp_count);
	}
	return 0;
}
