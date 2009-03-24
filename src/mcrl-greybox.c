#include "config.h"
#include <stdio.h>
#include <string.h>

#include "mcrl-greybox.h"
#include "runtime.h"
#include "rw.h"
#include "mcrl.h"
#include "step.h"
#include "at-map.h"


/**
\brief Flag that tells the mCRL grey box loader to pass state variable names.
 */
#define STATE_VISIBLE 0x01
static int flags=0;
static char *mcrl_args="-alt rw";


static void WarningHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(info);
	if (f) {
		fprintf(f,"MCRL grey box: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
}
     
static void ErrorHandler(const char *format, va_list args) {
	FILE* f=log_get_stream(error);
	if (f) {
		fprintf(f,"MCRL grey box: ");
		ATvfprintf(f, format, args);
		fprintf(f,"\n");
	}
	Fatal(1,error,"ATerror");
	exit(EXIT_FAILURE);
}


static void MCRLinitGreybox(int argc,char *argv[],void* stack_bottom){
	ATinit(argc, argv, stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	RWsetArguments(&argc, &argv);
	STsetArguments(&argc, &argv);
	MCRLsetArguments(&argc, &argv);
	if (argc!=1) {
		for(int i=0;i<argc;i++){
			Warning(error,"unparsed mCRL option %s",argv[i]);
		}
		Fatal(1,error,"Exiting");
	}
}

static void mcrl_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST: {
		int argc;
		char **argv;
		if (strstr(mcrl_args,"-confluent")||strstr(mcrl_args,"-conf-table")||strstr(mcrl_args,"-conf-compute")){
			Fatal(1,error,"This tool does not support tau confluence reduction.");
		}
		RTparseOptions(mcrl_args,&argc,&argv);
		MCRLinitGreybox(argc,argv,RTstackBottom());
		GBregisterLoader("tbf",MCRLloadGreyboxModel);
		Warning(debug,"mCRL language module initialized");
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to mcrl_popt");
}
struct poptOption mcrl_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , mcrl_popt , 0 , NULL , NULL },
	{ "state-names" , 0 , POPT_ARG_VAL|POPT_ARGFLAG_OR , &flags , STATE_VISIBLE , "make the names of the state parameters visible" ,NULL},
	{ "mcrl" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &mcrl_args , 0, "pass options to the mCRL library","<mCRL options>" },
	POPT_TABLEEND
};

static ATerm label=NULL;
static ATerm *dst;

static int instances=0;
static int state_length;
static struct edge_info e_info;
static int* e_smd_map=NULL;
static struct state_info s_info={0,NULL,NULL};
static int* s_smd_map=NULL;

static at_map_t termmap;
static at_map_t actionmap;

static TransitionCB user_cb;
static void* user_context;
static ATerm* src_term;
static int * src_int;

static int next_label=0;
static int *user_labels;

static char* remove_quotes(void *dummy,ATerm t){
	(void)dummy;
	char* temp=ATwriteToString(t);
	temp++;
	temp[strlen(temp)-1]=0;
	return temp;
}

static char* print_term(void *dummy,ATerm t){
	(void)dummy;
	return ATwriteToString(MCRLprint(t));
}

static ATerm parse_term(void *dummy,char*str){
	(void)dummy;
	return MCRLparse(str);
}

static void callback(void){
	int lbl=ATfindIndex(actionmap,label);
	int dst_p[state_length];
	for(int i=0;i<state_length;i++){
		if(ATisEqual(src_term[i],dst[i])){
			dst_p[i]=src_int[i];
		} else {
			dst_p[i]=ATfindIndex(termmap,dst[i]);
		}
	}
	user_cb(user_context,&lbl,dst_p);
}


static void MCRLgetStateLabelsAll(model_t model,int*state,int*labels){
	(void)model;
	ATerm at_src[state_length];
	next_label=0;
	user_cb=NULL;
	user_labels=labels;
	for(int i=0;i<state_length;i++) {
		at_src[i]=ATfindTerm(termmap,state[i]);
	}
	int res=STstepSmd(at_src,s_smd_map,s_info.labels);
	if (res<0) Fatal(1,error,"error in STstepSmd")
}

