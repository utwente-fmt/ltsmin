// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <pins-lib/dlopen-pins.h>
#include <pins-lib/dlopen-api.h>
#include <hre/stringindex.h>
#include <util-lib/tables.h>

static const char loader_long[]="loader";
#define IF_LONG(long) if(((opt->longName)&&!strcmp(opt->longName,long)))

void PINSpluginLoadLanguageModule(const char *name);

static void plugin_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		Warning(info,"Registering PINS so language module");
		GBregisterLoader("so",PINSpluginLoadGreyboxModel);
		return;
	case POPT_CALLBACK_REASON_OPTION:
	    IF_LONG(loader_long){
	        Warning(info,"Loading language module %s",arg);
	        PINSpluginLoadLanguageModule(arg);
	        return;
	    }
		break;
	}
	Abort("unexpected call to dlopen plugin callback");
}


#define LOADER_OPTION_SIZE 32

static struct poptOption table_end=POPT_TABLEEND;

struct poptOption pins_plugin_options[LOADER_OPTION_SIZE]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST , plugin_popt , 0 , NULL , NULL },
	{ loader_long , 0 , POPT_ARG_STRING , NULL , 0, "load language module","<plugin>" },
	POPT_TABLEEND
};

void PINSpluginLoadLanguageModule(const char *name){
    void* dlHandle = RTdlopen(name);
    char* pins_name=RTdlsym(name,dlHandle,"pins_plugin_name");
    
    init_proc init=RTtrydlsym(dlHandle,"init");
    if (init!=NULL){
        Warning(info,"Initializing %s plugin",pins_name);
        char *argv[2];
        argv[0]=get_label();
        argv[1]=NULL;
        init(1,argv);
    }

    loader_record_t* pins_loaders=RTdlsym(name,dlHandle,"pins_loaders");
    for(int i=0;pins_loaders[i].extension!=NULL;i++){
        Warning(info,"registering loader for %s",pins_loaders[i].extension);
        GBregisterLoader(pins_loaders[i].extension,pins_loaders[i].loader);
    }
    struct poptOption* loader_options=RTtrydlsym(dlHandle,"pins_options");
    if (loader_options!=NULL){
        int i=0;

        for(;i<LOADER_OPTION_SIZE;i++){
            if (memcmp(&pins_plugin_options[i],&table_end,sizeof(table_end))==0) break;
        }
        if (i<(LOADER_OPTION_SIZE-1)){
            Warning(info,"inserting options at position %d",i);
            char temp[1024];
            sprintf(temp,"%s Options",pins_name);
            char*header=strdup(temp);
            struct poptOption include= { NULL, 0 , POPT_ARG_INCLUDE_TABLE, loader_options , 0 , header , NULL};
            pins_plugin_options[i]=include;
            pins_plugin_options[i+1]=table_end;
        } else {
            Abort("Too many loaders with options.");
        }
    }
    Warning(info,"finished loading %s",name);
}

static int MCRLgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
    return 0;
}

static void* dlHandle = NULL;

void PINSpluginLoadGreyboxModel(model_t model,const char*name){
    dlHandle=RTdlopen(name);
    // TODO: close handle when finished.
    
	lts_type_t ltstype=lts_type_create();
	int int_type=lts_type_put_type(ltstype,"int",LTStypeDirect,NULL);
		
	int state_length=2;
	lts_type_set_state_length(ltstype,state_length);
	lts_type_set_state_name(ltstype,0,"x");
	lts_type_set_state_type(ltstype,0,"int");
	lts_type_set_state_name(ltstype,1,"y");
	lts_type_set_state_type(ltstype,1,"int");

	lts_type_set_edge_label_count(ltstype,0);
	
	GBsetLTStype(model,ltstype);

    matrix_t dm_info;
    dm_create(&dm_info, 1, state_length);
    dm_set(&dm_info, 0, 0);
    GBsetDMInfo(model, &dm_info);
    
    matrix_t sl_info;
    dm_create(&sl_info, 0, state_length);
    GBsetStateLabelInfo(model, &sl_info);

    int temp[state_length];
	GBsetInitialState(model,temp);

	GBsetNextStateAll(model,MCRLgetTransitionsAll);
	Warning(info,"model %s loaded",name);
}

