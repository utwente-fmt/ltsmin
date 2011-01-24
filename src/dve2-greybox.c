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
int         (*buchi_is_accepting)(void* m, int* in);
int         (*get_successor)( void* m, int t, int *in, TransitionCB, void *arg );
int         (*get_successors)( void* m, int *in, TransitionCB, void *arg );

int         (*get_guard_count)();
const int*  (*get_guard_matrix)(int g);
const int*  (*get_guards)(int t);
const int** (*get_all_guards)();
int         (*get_guard)(void*, int g, int *src);
void        (*get_guard_all)(void*, int *src, int* guards);
const int*  (*get_guard_may_be_coenabled_matrix)(int g);
const int*  (*get_guard_nes_matrix)(int g);
const int*  (*get_guard_nds_matrix)(int g);

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

enum {
    SL_IDX_BUCHI_ACCEPT = 0,
};

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

/* XXX missing sl_short, need way to check acceptance based only on
   Buchi state */

static int
sl_long_p (model_t model, int label, int *state)
{
    if (label == GBgetAcceptingStateLabelIndex(model)) {
        return buchi_is_accepting(model, state);
    } else {
        Abort("unexpected state label requested: %d", label);
    }
}

static void
sl_all_p (model_t model, int *state, int *labels)
{
    assert (labels != NULL);
    labels[GBgetAcceptingStateLabelIndex(model)] = buchi_is_accepting(model, state);
}

static int
sl_long_p_g (model_t model, int label, int *state)
{
    if (label == GBgetAcceptingStateLabelIndex(model)) {
        return buchi_is_accepting(model, state);
    } else {
        return get_guard(model, label, state);
    }
}

static void
sl_all_p_g (model_t model, int *state, int *labels)
{
    assert (labels != NULL);
    get_guard_all(model, state, labels++);
    labels[GBgetAcceptingStateLabelIndex(model)] = buchi_is_accepting(model, state);
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
    have_property = (int(*)())
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

    // optional, guard support (used for por)
    get_guard_count = (int(*)())
    RTopt_dlsym( dlHandle, "get_guard_count" );
    get_guard_matrix = (const int*(*)(int))
    RTopt_dlsym( dlHandle, "get_guard_matrix" );
    get_guards = (const int*(*)(int))
    RTopt_dlsym( dlHandle, "get_guards" );
    get_all_guards = (const int**(*)())
    RTopt_dlsym( dlHandle, "get_all_guards" );
    get_guard = (int(*)(void*,int,int*))
    RTopt_dlsym( dlHandle, "get_guard" );
    get_guard_all = (void(*)(void*,int*,int*))
    RTopt_dlsym( dlHandle, "get_guard_all" );
    get_guard_may_be_coenabled_matrix = (const int*(*)(int))
    RTopt_dlsym( dlHandle, "get_guard_may_be_coenabled_matrix" );
    get_guard_nes_matrix = (const int*(*)(int))
    RTopt_dlsym( dlHandle, "get_guard_nes_matrix" );
    get_guard_nds_matrix = (const int*(*)(int))
    RTopt_dlsym( dlHandle, "get_guard_nds_matrix" );

    // check system_with_property
    if (have_property()) {
        buchi_is_accepting = (int(*)(void*,int*))
        RTdlsym( filename, dlHandle, "buchi_is_accepting" );
    }
}

