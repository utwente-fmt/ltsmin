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
#if defined(MCRL)
#include "mcrl-greybox.h"
#define MODEL_TYPE "lpo"
#elif defined(MCRL2)
#include "mcrl2-greybox.h"
#define MODEL_TYPE "lps"
#elif defined(NIPS)
#include "nips-greybox.h"
#define MODEL_TYPE "nips"
#elif defined(ETF)
#include "etf-greybox.h"
#define MODEL_TYPE "etf"
#else
#error "Unknown greybox provider."
#endif
#include "treedbs.h"
#include "options.h"
#include "vector_set.h"
#include "struct_io.h"


static lts_enum_cb_t output_handle=NULL;
static lts_output_t output=NULL;

static treedbs_t dbs=NULL;
static char *dir_name=NULL;
static int write_state=0;
static char *vec_name=NULL;
static int plain=0;
static int blackbox=0;
static int greybox=0;
static int verbosity=1;
static int write_lts=1;
static int cache=0;
static int use_vset=0;
static int state_visible=0;
static int use_vset_list=0;
static int use_vset_tree=0;
static int use_vset_fdd=0;
static int torx =0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: " MODEL_TYPE "2lts-grey [options] <model>",NULL,NULL,NULL},
	{"-dir",OPT_REQ_ARG,assign_string,&dir_name,"-dir <archive>",
		"Give a name of an output archive to be written in DIR format.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
	{"-out",OPT_REQ_ARG,assign_string,&dir_name,"-out <archive>",
		"alias for -dir",NULL,NULL,NULL},
	{"-state",OPT_NORMAL,set_int,&write_state,NULL,
		"Add full state information to the DIR archive",NULL,NULL,NULL},
	{"-vec",OPT_REQ_ARG,assign_string,&vec_name,"-vec <archive>",
		"Give a name of an output archive to be written in vector format.",
		NULL,NULL,NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,NULL,"be silent",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{"-io-help",OPT_NORMAL,usage,NULL,NULL,
		"print help for io sub system",NULL,NULL,NULL},
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
	{"-vset",OPT_NORMAL,set_int,&use_vset_tree,NULL,"alias for -vset-tree",NULL,NULL,NULL},
	{"-vset-list",OPT_NORMAL,set_int,&use_vset_list,NULL,
	        "Use vector sets with MDD nodes organized in a linked list",
		"This option cannot be used in combination with -out",
		NULL,NULL},
	{"-version",OPT_NORMAL,print_version,NULL,NULL,"print the version",NULL,NULL,NULL},
#ifdef MCRL
	{"-mcrl-state",OPT_NORMAL,set_int,&state_visible,NULL,
		"Make all state variables visible.",NULL,NULL,NULL},
#endif
	{"-vset-tree",OPT_NORMAL,set_int,&use_vset_tree,NULL,
		"Use vector sets with MDD nodes organized in a tree",
		"This option cannot be used in combination with -out",
		NULL,NULL},
	{"-vset-fdd",OPT_NORMAL,set_int,&use_vset_fdd,NULL,
		"Uses the FDD interface of BuDDy to represent sets",
		"This option cannot be used in combination with -out",
		NULL,NULL},
	{"-torx",OPT_NORMAL,set_int,&torx,NULL,
		"Run TorX-Explorer textual interface on stdin+stdout",NULL,NULL,NULL},
	{"-version",OPT_NORMAL,print_version,NULL,NULL,"print the version",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
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


static void handle_next(void*arg,int*lbl,int*dst){
	(void)arg;
	if (use_vset) {
		if (!vset_member(visited_set,dst)) {
			visited++;
			vset_add(visited_set,dst);
			vset_add(next_set,dst);
		if (write_lts) enum_seg_vec(output_handle,0,explored,dst,lbl);
		}		
	} else {
		int tmp=TreeFold(dbs,dst);
		if (tmp>=visited) visited=tmp+1;
		if (write_lts) enum_seg_seg(output_handle,0,explored,0,tmp,lbl);
	}
	trans++;
}

static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}

/**
\brief Explore one state.

It is critical that the given state is the state whose number is explored.
When called from vector set enumeration this condition is met
because this is where the state number is defined and immediately forgotten.
When called from an exploration that uses dbs, the caller has to guarantee this.
 */
static void explore_state(void*context,int*src){
	model_t model=(model_t)context;
	if (state_labels){
		int labels[state_labels];
		GBgetStateLabelsAll(model,src,labels);
		if(write_lts){
			if(dbs) {
				enum_seg(output_handle,0,explored,labels);
			} else {
				enum_vec(output_handle,src,labels);
			}
		}
	} else if (write_lts) {
		if (dbs) {
			enum_seg(output_handle,0,explored,NULL);
		} else {
			enum_vec(output_handle,src,NULL);
		}
	}
	if(blackbox){
		GBgetTransitionsAll(model,src,handle_next,NULL);
	} else {
		for(int i=0;i<K;i++){
			GBgetTransitionsLong(model,i,src,handle_next,NULL);
		}
	}
	explored++;
	if(explored%1000==0) Warning(info,"explored %d visited %d trans %d",explored,visited,trans);
}

#if defined(NIPS) || defined(ETF)
#include "aterm1.h"

static void WarningHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(info);
	if (f) {
		fprintf(f,"ATerm library: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
}
     
static void ErrorHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(error);
	if (f) {
		fprintf(f,"ATerm library: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
	Fatal(1,error,"ATerror");
	exit(1);
}
#endif

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
	void* stackbottom=&argv;
	RTinit(&argc,&argv);
#if defined(MCRL)
	MCRLinitGreybox(argc,argv,stackbottom);
#elif defined(MCRL2)
	MCRL2initGreybox(argc,argv,stackbottom);
#elif defined(NIPS)
 	ATinit(argc, argv, (ATerm*) stackbottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	NIPSinitGreybox(argc,argv);
#elif defined(ETF)
 	ATinit(argc, argv, (ATerm*) stackbottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	// ETF has no init!
#endif
	lts_io_init(&argc,argv);
	parse_options(options,argc,argv);
	char*outputarch=NULL;
	if (write_lts) {
		if (dir_name && vec_name) Fatal(1,error,"please select a single output file");
		if (dir_name) {
			outputarch=dir_name;
			if (use_vset) Fatal(1,error,"cannot write DIR format when using vector sets");
		} else if (vec_name) {
			outputarch=vec_name;
		} else {
			Fatal(1,error,"please select an output mode");
		}
	}
	if (torx) {
		write_lts = 0;
		use_vset = 0;
		use_vset_tree = 0;
	}
	if (!outputarch && write_lts) Fatal(1,error,"please specify the output archive with -out");
	switch(use_vset_tree+use_vset_list+use_vset_fdd){
	case 0:
		break;
	case 1:
		use_vset=1;
		break;
	default:
		Fatal(1,error,"cannot use more than one vset implementation at once.");
	}
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
#if defined(MCRL)
	MCRLloadGreyboxModel(model,argv[argc-1],(state_visible*STATE_VISIBLE));
#elif defined(MCRL2)
	MCRL2loadGreyboxModel(model,argv[argc-1]);
#elif defined(NIPS)
	NIPSloadGreyboxModel(model,argv[argc-1]);
#elif defined(ETF)
	ETFloadGreyboxModel(model,argv[argc-1]);
#endif

	if (verbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	}

	if (cache) model=GBaddCache(model);

	lts_type_t ltstype=GBgetLTStype(model);
	N=lts_type_get_state_length(ltstype);
	edge_info_t e_info=GBgetEdgeInfo(model);
	K=e_info->groups;
	if (use_vset) {
		if (use_vset_list) domain=vdom_create_list(N);
		if (use_vset_tree) domain=vdom_create_tree(N);
		if (use_vset_fdd) domain=vdom_create_fdd(N);
		visited_set=vset_create(domain,0,NULL);
		next_set=vset_create(domain,0,NULL);
	} else {
		dbs=TreeDBScreate(N);
	}
	Warning(info,"length is %d, there are %d groups",N,K);
	Warning(info,"Using %s mode",blackbox?"black box":"grey box");
	state_info_t s_info=GBgetStateInfo(model);
	state_labels=lts_type_get_state_label_count(ltstype);
	edge_labels=lts_type_get_edge_label_count(ltstype);
	Warning(info,"There are %d state labels and %d edge labels",state_labels,edge_labels);
	int src[N];
	GBgetInitialState(model,src);
	Warning(info,"got initial state");
	archive_t arch=NULL;
	if (write_lts) {
		Warning(info,"opening %s",outputarch);
		if (use_vset) Fatal(1,error,"output unsupported");
		output=lts_output_open(outputarch,model,1,0,0,0);
		output_handle=lts_output_enum(output);
	}
	if (use_vset) {
		vset_add(visited_set,src);
		vset_add(next_set,src);
	} else {
		if(TreeFold(dbs,src)!=0){
			Fatal(1,error,"root should be 0");
		}
	}
	if (torx) {
		torx_struct_t context = { model, ltstype };
		torx_ui(&context);
		return 0;
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
			vset_enum(current_set,explore_state,model);
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
			explore_state(model,src);
		}
	}
	if (write_lts){
		Warning(info,"state space has %d levels %d states %d transitions",level,visited,trans);
	} else {
		printf("state space has %d levels %d states %d transitions\n",level,visited,trans);
	}
	if (use_vset) {
		long long size;
		long nodes;
		vset_count(visited_set,&nodes,&size);
	    	Warning(info,"%lld states represented symbolically with %ld nodes",size,nodes);
	}
	if (write_lts){
		lts_output_close(&output);
	}
	return 0;
}

