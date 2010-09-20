#include "runtime.h"
#include "spinja-greybox.h"
#include "dm/dm.h"
#include "chunk_support.h"
#include "unix.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

void
print_state(char* s) {
	for(int i=0; i<16; i++) {
	printf("%d", ((char*)s)[i]);
	} printf("\n");
}

// spinja ltsmin interface functions
void        (*spinja_get_initial_state)(int *to);
next_method_grey_t spinja_get_successor;

int         (*spinja_get_state_size)();
int         (*spinja_get_transition_groups)();

static void spinja_popt(poptContext con,
               enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		GBregisterLoader("spinja", SpinJaloadGreyboxModel);
		Warning(info,"Precompiled spinja module initialized");
		return;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to spinja_popt");
}

struct poptOption spinja_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&spinja_popt, 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
    int todo;
} *gb_context_t;

static lts_type_t ltstype;
static matrix_t dm_info;
static matrix_t sl_info;
static void* dlHandle = NULL;

#define SYSFAIL(cond,...)                                               \
    do { if (cond) FatalCall(__VA_ARGS__) Fatal(__VA_ARGS__); } while (0)
void SpinJacompileGreyboxModel(model_t model, const char *filename){
    (void)model;
    (void)filename;
    FatalCall(1, error, "Unimplemented: automatic compilation of promela code, please do this manually");
}
#undef SYSFAIL

void SpinJaloadGreyboxModel(model_t model, const char *filename){
	gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
	GBsetContext(model,ctx);

    // Open spinja file
    char abs_filename[PATH_MAX];
	char *ret_filename = realpath(filename, abs_filename);
    if (ret_filename) {
        dlHandle = dlopen(abs_filename, RTLD_LAZY);
        if (dlHandle == NULL)
        {
            Fatal (1, error, "%s, Library \"%s\" is not reachable", dlerror(), filename);
            return;
        }
    } else {
        Fatal (1, error, "%s, Library \"%s\" is not found", dlerror(), filename);
    }

    // load dynamic library functionality
    spinja_get_initial_state = (void(*)(int*))
    RTdlsym( filename, dlHandle, "spinja_get_initial_state" );
    spinja_get_successor = (next_method_grey_t)
    RTdlsym( filename, dlHandle, "spinja_get_successor" );

    spinja_get_state_size = (int(*)())
    RTdlsym( filename, dlHandle, "spinja_get_state_size" );

    spinja_get_transition_groups = (int(*)())
    RTdlsym( filename, dlHandle, "spinja_get_transition_groups" );

    // get ltstypes
    int state_length = spinja_get_state_size();
    ltstype=lts_type_create();

    // adding types
    int ntypes = state_length;
    for(int i=0; i < ntypes; i++) {
        char type_name[1024];
        sprintf(type_name, "dummy type %d", i);
        if (lts_type_add_type(ltstype,type_name,NULL) != i) {
            Fatal(1,error,"wrong type number");
        }
    }

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for(int i=0; i < state_length; ++i)
    {
        char name[1024];
        sprintf(name, "sv%d", i);
        const int   type = i;
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }

    GBsetLTStype(model, ltstype);

    int ngroups = spinja_get_transition_groups();
	dm_create(&dm_info, ngroups, state_length);
    for(int i=0; i < dm_nrows(&dm_info); i++) {
		for(int j=0; j<state_length; j++) {
            dm_set(&dm_info, i, j);
        }
    }
    GBsetDMInfo(model, &dm_info);

    // there are no state labels
    dm_create(&sl_info, 0, state_length);
    GBsetStateLabelInfo(model, &sl_info);

    // get initial state
	int state[state_length];
    spinja_get_initial_state(state);
    GBsetInitialState(model,state);

	printf("INIT STATE SET:\n"); fflush(stdout);
	print_state((char*)state);

//  GBsetNextStateAll  (model, spinja_get_successor_all);
    GBsetNextStateLong (model, spinja_get_successor);
}

