#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

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
#else
#error "Unknown greybox provider."
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
static int use_vset_list=0;
static int use_vset_tree=0;
static int use_vset_fdd=0;
static int torx =0;

struct option options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"usage: " MODEL_TYPE "2lts-grey [options] <model>",NULL,NULL,NULL},
	{"-out",OPT_REQ_ARG,assign_string,&outputarch,"-out <archive>",
		"Specify the name of the output archive.",
		"This will be a pattern archive if <archive> contains %s",
		"and a GCF archive otherwise",NULL},
	{"-v",OPT_NORMAL,inc_int,&verbosity,NULL,"increase the level of verbosity",NULL,NULL,NULL},
	{"-q",OPT_NORMAL,log_suppress,&info,NULL,"be silent",NULL,NULL,NULL},
	{"-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
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
	lts_struct_t ltstype;
} torx_struct_t;


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

#if defined(NIPS)
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
	chunk c=GBchunkGet(context->model,context->ltstype->edge_label_type[0],lbl[0]);
	
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
	RTinit(argc,&argv);
	take_vars(&argc,argv);
#if defined(MCRL)
	MCRLinitGreybox(argc,argv,stackbottom);
#elif defined(MCRL2)
	MCRL2initGreybox(argc,argv,stackbottom);
#elif defined(NIPS)
 	ATinit(argc, argv, (ATerm*) stackbottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	NIPSinitGreybox(argc,argv);
#endif
	parse_options(options,argc,argv);
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
#if defined(MCRL)
	MCRLloadGreyboxModel(model,argv[argc-1]);
#elif defined(MCRL2)
	MCRL2loadGreyboxModel(model,argv[argc-1]);
#elif defined(NIPS)
	NIPSloadGreyboxModel(model,argv[argc-1]);
#endif

	if (verbosity >=2) {
	  fprintf(stderr,"Dependency Matrix:\n");
	  GBprintDependencyMatrix(stderr,model);
	}

	if (cache) model=GBaddCache(model);

	lts_struct_t ltstype=GBgetLTStype(model);
	N=ltstype->state_length;
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
	if (torx) {
		torx_struct_t context = { model, ltstype };
		torx_ui(&context);
		return 0;
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
	if (write_lts){
		Warning(info,"state space has %d levels %d states %d transitions",level,visited,trans);
	} else {
	  printf("state space has %d levels %d states %d transitions\n",level,visited,trans);
	  if (use_vset) {
	    long long size;
	    long nodes;
	    if (use_vset)
	      vset_count(visited_set,&nodes,&size);
	    else 
	      vset_count_tree(visited_set,&nodes,&size);
	    printf("(%lld states represented symbolically with %ld nodes)\n",size,nodes);
	  }
	}
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

