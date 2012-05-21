#include <config.h>
#include <iostream>
#include <fstream>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
// DiVinE
#include "common/array.hh"
#include "system/state.hh"

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
int         (*lib_get_state_variable_type)(int var);
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
#include "unix.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

static void
dve_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption* opt,
         const char* arg, void* data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch (reason) {
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterPreLoader("dve", DVEcompileGreyboxModel);
        GBregisterPreLoader("dveC", DVEloadDynamicLib);
        GBregisterLoader("dve", DVEloadGreyboxModel);
        GBregisterLoader("dveC", DVEloadGreyboxModel);
        Warning(info,"Precompiled divine module initialized");
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal(1,error,"unexpected call to dve_popt");
}

struct poptOption dve_options[] = {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&dve_popt, 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
    divine::succ_container_t cb_cont;
} *gb_context_t;

static void
divine_get_initial_state (int* state)
{
    divine::state_t s = lib_get_initial_state();
    lib_project_state_to_int_array(s, state);
    divine::delete_state(s);
}

static void* dlHandle = NULL;
static char templatename[PATH_MAX];
transition_info_t transition_info = GB_TI(NULL, -1);

static int
succ_callback (TransitionCB cb, void* context, divine::succ_container_t& cb_cont)
{
    int result = cb_cont.size();
    for(size_t i=0; i < (size_t)result;++i)
    {
        int dst[lib_get_state_variable_count()];
        divine::state_t s = cb_cont.pop_back();
        lib_project_state_to_int_array(s, dst);
        divine::delete_state(s);
        cb(context, &transition_info, dst);
    }
    return result;
}

static int
divine_get_transitions_all(model_t self, int*src, TransitionCB cb, void*context)
{
    gb_context_t dve_ctx = (gb_context_t)GBgetContext(self);
    divine::state_t s = lib_new_state();
    lib_project_int_array_to_state(src, s);
    lib_get_succ(s, dve_ctx->cb_cont);
    divine::delete_state(s);
    return succ_callback(cb, context, dve_ctx->cb_cont);
}

static int
divine_get_transitions_long(model_t self, int group, int*src, TransitionCB cb, void*context)
{
    gb_context_t dve_ctx = (gb_context_t)GBgetContext(self);
    divine::state_t s = lib_new_state();
    lib_project_int_array_to_state(src, s);
    lib_get_transition_succ(group, s, dve_ctx->cb_cont);
    divine::delete_state(s);
    return succ_callback(cb, context, dve_ctx->cb_cont);
}

void
DVEexit()
{
    // close dveC library
    if (dlHandle == NULL)
        return;
    dlclose(dlHandle);

    if (strlen (templatename) == 0)
        return;

    char rmcmd[4096];
    if (snprintf(rmcmd, sizeof rmcmd, "rm -rf %s", templatename) < (ssize_t)sizeof rmcmd) {
        // remove!
        system(rmcmd);
    }
}

#define SYSFAIL(cond,...)                                               \
    do { if (cond) FatalCall(__VA_ARGS__) Fatal(__VA_ARGS__); } while (0)
void
DVEcompileGreyboxModel(model_t model, const char *filename)
{
    struct stat st;
    int ret;

    // check file exists
    if ((ret = stat (filename, &st)) != 0)
        FatalCall (1, error, "%s", filename);

    char abs_filename[PATH_MAX];
    char *ret_filename = realpath (filename, abs_filename);
    if (ret_filename == NULL)
        FatalCall (1, error, "Cannot determine absolute path of %s", filename);
    const char *basename = strrchr (abs_filename, '/');
    if (basename == NULL)
        Fatal (1, error, "Could not extract basename of file: %s", abs_filename);
    ++basename;                         // skip '/'

    // get temporary directory
    char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL)
        tmpdir = (char*)"/tmp";

    if ((ret = stat (tmpdir, &st)) != 0)
        FatalCall(1, error, "Cannot access `%s' for temporary compilation",
                  tmpdir);

    int len = snprintf (templatename, sizeof templatename, "%s/ltsmin-XXXXXX", tmpdir);
    if (len >= (ssize_t)sizeof templatename)
        Fatal (1, error, "Path too long: %s", tmpdir);

    atexit (DVEexit);                   // cleanup
    if ((tmpdir = mkdtemp(templatename)) == NULL)
        FatalCall(1, error, "Cannot create temporary directory for compilation: %s", tmpdir);

    // copy
    char command[PATH_MAX];
    // XXX shell escape filename
    if (snprintf (command, sizeof command, "cp '%s' '%s'", abs_filename, tmpdir) >= (ssize_t)sizeof command)
        Fatal (1, error, "Paths to long: cannot copy `%s' to `%s'", abs_filename, tmpdir);

    if ((ret = system (command)) != 0)
        SYSFAIL(ret < 0, 1, error, "Command failed with exit code %d: %s", ret, command);

    // compile dve model
    if (snprintf(command, sizeof command, "divine.precompile '%s/%s'", tmpdir, basename) >= (ssize_t)sizeof command)
        Fatal (1, error, "Cannot copy `%s' to `%s', paths too long", abs_filename, tmpdir);

    if ((ret = system(command)) != 0)
        SYSFAIL(ret < 0, 1, error, "Command failed with exit code %d: %s", ret, command);

    // check existence of dveC file
    char dveC[PATH_MAX];
    if (snprintf (dveC, sizeof dveC, "%s/%sC", tmpdir, basename) >= (ssize_t)sizeof dveC)
        Fatal (1, error, "Path too long: %s", tmpdir);

    if ((ret = stat (dveC, &st)) != 0)
        SYSFAIL(ret < 0, 1, error, "File not found: %s", dveC);

    DVEloadDynamicLib(model, dveC);
}
#undef SYSFAIL

