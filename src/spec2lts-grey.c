#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>


#include <lts_enum.h>
#include <lts_io.h>
#include <stringindex.h>

#include "archive.h"
#include "runtime.h"
#include "treedbs.h"
#include "vector_set.h"

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

static lts_enum_cb_t output_handle=NULL;
static lts_output_t output=NULL;

static treedbs_t dbs=NULL;
static int write_lts;
static int matrix=0;
static int write_state=0;

typedef enum { UseGreyBox , UseBlackBox } mode_t;
static mode_t call_mode=UseBlackBox;

static const char state_default[5]="tree";
static char* state_repr=state_default;

static enum { ReachTreeDBS, ReachVset, RunTorX } application=ReachTreeDBS;

static  struct poptOption development_options[] = {
	{ "grey", 0 , POPT_ARG_VAL , &call_mode , UseGreyBox , "make use of GetTransitionsLong calls" , NULL },
	{ "matrix", 0 , POPT_ARG_VAL, &matrix,1,"Print the dependency matrix and quit",NULL},
	{ "write-state" , 0 , POPT_ARG_VAL , &write_state, 1 , "write the full state vector" , NULL },
	POPT_TABLEEND
};

static si_map_entry db_types[]={
	{"tree",ReachTreeDBS},
	{"vset",ReachVset},
	{NULL,0}
};

static void state_db_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		if (state_repr!=state_default){
			if (application==RunTorX){
				Warning(error,"using --state=%s with --torx is not permitted",state_repr);				
				exit(EXIT_FAILURE);
			}
			int res=linear_search(db_types,state_repr);
			if (res<0) {
				Warning(error,"unknown vector storage mode type %s",state_repr);
				RTexitUsage(EXIT_FAILURE);
			}
			application = res;
			return;
			
		}
		return;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to state_db_popt");
}

static  struct poptOption options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION  , (void*)state_db_popt , 0 , NULL , NULL },
	{ "state" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &state_repr , 0 ,
		"select the data structure for storing states", "<tree|vset>"},
	{ "torx" , 0 , POPT_ARG_VAL , &application ,RunTorX, "run TorX-Explorer textual interface on stdin+stdout" , NULL },
#if defined(MCRL)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl_options , 0 , "mCRL options", NULL },
#endif
#if defined(MCRL2)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, mcrl2_options , 0 , "mCRL2 options", NULL },
#endif
#if defined(NIPS)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, nips_options , 0 , "NIPS options", NULL },
#endif
#if defined(ETF)
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, etf_options , 0 , "ETF options", NULL },
#endif
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, greybox_options , 0 , "Greybox options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, vset_setonly_options , 0 , "Vector set options", NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL , NULL },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, development_options , 0 , "Development options" , NULL },
	POPT_TABLEEND
};

typedef struct torx_context_t {
	model_t model;
	lts_type_t ltstype;
} torx_struct_t;


static vdom_t domain;
static vset_t visited_set;
static vset_t next_set;


static int N;
static int K;
static int state_labels;
static int edge_labels;
static int visited=1;
static int explored=0;
static int trans=0;


static void vector_next(void*arg,int*lbl,int*dst){
	int *src_ofs_p=(int*)arg;
	if (!vset_member(visited_set,dst)) {
		visited++;
		vset_add(visited_set,dst);
		vset_add(next_set,dst);
	}
	if (write_lts) enum_seg_vec(output_handle,0,*src_ofs_p,dst,lbl);
	trans++;
}
	
static void index_next(void*arg,int*lbl,int*dst){
	int *src_ofs_p=(int*)arg;
	int tmp=TreeFold(dbs,dst);
	if (tmp>=visited) visited=tmp+1;
	if (write_lts) enum_seg_seg(output_handle,0,*src_ofs_p,0,tmp,lbl);
	trans++;
}

static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}


