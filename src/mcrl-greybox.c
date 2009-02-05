#include <stdio.h>

#include "mcrl-greybox.h"
#include "runtime.h"
#include "rw.h"
#include "mcrl.h"
#include "step.h"
#include "at-map.h"

static ATerm label=NULL;
static ATerm *dst;

static int instances=0;
static struct lts_structure_s ltstype;
static struct edge_info e_info;
static struct state_info s_info={0,NULL,NULL};

static at_map_t termmap;
static at_map_t actionmap;

static TransitionCB user_cb;
static void* user_context;
static ATerm* src_term;
static int * src_int;

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
	int dst_p[ltstype.state_length];
	for(int i=0;i<ltstype.state_length;i++){
		if(ATisEqual(src_term[i],dst[i])){
			dst_p[i]=src_int[i];
		} else {
			dst_p[i]=ATfindIndex(termmap,dst[i]);
		}
	}
	user_cb(user_context,&lbl,dst_p);
}

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
	exit(1);
}


void MCRLinitGreybox(int argc,char *argv[],void* stack_bottom){
	ATinit(argc, argv, stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
	int i;
	int c=argc+4;
	char* cpargv[c];
	char** xargv=cpargv;
	xargv[0]=argv[0];
	xargv[1]="-alt";
	xargv[2]="rw";
	xargv[3]="-no-hash";
	xargv[4]="-conditional";
	for(i=1;i<argc;i++) xargv[i+4]=argv[i];
	MCRLsetArguments(&c, &xargv);
	RWsetArguments(&c, &xargv);
	STsetArguments(&c, &xargv);
}

int MCRLgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[ltstype.state_length];
	user_cb=cb;
	user_context=context;
	src_int=src;
	src_term=at_src;
	for(int i=0;i<ltstype.state_length;i++) {
		at_src[i]=ATfindTerm(termmap,src[i]);
	}
	return STstepSmd(at_src,&group,1);
}

int MCRLgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
	(void)model;
	ATerm at_src[ltstype.state_length];
	user_cb=cb;
	user_context=context;
	src_int=src;
	src_term=at_src;
	for(int i=0;i<ltstype.state_length;i++) {
		at_src[i]=ATfindTerm(termmap,src[i]);
	}
	return STstep(at_src);
}

static char* edge_name[1]={"action"};
static int edge_type[1]={1};
static char* MCRL_types[2]={"leaf","action"};

void MCRLloadGreyboxModel(model_t m,char*model){
	if(instances) {
		Fatal(1,error,"mCRL is limited to one instance, due to global variables.");
	}
	instances++;
	if(!MCRLinitNamedFile(model)) {
		FatalCall(1,error,"failed to open %s",model);
	}
	if (!RWinitialize(MCRLgetAdt())) {
		ATerror("Initialize rewriter");
	}
	ltstype.state_length=MCRLgetNumberOfPars();
	ltstype.visible_count=0;
	ltstype.visible_indices=NULL;
	ltstype.visible_name=NULL;
	ltstype.visible_type=NULL;
	ltstype.state_labels=0;
	ltstype.state_label_name=NULL;
	ltstype.state_label_type=NULL;
	ltstype.edge_labels=1;
	ltstype.edge_label_name=edge_name;
	ltstype.edge_label_type=edge_type;
	ltstype.type_count=2;
	ltstype.type_names=MCRL_types;
	GBsetLTStype(m,&ltstype);

	termmap=ATmapCreate(m,0,NULL,print_term,parse_term);
	actionmap=ATmapCreate(m,1,NULL,remove_quotes,NULL);

	dst=(ATerm*)malloc(ltstype.state_length*sizeof(ATerm));
	for(int i=0;i<ltstype.state_length;i++) {
		dst[i]=NULL;
	}
 	ATprotect(&label);
	ATprotectArray(dst,ltstype.state_length);
	STinitialize(noOrdering,&label,dst,callback);
	STsetInitialState();
	int temp[ltstype.state_length];
	for(int i=0;i<ltstype.state_length;i++) temp[i]=ATfindIndex(termmap,dst[i]);
	GBsetInitialState(m,temp);


	e_info.groups=STgetSummandCount();
	e_info.length=(int*)RTmalloc(e_info.groups*sizeof(int));
	e_info.indices=(int**)RTmalloc(e_info.groups*sizeof(int*));
	for(int i=0;i<e_info.groups;i++){
		int temp[ltstype.state_length];
		e_info.length[i]=STgetProjection(temp,i);
		e_info.indices[i]=(int*)RTmalloc(e_info.length[i]*sizeof(int));
		for(int j=0;j<e_info.length[i];j++) e_info.indices[i][j]=temp[j];
	}
	GBsetEdgeInfo(m,&e_info);
	GBsetStateInfo(m,&s_info);
	GBsetNextStateLong(m,MCRLgetTransitionsLong);
	GBsetNextStateAll(m,MCRLgetTransitionsAll);
}


