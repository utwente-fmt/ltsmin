// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>
#include <hre/user.h>
#include <dm/dm.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/dlopen-pins.h>
#include <pins-lib/dlopen-api.h>
#include <util-lib/rationals.h>
#include <hre/stringindex.h>
#include <scoop.h>

static int check_confluence=0;

static const char const_long[]="const";
static const char progress_long[]="max-progress";
#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

void prcrl_load_model(model_t model,const char*name);
void mapa_load_model(model_t model,const char*name);


static enum {
    MAX_PROGRESS_NONE=0,
    MAX_PROGRESS_TAU=1,
    MAX_PROGRESS_ALL=2
} max_progress=MAX_PROGRESS_ALL;

static void poptCallback(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
	{
	    char *argv[2];
        argv[0]=get_label();
        argv[1]=NULL;
        scoop_init(1,argv);
		return;
    }
	case POPT_CALLBACK_REASON_POST:
	{
	    GBregisterLoader("prcrl",prcrl_load_model);
	    GBregisterLoader("mapa",mapa_load_model);
		Warning(infoLong,"MAPA language module initialized");
		return;
    }
	case POPT_CALLBACK_REASON_OPTION:
	    IF_LONG(const_long){
	        Warning(info,"Definition %s",arg);
	        char*val=strchr(arg,'=');
	        if (val==NULL) {
	            Abort("illegal use of %s option",const_long);
	        }
	        *val=0;
	        val++;
	        scoop_put_constant(arg,val);
	        Warning(info,"Definition %s = %s",arg,val);
	    }
	    IF_LONG(progress_long){
	        Warning(info,"maximal progress %s",arg);
	        if (strcmp(arg,"none")==0){
	            max_progress=MAX_PROGRESS_NONE;
	            return;
	        }
	        if (strcmp(arg,"tau")==0){
	            max_progress=MAX_PROGRESS_TAU;
	            return;
	        }
	        if (strcmp(arg,"all")==0){
	            max_progress=MAX_PROGRESS_ALL;
	            return;
	        }
	        Abort("Unknown maximal progress version %s",arg);
	    }
	    // ignore all other options.
		return;
	}
	Abort("unexpected call to scoop plugin callback");
}

struct poptOption mapa_options[]= {
    { NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_PRE|POPT_CBFLAG_POST , poptCallback , 0 , NULL , NULL },
    { const_long , 0 , POPT_ARG_STRING , NULL , 0, "define constant <var> as <val>","<var>=<val>" },
    { progress_long , 0 , POPT_ARG_STRING , NULL , 0,
     "Specify the set of actions for which maximal progress has to be enabled."
     " The default is 'all', meaning that all actions are prioritised."
     " The other settings are 'tau', to prioritise just the tau steps"
     " and 'none' to disable maximal progress","<actions>"},
    { "confluence", 0, POPT_ARG_VAL, &check_confluence, 1, "detect confluent summands and write confluent matrix", NULL },
	POPT_TABLEEND
};

static string_index_t reach_actions;
static int action_type;
static int state_length;
static int *state_type;
static model_t main_model;
static int *cb_dest;
static int cb_label[5];

static TransitionCB user_cb;
static void* cb_ctx;

void report_reach(char* str){
    SIput(reach_actions,str);
}

void write_prob_label(char *str,int*label){
    label[2]=atoi(str);
    char* ptr=strrchr(str,'/');
    if (ptr==NULL) {
        label[3]=1;
    } else {
        ptr++;
        label[3]=atoi(ptr);
    }
}

void write_rate_label(char *str,int*label){
    Warning(infoLong,"label %s=",str);
    float f=atof(str+5);
    rationalize32(f,(uint32_t*)label+2,(uint32_t*)label+3);
    Warning(infoLong,"  %d/%d",label[2],label[3]);
}
int get_numerator(char *str){
    return atoi(str);
}

int get_denominator(char *str){
    char* ptr=strrchr(str,'/');
    if (ptr==NULL) return 1;
    ptr++;
    return atoi(ptr);
}

void write_reward_label(char *str,int*label){
    Warning(infoLong,"reward label %s=",str);
    float f=atof(str);
    if(f>0){
        rationalize32(f,(uint32_t*)label+0,(uint32_t*)label+1);
        Warning(infoLong,"  %d/%d",label[0],label[1]);
    }
}

int action_get_index(char* val){
    int res=GBchunkPut(main_model,action_type,chunk_str(val));
//    Warning(info,"get action index for %s : %d",val,res);
    return res;
}

int term_get_index(int pos,char* val){
    int res=GBchunkPut(main_model,state_type[pos],chunk_str(val));
//    Warning(info,"get %d index (%d) for %s : %d",pos,state_type[pos],val,res);
    return res;
}

