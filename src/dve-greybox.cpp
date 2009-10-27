#include "system/state.hh"
#include <iostream>
#include <fstream>
#include "common/array.hh"

using namespace divine;

namespace divine {

    class succ_container_t: public divine::array_t<state_t>
    {
    public:
	//!A constructor (only calls a constructor of array_t<state_t> with
	//! parameters 4096 (preallocation) and 16 (allocation step).
    succ_container_t(): array_t<state_t>(4096, 16) {}
	//!A destructor.
	~succ_container_t() {}
    };
};


// added interface functions
bool        (*lib_system_with_property)();
size_t      (*lib_get_state_variable_count)();
const char* (*lib_get_state_variable_name)(int var);
size_t      (*lib_get_state_variable_type_count)();
const char* (*lib_get_state_variable_type_name)(int type);
const int   (*lib_get_state_variable_type)(int var);
size_t      (*lib_get_state_variable_type_value_count)(int type);
const char* (*lib_get_state_variable_type_value)(int type, int value);
void        (*lib_project_state_to_int_array)(state_t state, int* proj);
void        (*lib_project_int_array_to_state)(int* proj, state_t state);
int*        (*lib_get_transition_proj)(int trans);
int         (*lib_get_transition_succ)(size_int_t transition, state_t state, succ_container_t & succ_container);
int         (*lib_get_transition_count)();
divine::state_t (*lib_new_state)();

// original interface functions
int         (*lib_get_succ)(state_t, succ_container_t &);
bool        (*lib_is_accepting)(state_t);
divine::state_t (*lib_get_initial_state)();
void        (*lib_print_state)(state_t, std::ostream & );
bool        (*lib_is_in_accepting_component)(state_t state);

extern "C" {

#include "runtime.h"
#include "dve-greybox.h"
#include "dm/dm.h"
#include "chunk_support.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

static void dve_popt(poptContext con,
               enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		GBregisterLoader("dve", DVEcompileGreyboxModel);
		GBregisterLoader("dveC",DVEloadGreyboxModel);
		Warning(info,"Precompiled divine module initialized");
		return;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to dve_popt");
}

struct poptOption dve_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&dve_popt, 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
    int todo;
} *gb_context_t;

static void divine_get_initial_state(int* state)
{
    divine::state_t s = lib_get_initial_state();
    lib_project_state_to_int_array(s, state);
}

static lts_type_t ltstype;
static matrix_t dm_info;
static matrix_t sl_info;
static divine::succ_container_t cb_cont;
static void* dlHandle = NULL;
static char templatename[] = "/tmp/ltsmin-XXXXXX";

static int
succ_callback(TransitionCB cb, void* context)
{
    int dummy = 42; // dummy to work with --cache
    int result = cb_cont.size();
    for(size_t i=0; i < (size_t)result;++i)
    {
        int dst[lib_get_state_variable_count()];
        lib_project_state_to_int_array(cb_cont[i], dst);
        cb(context, &dummy, dst);
    }
    cb_cont.clear();
    return result;
}

static int
divine_get_transitions_all(model_t self, int*src, TransitionCB cb, void*context)
{
    (void)self;
    divine::state_t s = lib_new_state();
    lib_project_int_array_to_state(src, s);
    lib_get_succ(s, cb_cont);
    return succ_callback(cb, context);
}

static int
divine_get_transitions_long(model_t self, int group, int*src, TransitionCB cb, void*context)
{
    (void)self;
    divine::state_t s = lib_new_state();
    lib_project_int_array_to_state(src, s);
    lib_get_transition_succ(group, s, cb_cont);
    return succ_callback(cb, context);
}

void DVEexit()
{
    // close dveC library
    if (dlHandle != NULL)
    {
        dlclose(dlHandle);

        char rmcmd[4096];
        if (snprintf(rmcmd, sizeof rmcmd, "rm -rf %s", templatename) < sizeof rmcmd)
        {
            // remove!
            system(rmcmd);
        }
    }
}

void DVEcompileGreyboxModel(model_t model, const char *filename){
    // check file exists
    struct stat st;
    if (stat(filename, &st) != 0)
        FatalCall (1, error, "File not found: %s ", filename);

    // check /tmp
    if (stat("/tmp", &st))
        FatalCall (1, error, "Can't access /tmp for temporary compilation");

    // get temporary directory
    char* tmpdir = mkdtemp(templatename);
    if (!tmpdir)
        FatalCall (1, error, "Can't create temporary directory for compilation");

    char command[4096];
    if (snprintf(command, sizeof command, "cp %s %s", filename, tmpdir) < sizeof command)
    {
        system(command);

        // compile the dve model
        if (snprintf(command, sizeof command, "divine.precompile %s/%s", tmpdir, filename) < sizeof command)
        {
            system(command);

            // check existence dveC model
            snprintf(command, sizeof command, "%s/%sC", tmpdir, filename);
            if (stat(command, &st) != 0)
            {
                FatalCall (1, error, "Something went wrong with creation of %s ", command);
            }

            // all good, continue
            atexit(DVEexit); // hopefully this works :), if not, keep garbage

            DVEloadGreyboxModel(model, command);
            return;
        } else {
            FatalCall (1, error, "Problems occured when compiling %s/%s", tmpdir, filename);
        }
    }
    FatalCall (1, error, "Can't copy, paths too long ");
}

