#include <config.h>
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dve2-greybox.h>
#include <tls.h>
#include <chunk_support.h>
#include <dm/dm.h>
#include <runtime.h>
#include <unix.h>

// dve2 ltsmin interface functions
void        (*get_initial_state)(char *to);
int         (*have_property)(); // bool not defined, todo
int         (*get_successor)( void* m, int t, int *in, TransitionCB, void *arg );
int         (*get_successors)( void* m, int *in, TransitionCB, void *arg );

int         (*get_state_variable_count)();
const char* (*get_state_variable_name)(int var);
int         (*get_state_variable_type)(int var);
int         (*get_state_variable_type_count)();
const char* (*get_state_variable_type_name)(int type);
int         (*get_state_variable_type_value_count)(int type);
const char* (*get_state_variable_type_value)(int type, int value);
int         (*get_transition_count)();
const int*  (*get_transition_read_dependencies)(int t);
const int*  (*get_transition_write_dependencies)(int t);

static void
dve_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption * opt,
         const char * arg, void * data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch(reason){
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterPreLoader("dve", DVE2compileGreyboxModel);
        GBregisterPreLoader("so",DVE2loadDynamicLib);
        GBregisterPreLoader("dve2C",DVE2loadDynamicLib);
        GBregisterLoader("dve", DVE2loadGreyboxModel);
        GBregisterLoader("so",DVE2loadGreyboxModel);
        GBregisterLoader("dve2C",DVE2loadGreyboxModel);
        Warning(info,"Precompiled divine module initialized");
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Fatal(1,error,"unexpected call to dve_popt");
}

struct poptOption dve2_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&dve_popt, 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
    int todo;
} *gb_context_t;

static void* dlHandle = NULL;
static char templatename[PATH_MAX];

void
DVEexit()
{
    // close dveC library
    if (dlHandle == NULL)
        return;
    dlclose(dlHandle);

    if (strlen (templatename) == 0)
        return;

    char rmcmd[PATH_MAX];
    if (snprintf(rmcmd, sizeof rmcmd, "rm -rf %s", templatename) < (ssize_t)sizeof rmcmd) {
        // remove!
        system(rmcmd);
    }
}

#define SYSFAIL(cond,...)                                               \
    do { if (cond) FatalCall(__VA_ARGS__) Fatal(__VA_ARGS__); } while (0)
void
DVE2compileGreyboxModel(model_t model, const char *filename)
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
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL)
        tmpdir = "/tmp";

    if ((ret = stat (tmpdir, &st)) != 0)
        FatalCall(1, error, "Cannot access `%s' for temporary compilation",
                  tmpdir);
    // XXX if ( cas(&initialized, 0, 1) ) {
    if (snprintf (templatename, sizeof templatename, "%s/ltsmin-XXXXXX", tmpdir) >= (ssize_t)sizeof templatename)
        Fatal (1, error, "Path too long: %s", tmpdir);

    if ((tmpdir = mkdtemp(templatename)) == NULL)
        FatalCall(1, error, "Cannot create temporary directory for compilation: %s", tmpdir);

    // change to temp dir
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) != NULL)
        cwd[0] = 0;
    int cwdfd = open (".", O_RDONLY);
    if (cwdfd < 0)
        FatalCall(1, error, "Cannot open current directory");
    if ((ret = chdir (tmpdir)) != 0)
        FatalCall(1, error, "Cannot change directory: %s", tmpdir);
    
    // compile dve model
    char command[4096];
    if (snprintf(command, sizeof command, "divine compile --ltsmin '%s'", abs_filename) >= (ssize_t)sizeof command)
        Fatal (1, error, "Cannot compile `%s', paths too long", abs_filename);

    if ((ret = system(command)) != 0)
        SYSFAIL(ret < 0, 1, error, "Command failed with exit code %d: %s", ret, command);

    if (fchdir (cwdfd) != 0 && (cwd[0] == 0 || chdir(cwd) != 0))
        FatalCall(1, error, "Cannot change directory back to current: %s", cwd);
    
    // check existence of dve2C file
    char dve_so_fname[PATH_MAX];
    if (snprintf (dve_so_fname, sizeof dve_so_fname, "%s/%s2C", tmpdir, basename) >= (ssize_t)sizeof dve_so_fname)
        Fatal (1, error, "Path too long: %s", tmpdir);

    if ((ret = stat (dve_so_fname, &st)) != 0)
        SYSFAIL(ret < 0, 1, error, "File not found: %s", dve_so_fname);

    DVE2loadDynamicLib(model, dve_so_fname);
}
#undef SYSFAIL