void
DVEloadDynamicLib (model_t model, const char *filename)
{
    (void)model;
    // Open dveC file
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
        Fatal (1, error, "Library \"%s\" doesn't export the required functions", filename);
    }

    // added interface functions
    lib_system_with_property = (bool (*)())
        RTdlsym (filename, dlHandle, "lib_system_with_property");
    lib_get_state_variable_count = (size_t (*)())
        RTdlsym (filename, dlHandle, "lib_get_state_variable_count");
    lib_get_state_variable_name = (const char* (*)(int var))
        RTdlsym (filename, dlHandle, "lib_get_state_variable_name" );
    lib_get_state_variable_type_count = (size_t (*)())
        RTdlsym (filename, dlHandle, "lib_get_state_variable_type_count");
    lib_get_state_variable_type_name = (const char* (*)(int type))
        RTdlsym (filename, dlHandle, "lib_get_state_variable_type_name");
    lib_get_state_variable_type  = (int (*)(int var))
        RTdlsym (filename, dlHandle, "lib_get_state_variable_type");
    lib_get_state_variable_type_value_count = (size_t (*)(int))
        RTdlsym (filename, dlHandle, "lib_get_state_variable_type_value_count");
    lib_get_state_variable_type_value = (const char* (*)(int type, int value))
        RTdlsym (filename, dlHandle, "lib_get_state_variable_type_value");
    lib_project_state_to_int_array = (void (*)(state_t state, int* proj))
        RTdlsym (filename, dlHandle, "lib_project_state_to_int_array");
    lib_project_int_array_to_state = (void (*)(int* proj, state_t state))
        RTdlsym (filename, dlHandle, "lib_project_int_array_to_state");
    lib_get_transition_proj = (int* (*)(int trans))
        RTdlsym (filename, dlHandle, "lib_get_transition_proj");
    lib_get_transition_succ = (int (*)(size_int_t transition, state_t state, succ_container_t & succ_container))
        RTdlsym (filename, dlHandle, "lib_get_transition_succ");
    lib_get_transition_count = (int (*)())
        RTdlsym (filename, dlHandle, "lib_get_transition_count");
    lib_new_state = (divine::state_t (*)())
        RTdlsym (filename, dlHandle, "lib_new_state");

    // check system_with_property
    if (lib_system_with_property()) {
        Fatal(1,error,"DVE models with properties are currently not supported!");
    }
}

void
DVEloadGreyboxModel (model_t model, const char *filename)
{

    lts_type_t ltstype;
    matrix_t *dm_info = (matrix_t*) RTmalloc(sizeof(matrix_t));
    matrix_t *sl_info = (matrix_t*) RTmalloc(sizeof(matrix_t));
    
    //assume sequential use:
    if (NULL == dlHandle) {
        char *extension = strrchr ((char*)filename, '.');
        assert (extension != NULL);
        ++extension;
        if (0==strcmp (extension, "dveC")) {
            DVEloadDynamicLib(model, filename);
        } else {
            DVEcompileGreyboxModel(model, filename);
        }
    }

    gb_context_t ctx=new grey_box_context();
    GBsetContext(model,ctx);

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
                char *type_value = (char *)lib_get_state_variable_type_value(i, j);
                GBchunkPut(model, i, chunk_str(type_value));
            }
        }
    }
    lts_type_validate(ltstype);

    int ngroups = lib_get_transition_count();
	dm_create(dm_info, ngroups, state_length);
    for(int i=0; i < dm_nrows(dm_info); i++) {
        int* proj = lib_get_transition_proj(i);
		for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
        }
    }
    GBsetDMInfo(model, dm_info);

    // there are no state labels
    dm_create(sl_info, 0, state_length);
    GBsetStateLabelInfo(model, sl_info);

    // get initial state
	int state[state_length];
    divine_get_initial_state(state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, divine_get_transitions_all);
    GBsetNextStateLong (model, divine_get_transitions_long);
}

} // extern "C"