char* term_get_value(int pos,int idx){
    chunk res=GBchunkGet(main_model,state_type[pos],idx);
//    Warning(info,"lookup %d index (%d) for %d : %s",pos,state_type[pos],idx,res.data);
    return res.data;
}


void prcrl_callback(void){
//	int lbl=ATfindIndex(actionmap,label);
//    transition_info_t ti = GB_TI(&lbl, -1);
    transition_info_t ti = GB_TI(cb_label, -1);
	user_cb(cb_ctx,&ti,cb_dest,NULL);
}

static void discard_callback(void*context,transition_info_t*info,int*dst,int*src){
    (void)context;
    (void)info;
    (void)dst;
    (void)src;
}

#define HsStablePtr void*

typedef struct prcrl_context {
    model_t cached;
    HsStablePtr spec;
    string_index_t reach_actions;
    matrix_t reach_info;
} *prcrl_context_t;

/*
static int PRCRLgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
    prcrl_context_t ctx=GBgetContext(model);
    cb_ctx=context;
    user_cb=cb;
    int res=prcrl_explore(ctx->spec,src,cb_dest,cb_label);
    return res;
}
*/

static int PRCRLdelegateTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
    prcrl_context_t ctx=GBgetContext(model);
    return GBgetTransitionsLong(ctx->cached,group,src,cb,context);
}

static int PRCRLgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
    prcrl_context_t ctx=GBgetContext(model);
    cb_ctx=context;
    user_cb=cb;
    int res=prcrl_explore_long(ctx->spec,group,src,cb_dest,cb_label);
    return res;
}

/*
static int label_actions(char*edge_class){
    return SIlookup(reach_actions,edge_class)>=0;
}
*/

static int check_goal(model_t self,int label,int*src){
    (void)label;
    prcrl_context_t ctx=GBgetContext(self);
    matrix_t *dm_reach=&ctx->reach_info;
    int N=dm_ncols(dm_reach);
    for (int i=0;i<N;i++){
      if (dm_is_set(dm_reach,0,i)){
        if (GBgetTransitionsLong(ctx->cached,i,src,discard_callback,NULL)){
            // A reach transition is enabled, goal is true.
            return 1;
        }
      }
    }
    // No reach transition is enabled, goal is false.
    return 0;
}