void
DVE2loadDynamicLib(model_t model, const char *filename)
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
    atexit (DVEexit);                   // cleanup

    // load dynamic library functionality
    get_initial_state = (void(*)(char*))
    RTdlsym( filename, dlHandle, "get_initial_state" );
    have_property = (int(*)()) // todo: bool doesn't exist
    RTdlsym( filename, dlHandle, "have_property" );
    get_successor = (int(*)(void*, int, int*, TransitionCB, void*))
    RTdlsym( filename, dlHandle, "get_successor" );
    get_successors = (int(*)(void*, int*, TransitionCB, void*))
    RTdlsym( filename, dlHandle, "get_successors" );
    get_state_variable_count = (int(*)())
    RTdlsym( filename, dlHandle, "get_state_variable_count" );
    get_state_variable_name = (const char*(*)(int))
    RTdlsym( filename, dlHandle, "get_state_variable_name" );
    get_state_variable_type = (int (*)(int))
    RTdlsym( filename, dlHandle, "get_state_variable_type" );
    get_state_variable_type_count = (int(*)())
    RTdlsym( filename, dlHandle, "get_state_variable_type_count" );
    get_state_variable_type_name = (const char*(*)(int))
    RTdlsym( filename, dlHandle, "get_state_variable_type_name" );
    get_state_variable_type_value_count = (int(*)(int))
    RTdlsym( filename, dlHandle, "get_state_variable_type_value_count" );
    get_state_variable_type_value = (const char*(*)(int,int))
    RTdlsym( filename, dlHandle, "get_state_variable_type_value" );
    get_transition_count = (int(*)())
    RTdlsym( filename, dlHandle, "get_transition_count" );
    get_transition_read_dependencies = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_transition_read_dependencies" );
    get_transition_write_dependencies = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_transition_write_dependencies" );

    // check system_with_property
    if (have_property()) {
        Fatal(1,error,"DVE models with properties are currently not supported!");
    }
}

void
DVE2loadGreyboxModel(model_t model, const char *filename)
{
    lts_type_t ltstype;
    matrix_t *dm_info = RTmalloc(sizeof(matrix_t));
    matrix_t *sl_info = RTmalloc(sizeof(matrix_t));
   
    //assume sequential use:
    if (NULL == dlHandle) {
        char *extension = strrchr (filename, '.');
        assert (extension != NULL);
        ++extension;
        if (0==strcmp (extension, "dve2C") || 0==strcmp (extension, "so")) {
            DVE2loadDynamicLib(model, filename);
        } else {
            DVE2compileGreyboxModel(model, filename);
        }
    }

    gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model,ctx);

    // get ltstypes
    int state_length = get_state_variable_count();
    ltstype=lts_type_create();

    // adding types
    int ntypes = get_state_variable_type_count();
    for(int i=0; i < ntypes; i++) {
        const char* type_name = get_state_variable_type_name(i);
        if (lts_type_add_type(ltstype,type_name,NULL) != i) {
            Fatal(1,error,"wrong type number");
        }
    }

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for(int i=0; i < state_length; ++i)
    {
        const char* name = get_state_variable_name(i);
        const int   type = get_state_variable_type(i);
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }

    GBsetLTStype(model, ltstype);

    // setting values for types
    for(int i=0; i < ntypes; i++) {
        int type_value_count = get_state_variable_type_value_count(i);
        if (type_value_count > 0) {
            for(int j=0; j < type_value_count; ++j) {
                const char* type_value = get_state_variable_type_value(i, j);
                GBchunkPut(model, i, chunk_str((char*)type_value));
            }
        }
    }
    lts_type_validate(ltstype);

    int ngroups = get_transition_count();
	dm_create(dm_info, ngroups, state_length);
    for(int i=0; i < dm_nrows(dm_info); i++) {
        int* proj = (int*)get_transition_read_dependencies(i);
		for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
        }
        proj = (int*)get_transition_write_dependencies(i);
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
    get_initial_state((char*)state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, (next_method_black_t) get_successors);
    GBsetNextStateLong (model, (next_method_grey_t)  get_successor);
}