static void explore_state_index(void*context,int idx,int*src){
	model_t model=(model_t)context;
	int labels[state_labels];
	if (state_labels){
		GBgetStateLabelsAll(model,src,labels);
	}
	if(write_lts){
		if(write_state){
			enum_vec(output_handle,src,labels);
		} else {
			enum_seg(output_handle,0,idx,labels);
		}
	}
	switch(call_mode){
	case UseBlackBox:
		GBgetTransitionsAll(model,src,index_next,&idx);
		break;
	case UseGreyBox:
		for(int i=0;i<K;i++){
			GBgetTransitionsLong(model,i,src,index_next,&idx);
		}
		break;
	}
	explored++;
	if (explored%1000==0 && RTverbosity>=2) 
	  Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
}

static void explore_state_vector(void*context,int*src){
	model_t model=(model_t)context;
	int labels[state_labels];
	if (state_labels){
		GBgetStateLabelsAll(model,src,labels);
	}
	if(write_lts){
		enum_vec(output_handle,src,labels);
	}
	switch(call_mode){
	case UseBlackBox:
		GBgetTransitionsAll(model,src,vector_next,&explored);
		break;
	case UseGreyBox:
		for(int i=0;i<K;i++){
			GBgetTransitionsLong(model,i,src,vector_next,&explored);
		}
		break;
	}
	explored++;
	if (explored%1000==0 && RTverbosity>=2) 
	  Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
}


static void torx_transition(void*arg,int*lbl,int*dst){

	torx_struct_t *context=(torx_struct_t*)arg;

	int tmp=TreeFold(dbs,dst);
	chunk c=GBchunkGet(context->model,lts_type_get_edge_label_typeno(context->ltstype,0),lbl[0]);
	
	int vis = 1;
	if (c.len==3 && strncmp(c.data, "tau", c.len)==0)
		vis =0;

	/* tab-separated fields: edge vis sat lbl pred vars state */
	fprintf(stdout, "Ee\t\t%d\t1\t%*s\t\t\t%d\n", vis, c.len, c.data, tmp);
}


static int torx_handle_request(torx_struct_t *context, char *req)
{
	while(isspace((int)*req))
		req++;
	switch(req[0]) {
	case 'r': {			/* reset */
		fprintf(stdout, "R 0\t1\n");
		fflush(stdout);
		break;
	}
	case 'e': {			/*explore */
		int n, res;
		req++;
		while(isspace((int)*req))
			req++;
		if ((res = sscanf(req, "%u", &n)) != 1) {
			int l = strlen(req);
			if (req[l - 1] == '\n')
				req[l - 1] = '\0';
			fprintf(stdout, "E0 Missing event number (%s; sscanf found #%d)\n", req, res);
		} else if (n >= TreeCount(dbs)) {
			fprintf(stdout, "E0 Unknown event number\n");
			fflush(stdout);
		} else {
			int src[N], c;
			TreeUnfold(dbs,n,src);
			fprintf(stdout, "EB\n");
			c=GBgetTransitionsAll(context->model,src,torx_transition,context);
			fprintf(stdout, "EE\n");
			fflush(stdout);
		}
		break;
	}
	case 'q': {
		fprintf(stdout, "Q\n");
		fflush(stdout);
		return 1;
		break;
	}
	default:			/* unknown command */
		fprintf(stdout, "A_ERROR UnknownCommand: %s\n", req);
		fflush(stdout);
	}
	return 0;
}

static void torx_ui(torx_struct_t *context) {
	char buf[BUFSIZ];
	int stop = 0;
	while (!stop && fgets(buf, BUFSIZ, stdin)) {
		if (!strchr(buf, '\n'))
			/* uncomplete read; ignore the problem for now */
			Warning(info, "no end-of-line character read on standard input (incomplete read?)\n") ;
		stop = torx_handle_request(context, buf);
	}
}