void common_load_model(model_t model,const char*name,int mapa){
    Warning(infoLong,"Loading %s",name);
    prcrl_context_t context=RT_NEW(struct prcrl_context);
    GBsetContext(model,context);
    main_model=model;
    context->reach_actions=reach_actions=SIcreate();
    if (mapa){
        context->spec=scoop_load_mapa((char*)name);
    } else {
        context->spec=scoop_load_prcrl((char*)name);
    }

   	lts_type_t ltstype=lts_type_create();
   	int bool_type=lts_type_put_type(ltstype,"Bool",LTStypeChunk,NULL);


	int N=prcrl_pars(context->spec);
	int nSmds=prcrl_summands(context->spec);
	state_length=N;
	lts_type_set_state_length(ltstype,N);
	Warning(infoLong,"spec has %d parameters",N);
	state_type=(int*)RTmalloc(N*sizeof(int));
	cb_dest=(int*)RTmalloc(N*sizeof(int));
	for(int i=0;i<N;i++){
	    char* v=prcrl_par_name(context->spec,i);
	    char* t=prcrl_par_type(context->spec,i);
	    Warning(infoLong,"%s: %s",v,t);
	    state_type[i]=lts_type_put_type(ltstype,t,LTStypeChunk,NULL);
	    lts_type_set_state_name(ltstype,i,v);
	    lts_type_set_state_type(ltstype,i,t);
	}	
	// TODO: set proper edge types!
	action_type=lts_type_put_type(ltstype,"action",LTStypeChunk,NULL);
    lts_type_put_type(ltstype,"nat",LTStypeDirect,NULL);
    lts_type_put_type(ltstype,"pos",LTStypeDirect,NULL);

    lts_type_set_edge_label_count(ltstype,4);
    lts_type_set_edge_label_name(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_type_set_edge_label_type(ltstype,0,LTSMIN_EDGE_TYPE_ACTION_PREFIX);
    lts_type_set_edge_label_name(ltstype,1,"group");
    lts_type_set_edge_label_type(ltstype,1,"nat");
    lts_type_set_edge_label_name(ltstype,2,"numerator");
    lts_type_set_edge_label_type(ltstype,2,"nat");
    lts_type_set_edge_label_name(ltstype,3,"denominator");
    lts_type_set_edge_label_type(ltstype,3,"pos");
    
    static matrix_t class_matrix;
    dm_create(&class_matrix,3,nSmds);

    int reach_smds=0;
    static matrix_t sl_info;
    dm_create(&sl_info, 1, state_length);
    dm_create(&context->reach_info, 1, nSmds);
    static matrix_t conf_info;
    if(check_confluence){
        Warning(info,"creating confluence matrix");
        dm_create(&conf_info, 3, nSmds);
        // row 0 are confluent
        // row 1 are silent
        // row 2 are non-confluence markers
        // a state with precisely one silent step and no non-confluence markers is confluent too.
    }
    for(int i=0;i<nSmds;i++){
        char *action=prcrl_get_action(context->spec,i);
        Warning(infoLong,"summand %d is a %s summand",i,action);
        if (SIlookup(reach_actions,action)>=0 || strcmp(action,"reachConditionAction")==0){
            Warning(infoLong,"summand %d is a %s reach marked summand",i,action);
            reach_smds++;
            dm_set(&context->reach_info, 0, i);
            for(int j=0;j<state_length;j++){
                if (prcrl_is_used(context->spec,i,j)){
                    dm_set(&sl_info, 0, j);
                }
            }
        }
        if (strcmp(action,"tau")==0) {
            if(check_confluence){ // mark entry as silent
                dm_set(&conf_info,1,i);
            }
            dm_set(&class_matrix,0,i);
        } else if (strncmp(action,"rate",4)==0) {
            dm_set(&class_matrix,2,i);
            // rate steps do not make a tau step non-confluent
        } else {
            if (strcmp(action,"reachConditionAction")!=0) {
                dm_set(&class_matrix,1,i);
                if(check_confluence){ // other steps make tau steps non-confluent
                    dm_set(&conf_info,2,i);
                }
            }
        }
    }

	GBsetLTStype(model,ltstype);
    GBchunkPutAt(model,bool_type,chunk_str("F"),0);
    GBchunkPutAt(model,bool_type,chunk_str("T"),1);

    GBsetMatrix(model,LTSMIN_EDGE_TYPE_ACTION_CLASS,&class_matrix,PINS_STRICT,PINS_INDEX_OTHER,PINS_INDEX_GROUP);
    
    if (max_progress != MAX_PROGRESS_NONE){
        static matrix_t progress_matrix;
        dm_create(&progress_matrix,3,3);
        dm_set(&progress_matrix,0,2);
        if (max_progress == MAX_PROGRESS_ALL) dm_set(&progress_matrix,1,2);
        int id=GBsetMatrix(model,"inhibit",&progress_matrix,PINS_STRICT,PINS_INDEX_OTHER,PINS_INDEX_OTHER);
        Warning(info,"inhibit matrix registered as %d",id);
    }
    
	//NOTE in older ghc: int == int64 and NOT int.
	
	int state[N];
	prcrl_get_init(context->spec,state);
    GBsetInitialState(model,state);
    
//    GBsetNextStateAll(model,PRCRLgetTransitionsAll);
    
    static matrix_t dm_info;
    Warning(info,"spec has %d summands",nSmds);
    dm_create(&dm_info, nSmds, state_length);
    for(int i=0;i<nSmds;i++){
        for(int j=0;j<state_length;j++){
            if (prcrl_is_used(context->spec,i,j)){
                dm_set(&dm_info, i, j);
            }
        }
    }
    GBsetDMInfo(model, &dm_info);
    GBsetNextStateLong(model,PRCRLdelegateTransitionsLong);
    
    model_t raw_model=GBcreateBase();
    GBcopyChunkMaps(raw_model,model);
    GBsetLTStype(raw_model,ltstype);
    GBsetContext(raw_model,context);
    GBsetInitialState(raw_model,state);
    GBsetDMInfo(raw_model, &dm_info);
    GBsetNextStateLong(raw_model,PRCRLgetTransitionsLong);
    context->cached=GBaddCache(raw_model);

    if (reach_smds>0){
        Warning(info,"adding goal state label");
        lts_type_set_state_label_count(ltstype,1);
        lts_type_set_state_label_name(ltstype,0,"goal");
        lts_type_set_state_label_type(ltstype,0,"Bool");
        GBsetStateLabelInfo(model, &sl_info);
        GBsetStateLabelLong(model,check_goal);
    }

    GBsetDefaultFilter(model,SSMcreateSWPset("goal;action;group;numerator;denominator"));

    if(check_confluence){
        Warning(info,"setting confluence matrix");
        HsStablePtr conf;
        conf=get_confluent_summands(context->spec);
        while(!empty_conf(conf)){
            int i=head_conf(conf);
            dm_unset(&conf_info, 1, i); // remove confluent from silent
            dm_set(&conf_info, 0, i);
            conf=tail_conf(conf);
        }
        GBsetMatrix(model,"confluent",&conf_info,PINS_STRICT,PINS_INDEX_OTHER,PINS_INDEX_GROUP);
    }

	Warning(info,"model %s loaded",name);
}

void prcrl_load_model(model_t model,const char*name){
    common_load_model(model,name,0);
}

void mapa_load_model(model_t model,const char*name){
    common_load_model(model,name,1);
}