void
DVE2loadGreyboxModel(model_t model, const char *filename)
{
    lts_type_t ltstype;
    matrix_t *dm_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_write_info = RTmalloc(sizeof(matrix_t));
    matrix_t *sl_info = RTmalloc(sizeof(matrix_t));
    matrix_t *gce_info = RTmalloc(sizeof(matrix_t));  // guard may be co-enabled information
    matrix_t *gnes_info = RTmalloc(sizeof(matrix_t)); // guard necessary enabling set information
    matrix_t *gnds_info = RTmalloc(sizeof(matrix_t)); // guard necessary disabling set informaiton

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

    // check for guards
    int model_has_guards = 0;
    if (get_guard_count != NULL || get_guard_matrix != NULL ||
        get_guards != NULL || get_all_guards != NULL ||
        get_guard != NULL || get_guard_all != NULL ) {
        if (get_guard_count == NULL || get_guard_matrix == NULL ||
            get_guards == NULL || get_all_guards == NULL ||
            get_guard == NULL || get_guard_all == NULL) {
            Warning(info,"DVE guard functionality only partially available in model, ignoring guards!");
        } else {
            model_has_guards = 1;
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
    int bool_is_new, bool_type = lts_type_add_type (ltstype, "bool", &bool_is_new);

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for(int i=0; i < state_length; ++i)
    {
        const char* name = get_state_variable_name(i);
        const int   type = get_state_variable_type(i);
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }
    // compute state label names
    int nguards = get_guard_count(); // TODO: should be in model has guards block..?
    int sl_size = 0 +
                  (model_has_guards ? nguards : 0) +
                  (have_property() ? 1 : 0);

    // assumption on state labels:
    // state labels (idx): 0 - nguards-1 = guard state labels
    // state label  (idx): nguards = property state label
    lts_type_set_state_label_count (ltstype, sl_size);
    if (model_has_guards) {
        char buf[256];
        for(int i=0; i < nguards; i++) {
            snprintf(buf, 256, "guard_%d", i);
            lts_type_set_state_label_name (ltstype, i, buf);
            lts_type_set_state_label_typeno (ltstype, i, bool_type);
        }
    }
    if (have_property()) {
        lts_type_set_state_label_name (ltstype, nguards,
                                       "buchi_accept_dve2");
        lts_type_set_state_label_typeno (ltstype, nguards, bool_type);
        GBsetAcceptingStateLabelIndex (model,nguards);
    }

    GBsetLTStype(model, ltstype);

    // setting values for types
    for(int i=0; i < ntypes; i++) {
        int type_value_count = get_state_variable_type_value_count(i);
        for(int j=0; j < type_value_count; ++j) {
            const char* type_value = get_state_variable_type_value(i, j);
            GBchunkPut(model, i, chunk_str((char*)type_value));
        }
    }

    if (bool_is_new) {
        int idx_false = GBchunkPut(model, bool_type, chunk_str("false"));
        int idx_true  = GBchunkPut(model, bool_type, chunk_str("true"));
        assert (idx_false == 0);
        assert (idx_true == 1);
        (void)idx_false; (void)idx_true;
    }

    lts_type_validate(ltstype);

    int ngroups = get_transition_count();
	dm_create(dm_info, ngroups, state_length);
	dm_create(dm_read_info, ngroups, state_length);
	dm_create(dm_write_info, ngroups, state_length);
    for(int i=0; i < dm_nrows(dm_info); i++) {
        int* proj = (int*)get_transition_read_dependencies(i);
		for(int j=0; j<state_length; j++) {
            if (proj[j]) {
                dm_set(dm_info, i, j);
                dm_set(dm_read_info, i, j);
            }
        }
        proj = (int*)get_transition_write_dependencies(i);
		for(int j=0; j<state_length; j++) {
            if (proj[j]) {
                dm_set(dm_info, i, j);
                dm_set(dm_write_info, i, j);
            }
        }
    }
    GBsetDMInfo(model, dm_info);
    GBsetDMInfoRead(model, dm_read_info);
    GBsetDMInfoWrite(model, dm_write_info);

    // set state label matrix (accepting label and guards)
    get_label_method_t sl_long = NULL;
    get_label_all_method_t sl_all = NULL;
    dm_create(sl_info, sl_size, state_length);

    // if the model exports a property, reserve first for accepting label
    if (have_property()) {
        for (int i=0; i<state_length; ++i) {
            if (strcmp ("LTL_property", lts_type_get_state_name(ltstype, i)) == 0) {
                dm_set(sl_info, GBgetAcceptingStateLabelIndex(model), i);
            }
        }
        sl_long = sl_long_p;
        sl_all = sl_all_p;
    }

    // if the model has guards, add guards as state labels
    if (model_has_guards) {
        if (have_property()) {
            // filter the property
            sl_long = sl_long_p_g;
            sl_all = sl_all_p_g;
        } else {
            // pass request directly to dynamic lib
            sl_long = (get_label_method_t)     get_guard;
            sl_all =  (get_label_all_method_t) get_guard_all;
        }

        // set the guards per transition group
        GBsetGuardsInfo(model, (guard_t**) get_all_guards());

        // initialize state label matrix
        // assumption, guards come first (0-nguards)
        for(int i=0; i < nguards; i++) {
            int* guards = (int*)get_guard_matrix(i);
            for(int j=0; j<state_length; j++) {
                if (guards[j]) dm_set(sl_info, i, j);
            }
        }

        // set guard may be co-enabled relation
        if (get_guard_may_be_coenabled_matrix) {
            dm_create(gce_info, nguards, nguards);
            for(int i=0; i < nguards; i++) {
                int* guardce = (int*)get_guard_may_be_coenabled_matrix(i);
                for(int j=0; j<nguards; j++) {
                    if (guardce[j]) dm_set(gce_info, i, j);
                }
            }
            GBsetGuardCoEnabledInfo(model, gce_info);
        }

        // set guard necessary enabling set info
        if (get_guard_nes_matrix) {
            dm_create(gnes_info, nguards, ngroups);
            for(int i=0; i < nguards; i++) {
                int* guardnes = (int*)get_guard_nes_matrix(i);
                for(int j=0; j<ngroups; j++) {
                    if (guardnes[j]) dm_set(gnes_info, i, j);
                }
            }
            GBsetGuardNESInfo(model, gnes_info);
        }

        // set guard necessary disabling set info
        if (get_guard_nds_matrix) {
            dm_create(gnds_info, nguards, ngroups);
            for(int i=0; i < nguards; i++) {
                int* guardnds = (int*)get_guard_nds_matrix(i);
                for(int j=0; j<ngroups; j++) {
                    if (guardnds[j]) dm_set(gnds_info, i, j);
                }
            }
            GBsetGuardNDSInfo(model, gnds_info);
        }
    }

    GBsetStateLabelInfo(model, sl_info);
    if (sl_long != NULL) GBsetStateLabelLong(model, sl_long);
    if (sl_all  != NULL) GBsetStateLabelsAll(model, sl_all);

    // get initial state
    int state[state_length];
    get_initial_state((char*)state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, (next_method_black_t) get_successors);
    GBsetNextStateLong (model, (next_method_grey_t)  get_successor);
}