void DVEloadGreyboxModel(model_t model, const char *filename){
	gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
	GBsetContext(model,ctx);

    // Open dveC file
    char* abs_filename = realpath(filename, NULL);
    if (abs_filename) {
        dlHandle = dlopen(abs_filename, RTLD_LAZY);
        free(abs_filename);
        if (dlHandle == NULL)
        {
            FatalCall (1, error, "%s, Library \"%s\" is not reachable", dlerror(), filename);
            return;
        }
    } else {
        FatalCall (1, error, "%s, Library \"%s\" is not found", dlerror(), filename);
    }

    // get functions
    lib_get_succ = (int(*)(divine::state_t, divine::succ_container_t &))
	dlsym( dlHandle, "lib_get_succ");
    lib_is_accepting = (bool(*)(divine::state_t))
	dlsym( dlHandle, "lib_is_accepting");
    lib_is_in_accepting_component = (bool(*)(divine::state_t))
	dlsym( dlHandle, "lib_is_in_accepting_component");
    lib_get_initial_state = (divine::state_t(*)())
	dlsym( dlHandle, "lib_get_initial_state");
    lib_print_state = (void(*)(divine::state_t, std::ostream &))
	dlsym( dlHandle, "lib_print_state");

    if (lib_get_succ == NULL || lib_is_accepting == NULL ||
        lib_is_in_accepting_component == NULL || lib_get_initial_state == NULL ||
        lib_print_state == NULL) {
        FatalCall (1, error, "Library \"%s\" doesn't export the required functions", filename);
    }

    // added interface functions
    lib_system_with_property = (bool (*)())
    dlsym( dlHandle, "lib_system_with_property");
    lib_get_state_variable_count = (size_t (*)())
    dlsym( dlHandle, "lib_get_state_variable_count");
    lib_get_state_variable_name = (const char* (*)(int var))
    dlsym( dlHandle, "lib_get_state_variable_name" );
    lib_get_state_variable_type_count = (size_t (*)())
    dlsym( dlHandle, "lib_get_state_variable_type_count");
    lib_get_state_variable_type_name = (const char* (*)(int type))
    dlsym( dlHandle, "lib_get_state_variable_type_name");
    lib_get_state_variable_type  = (const int (*)(int var))
    dlsym( dlHandle, "lib_get_state_variable_type");
    lib_get_state_variable_type_value_count = (size_t (*)(int))
    dlsym( dlHandle, "lib_get_state_variable_type_value_count");
    lib_get_state_variable_type_value = (const char* (*)(int type, int value))
    dlsym( dlHandle, "lib_get_state_variable_type_value");
    lib_project_state_to_int_array = (void (*)(state_t state, int* proj))
    dlsym( dlHandle, "lib_project_state_to_int_array");
    lib_project_int_array_to_state = (void (*)(int* proj, state_t state))
    dlsym( dlHandle, "lib_project_int_array_to_state");
    lib_get_transition_proj = (int* (*)(int trans))
    dlsym( dlHandle, "lib_get_transition_proj");
    lib_get_transition_succ = (int (*)(size_int_t transition, state_t state, succ_container_t & succ_container))
    dlsym( dlHandle, "lib_get_transition_succ");
    lib_get_transition_count = (int (*)())
    dlsym( dlHandle, "lib_get_transition_count");
    lib_new_state = (divine::state_t (*)())
    dlsym( dlHandle, "lib_new_state");

    // test dveC file
    if (lib_system_with_property == NULL || lib_get_state_variable_count == NULL ||
        lib_get_state_variable_name == NULL || lib_get_state_variable_type_count == NULL ||
        lib_get_state_variable_type_name == NULL || lib_get_state_variable_type == NULL ||
        lib_get_state_variable_type_value_count == NULL || lib_get_state_variable_type_value == NULL ||
        lib_project_state_to_int_array == NULL || lib_project_int_array_to_state == NULL ||
        lib_get_transition_proj == NULL || lib_get_transition_succ == NULL ||
        lib_get_transition_count == NULL || lib_new_state == NULL) {
        FatalCall (1, error, "Library \"%s\" doesn't export the required functions", filename);
    }

    // check system_with_property
    if (lib_system_with_property())
    {
        Fatal(1,error,"DVE models with properties are currently not supported!");
    }

    // get ltstypes
    int state_length = lib_get_state_variable_count();
    ltstype=lts_type_create();

    // adding types
    int ntypes = lib_get_state_variable_type_count();
    for(int i=0; i < ntypes; i++) {
        const char* type_name = lib_get_state_variable_type_name(i);
        if (lts_type_add_type(ltstype,type_name,NULL) != i) {
            Fatal(1,error,"wrong type number");
        }
    }

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for(int i=0; i < state_length; ++i)
    {
        const char* name = lib_get_state_variable_name(i);
        const int   type = lib_get_state_variable_type(i);
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }

    GBsetLTStype(model, ltstype);

    // setting values for types
    // chunk_str doesn't work
    for(int i=0; i < ntypes; i++) {
        int type_value_count = lib_get_state_variable_type_value_count(i);
        if (type_value_count > 0) {
            for(int j=0; j < type_value_count; ++j) {
                const char* type_value = lib_get_state_variable_type_value(i, j);
                GBchunkPut(model, i, (chunk){strlen(type_value),(char*)type_value});
            }
        }
    }
    lts_type_validate(ltstype);

    int ngroups = lib_get_transition_count();
	dm_create(&dm_info, ngroups, state_length);
    for(int i=0; i < dm_nrows(&dm_info); i++) {
        int* proj = lib_get_transition_proj(i);
		for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(&dm_info, i, j);
        }
    }
    GBsetDMInfo(model, &dm_info);

    // there are no state labels
    dm_create(&sl_info, 0, state_length);
    GBsetStateLabelInfo(model, &sl_info);

    // get initial state
	int state[state_length];
    divine_get_initial_state(state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, divine_get_transitions_all);
    GBsetNextStateLong (model, divine_get_transitions_long);
}

} // extern "C"