static int MCRLgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[state_length];
	user_cb=cb;
	user_context=context;
	src_int=src;
	src_term=at_src;
	for(int i=0;i<state_length;i++) {
		at_src[i]=ATfindTerm(termmap,src[i]);
	}
	int res=STstepSmd(at_src,e_smd_map+group,1);
	if (res<0) Fatal(1,error,"error in STstepSmd")
	return res;
}

static int MCRLgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[state_length];
	user_cb=cb;
	user_context=context;
	src_int=src;
	src_term=at_src;
	for(int i=0;i<state_length;i++) {
		at_src[i]=ATfindTerm(termmap,src[i]);
	}
	int res=STstepSmd(at_src,e_smd_map,e_info.groups);
	if (res<0) Fatal(1,error,"error in STstepSmd")
	return res;
}

void MCRLloadGreyboxModel(model_t m,const char*model){
	if(instances) {
		Fatal(1,error,"mCRL is limited to one instance, due to global variables.");
	}
	instances++;
	char*x=strdup(model);
	if(!MCRLinitNamedFile(x)) {
		FatalCall(1,error,"failed to open %s",model);
	}
	free(x);
	if (!RWinitialize(MCRLgetAdt())) {
		Fatal(1,error,"could not initialize rewriter for %s",model);
	}
	lts_type_t ltstype=lts_type_create();
	state_length=MCRLgetNumberOfPars();
	lts_type_set_state_length(ltstype,state_length);
	if (flags & STATE_VISIBLE){
		Warning(info,"state variables are visible.");
		ATermList pars=MCRLgetListOfPars();
		for(int i=0;i<state_length;i++){
			ATerm decl=ATelementAt(pars,i);
			ATerm var=MCRLprint(ATgetArgument(decl,0));
			ATerm type=MCRLprint(ATgetArgument(decl,1));
			lts_type_set_state_name(ltstype,i,ATwriteToString(var));
			lts_type_set_state_type(ltstype,i,"leaf");
			Warning(info,"parameter %8s: %8s",lts_type_get_state_name(ltstype,i),ATwriteToString(type));
		}
	} else {
		Warning(info,"hiding the state.");
		for(int i=0;i<state_length;i++){
			lts_type_set_state_type(ltstype,i,"leaf");
		}
	}
	termmap=ATmapCreate(m,lts_type_add_type(ltstype,"leaf",NULL),NULL,print_term,parse_term);
	actionmap=ATmapCreate(m,lts_type_add_type(ltstype,"action",NULL),NULL,remove_quotes,NULL);

	dst=(ATerm*)malloc(state_length*sizeof(ATerm));
	for(int i=0;i<state_length;i++) {
		dst[i]=NULL;
	}
 	ATprotect(&label);
	ATprotectArray(dst,state_length);
	STinitialize(noOrdering,&label,dst,callback);

	int nSmds=STgetSummandCount();
	{
		e_info.groups=nSmds;
	}
	lts_type_set_edge_label_count(ltstype,1);
	lts_type_set_edge_label_name(ltstype,0,"action");
	lts_type_set_edge_label_type(ltstype,0,"action");
	e_info.length=(int*)RTmalloc(e_info.groups*sizeof(int));
	e_smd_map=(int*)RTmalloc(e_info.groups*sizeof(int));
	e_info.indices=(int**)RTmalloc(e_info.groups*sizeof(int*));
	int next_edge=0;
	for(int i=0;i<nSmds;i++){
		int temp[state_length];
		e_info.length[next_edge]=STgetProjection(temp,i);
		e_info.indices[next_edge]=(int*)RTmalloc(e_info.length[next_edge]*sizeof(int));
		for(int j=0;j<e_info.length[next_edge];j++) e_info.indices[next_edge][j]=temp[j];
		e_smd_map[next_edge]=i;
		next_edge++;
	}
	GBsetLTStype(m,ltstype);
	GBsetEdgeInfo(m,&e_info);
	GBsetStateInfo(m,&s_info);

	STsetInitialState();
	int temp[state_length];
	for(int i=0;i<state_length;i++) temp[i]=ATfindIndex(termmap,dst[i]);
	GBsetInitialState(m,temp);

	GBsetNextStateLong(m,MCRLgetTransitionsLong);
	GBsetNextStateAll(m,MCRLgetTransitionsAll);
	GBsetStateLabelsAll(m,MCRLgetStateLabelsAll);
	Warning(info,"model %s loaded",model);
}



