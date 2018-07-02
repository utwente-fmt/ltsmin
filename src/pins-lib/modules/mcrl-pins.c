#include <hre/config.h>
#include <stdio.h>
#include <string.h>

#include <mcrl.h>
#include <rw.h>
#include <step.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/modules/at-map.h>
#include <pins-lib/modules/mcrl-pins.h>

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
	Abort ("ATerror");
}


static void MCRLinitGreybox(int argc,const char *argv[],void* stack_bottom){
	ATinit(argc, (char **)argv, stack_bottom);
	ATsetWarningHandler(WarningHandler);
	ATsetErrorHandler(ErrorHandler);
#if defined(__APPLE__)
        /* KLUDGE: On OSX, mcrl at least up to 2.18.4 unconditionally
         *   sets -bundle_loader to mcrl's installation path, in
         *   <prefix>/mCRL/libexec/Rww.  Thus, linking the compiled
         *   rewriters into our application fails (-alt rww).  We can
         *   fake it to work by pretending to be mcrl's rewr
         *   executable.
         */
        const char *argv_[argc];
        argv_[0] = "/fake/path/to/rewr";
        for (int i = 1; i < argc; ++i) argv_[i] = argv[i];
        argv = argv_;
#endif
	RWsetArguments(&argc, (char ***)&argv);
	STsetArguments(&argc, (char ***)&argv);
	MCRLsetArguments(&argc, (char ***)&argv);
	if (argc > 1) {
		for(int i=1;i<argc;i++){
			Warning(lerror,"unparsed mCRL option %s",argv[i]);
		}
		Abort("Exiting");
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
		const char **argv;
		if (strstr(mcrl_args,"-confluent")||strstr(mcrl_args,"-conf-table")||strstr(mcrl_args,"-conf-compute")){
			Abort("This tool does not support tau confluence reduction.");
		}
		RTparseOptions(mcrl_args,&argc,(char***)&argv);
		MCRLinitGreybox(argc,argv,HREstackBottom());
		free(argv);    // Allocated as one block by RTparseOptions
		GBregisterLoader("tbf",MCRLloadGreyboxModel);
		Warning(debug,"mCRL language module initialized");
		return;
	}
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Abort("unexpected call to mcrl_popt");
}
struct poptOption mcrl_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , mcrl_popt , 0 , NULL , NULL },
	{ "mcrl" , 0 , POPT_ARG_STRING|POPT_ARGFLAG_SHOW_DEFAULT , &mcrl_args , 0, "pass options to the mCRL library","<mCRL options>" },
	POPT_TABLEEND
};

static ATerm label=NULL;
static ATerm *dst;

static int instances=0;
static int state_length;
static matrix_t dm_info;
static int* e_smd_map=NULL;
static matrix_t   sl_info;
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
    transition_info_t ti = GB_TI(&lbl, -1);
	int dst_p[state_length];
	for(int i=0;i<state_length;i++){
		if(ATisEqual(src_term[i],dst[i])){
			dst_p[i]=src_int[i];
		} else {
			dst_p[i]=ATfindIndex(termmap,dst[i]);
		}
	}
	user_cb(user_context,&ti,dst_p,NULL);
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
	int res=STstepSmd(at_src,s_smd_map,dm_nrows(&sl_info));
	if (res<0) Abort("error in STstepSmd")
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
	if (res<0) Abort("error in STstepSmd")
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
	int res=STstepSmd(at_src,e_smd_map,dm_nrows(&dm_info));
	if (res<0) Abort("error in STstepSmd")
	return res;
}

void MCRLloadGreyboxModel(model_t m,const char*model){
	if(instances) {
		Abort("mCRL is limited to one instance, due to global variables.");
	}
	instances++;
	char*x=strdup(model);
	if(!MCRLinitNamedFile(x)) {
		Abort("failed to open %s",model);
	}
	free(x); // strdup
	if (!RWinitialize(MCRLgetAdt())) {
		Abort("could not initialize rewriter for %s",model);
	}
	lts_type_t ltstype=lts_type_create();
	state_length=MCRLgetNumberOfPars();
	lts_type_set_state_length(ltstype,state_length);
    ATermList pars=MCRLgetListOfPars();
    for(int i=0;i<state_length;i++){
        ATerm decl=ATelementAt(pars,i);
        ATerm var=MCRLprint(ATgetArgument(decl,0));
        //ATerm type=MCRLprint(ATgetArgument(decl,1));
        //should be used instead of "leaf", future work.
        lts_type_set_state_name(ltstype,i,ATwriteToString(var));
        lts_type_set_state_type(ltstype,i,"leaf");
    }
	termmap=ATmapCreate(m,lts_type_add_type(ltstype,"leaf",NULL),NULL,print_term,parse_term);
	actionmap=ATmapCreate(m,lts_type_add_type(ltstype,LTSMIN_EDGE_TYPE_ACTION_PREFIX,NULL),NULL,remove_quotes,NULL);

	dst=(ATerm*)RTmalloc(state_length*sizeof(ATerm));
	for(int i=0;i<state_length;i++) {
		dst[i]=NULL;
	}
 	ATprotect(&label);
	ATprotectArray(dst,state_length);
	STinitialize(noOrdering,&label,dst,callback);

	int nSmds=STgetSummandCount();

	lts_type_set_edge_label_count(ltstype,1);
	lts_type_set_edge_label_name(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
	lts_type_set_edge_label_type(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);

	dm_create(&dm_info, nSmds, state_length);
	e_smd_map=(int*)RTmalloc( nSmds *sizeof(int));

	// load projection from mcrl into matrix
	for(int i=0; i < nSmds; i++)
	{
		int temp[state_length];
		int temp_len = STgetProjection(temp, i);
		for(int j=0; j < temp_len; j++)
			dm_set(&dm_info, i, temp[j]);
		e_smd_map[i] = i;
	}

	GBsetLTStype(m,ltstype);
	GBsetDMInfo(m, &dm_info);
	dm_create(&sl_info, 0, state_length);
	GBsetStateLabelInfo(m, &sl_info);

	STsetInitialState();
	int temp[state_length];
	for(int i=0;i<state_length;i++) temp[i]=ATfindIndex(termmap,dst[i]);
	GBsetInitialState(m,temp);

	GBsetNextStateLong(m,MCRLgetTransitionsLong);
	GBsetNextStateAll(m,MCRLgetTransitionsAll);
	GBsetStateLabelsAll(m,MCRLgetStateLabelsAll);
	Warning(info,"model %s loaded",model);
}