int main(int argc, char *argv[]){
	char *files[2];
	RTinitPopt(&argc,&argv,options,1,2,files,NULL,"<model> [<lts>]",
		"Perform an enumerative reachability analysis of <model>\n"
		"Run the TorX remote procedure call protocol on <model> (--torx).\n\n"
		"Options");
	if (files[1]) {
		Warning(info,"Writing output to %s",files[1]);
		write_lts=1;
	} else {
		Warning(info,"No output, just counting the number of states");
		write_lts=0;
	}
	if (application==RunTorX && write_lts) Fatal(1,error,"A TorX server does not write to a file");
	Warning(info,"loading model from %s",files[0]);
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);

	GBloadFile(model,files[0],&model);

	if (RTverbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	}
	if (matrix) {
	  GBprintDependencyMatrix(stdout,model);
	  exit(0);
	}
	lts_type_t ltstype=GBgetLTStype(model);
	N=lts_type_get_state_length(ltstype);
	edge_info_t e_info=GBgetEdgeInfo(model);
	K=e_info->groups;
	Warning(info,"length is %d, there are %d groups",N,K);
	state_labels=lts_type_get_state_label_count(ltstype);
	edge_labels=lts_type_get_edge_label_count(ltstype);
	Warning(info,"There are %d state labels and %d edge labels",state_labels,edge_labels);
	if (state_labels&&write_lts&&!write_state) {
		Fatal(1,error,"Writing state labels, but not state vectors unsupported. "
			"Writing of state vector is enabled with the option --write-state");
	}
	int src[N];
	GBgetInitialState(model,src);
	Warning(info,"got initial state");
	int level=0;
	switch(application){
	case ReachVset:
		domain=vdom_create_default(N);
		visited_set=vset_create(domain,0,NULL);
		next_set=vset_create(domain,0,NULL);
		if (write_lts){
			output=lts_output_open(files[1],model,1,0,1,"viv",NULL);
			lts_output_set_root_vec(output,(uint32_t*)src);
			lts_output_set_root_idx(output,0,0);
			output_handle=lts_output_begin(output,0,0,0);	
		}
		vset_add(visited_set,src);
		vset_add(next_set,src);
		vset_t current_set=vset_create(domain,0,NULL);
		while (!vset_is_empty(next_set)){
		  if (RTverbosity >= 1)
		    Warning(info,"level %d has %d states, explored %d states %d trans",
			    level,(visited-explored),explored,trans);
		  level++;
		  vset_copy(current_set,next_set);
		  vset_clear(next_set);
		  vset_enum(current_set,explore_state_vector,model);
		}
		long long size;
		long nodes;
		vset_count(visited_set,&nodes,&size);
	    	Warning(info,"%lld reachable states represented symbolically with %ld nodes",size,nodes);
		break;
	case ReachTreeDBS:
		dbs=TreeDBScreate(N);
		if(TreeFold(dbs,src)!=0){
			Fatal(1,error,"expected 0");
		}
		if (write_lts){
			output=lts_output_open(files[1],model,1,0,1,write_state?"vsi":"-ii",NULL);
			if (write_state) lts_output_set_root_vec(output,(uint32_t*)src);
			lts_output_set_root_idx(output,0,0);
			output_handle=lts_output_begin(output,0,0,0);	
		}
		int limit=visited;
		while(explored<visited){
		  if (limit==explored){
		    if (RTverbosity >= 1)
		      Warning(info,"level %d has %d states, explored %d states %d trans",
			      level,(visited-explored),explored,trans);
		    limit=visited;
		    level++;
		  }
		  TreeUnfold(dbs,explored,src);
		  explore_state_index(model,explored,src);
		}
		break;
	case RunTorX:
		{
		torx_struct_t context = { model, ltstype };
		torx_ui(&context);
		return 0;
		}
	}
	if (write_lts){
		lts_output_end(output,output_handle);
		Warning(info,"finishing the writing");
		lts_output_close(&output);
		Warning(info,"state space has %d levels %d states %d transitions",level,visited,trans);
	} else {
		printf("state space has %d levels %d states %d transitions\n",level,visited,trans);
	}
	return 0;
}

