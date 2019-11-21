// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fcntl.h>
#include <limits.h>
#include <ltdl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <pins-lib/dlopen-api.h>
#include <pins-lib/modules/dlopen-pins.h>
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
		GBregisterLoader("dll",PINSpluginLoadGreyboxModel);
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

void PINSpluginLoadGreyboxModel(model_t model,const char*name){
    void* dlHandle = RTdlopen(name);
    char* pins_name=RTdlsym(name,dlHandle,"pins_plugin_name");
    Warning(info,"loading model %s",pins_name);
    init_proc init=RTtrydlsym(dlHandle,"init");
    if (init!=NULL){
        Warning(info,"Initializing %s plugin",pins_name);
        char *argv[2];
        argv[0]=get_label();
        argv[1]=NULL;
        init(1,argv);
    }
    
    pins_model_init_proc model_init=RTtrydlsym(dlHandle,"pins_model_init");
    if (model_init!=NULL) {
        model_init(model);
    } else {
        Abort("Could not find a model.");
    }
    
	Warning(info,"completed loading model %s",pins_name);
}

