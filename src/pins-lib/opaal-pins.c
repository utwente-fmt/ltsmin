#include <hre/config.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dm/dm.h>
#include <hre/unix.h>
#include <hre/user.h>
#include <pins-lib/opaal-pins.h>
#include <util-lib/chunk_support.h>

// opaal ltsmin interface functions
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
covered_by_grey_t   covered_by;
covered_by_grey_t   covered_by_short;

enum {
    SL_IDX_BUCHI_ACCEPT = 0,
};

static void
opaal_popt(poptContext con,
         enum poptCallbackReason reason,
         const struct poptOption * opt,
         const char * arg, void * data)
{
    (void)con;(void)opt;(void)arg;(void)data;
    switch(reason){
    case POPT_CALLBACK_REASON_PRE:
        break;
    case POPT_CALLBACK_REASON_POST:
        GBregisterPreLoader("so",opaalLoadDynamicLib);
        GBregisterPreLoader("xml", opaalCompileGreyboxModel);
        GBregisterLoader("so",opaalLoadGreyboxModel);
        GBregisterLoader("xml", opaalLoadGreyboxModel);
        Warning(info,"Precompiled opaal module initialized");
        return;
    case POPT_CALLBACK_REASON_OPTION:
        break;
    }
    Abort("unexpected call to opaal_popt");
}

struct poptOption opaal_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&opaal_popt, 0 , NULL , NULL },
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
    HREassert (labels != NULL, "No labels");
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
    get_guard_all(model, state, labels);
    labels[GBgetAcceptingStateLabelIndex(model)] = buchi_is_accepting(model, state);
}

static void
sl_group (model_t model, sl_group_enum_t group, int *state, int *labels)
{
    switch (group) {
        case GB_SL_ALL:
            GBgetStateLabelsAll(model, state, labels);
            return;
        case GB_SL_GUARDS:
            get_guard_all(model, state, labels);
            return;
        default:
            return;
    }
}

void
opaalExit()
{
    // close so library
    if (dlHandle == NULL)
        return;
    dlclose(dlHandle);

    if (strlen (templatename) == 0)
        return;

    char rmcmd[PATH_MAX];
    if (snprintf(rmcmd, sizeof rmcmd, "rm -rf %s", templatename) < (ssize_t)sizeof rmcmd) {
        // remove temporary files
        int status = system(rmcmd);
        if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            Warning(info, "Could not remove %s.", templatename);
    }
}

#define SYSFAIL(cond,...)                                               \
    do { if (cond) Abort(__VA_ARGS__); } while (0)
void
opaalCompileGreyboxModel(model_t model, const char *filename)
{
    struct stat st;
    int ret;

    // check file exists
    if ((ret = stat (filename, &st)) != 0)
        Abort("File not found: %s", filename);

    // compile opaal model
    char command[4096];
    if (snprintf(command, sizeof command, "opaal_ltsmin --only-compile '%s'", filename) >= (ssize_t)sizeof command)
        Abort("Cannot compile `%s', paths too long", filename);

    if ((ret = system(command)) != 0)
        SYSFAIL(ret < 0, "Command failed with exit code %d: %s", ret, command);

    // check existence of so file
    char *opaal_so_fname = "/tmp/gensuccgen.so";

    if ((ret = stat (opaal_so_fname, &st)) != 0)
        SYSFAIL(ret < 0, "File not found: %s", opaal_so_fname);

    opaalLoadDynamicLib(model, opaal_so_fname);
}
#undef SYSFAIL

void
opaalLoadDynamicLib(model_t model, const char *filename)
{
    (void)model;
    // Open so file
    char abs_filename[PATH_MAX];
	char *ret_filename = realpath(filename, abs_filename);
    if (ret_filename) {
        dlHandle = dlopen(abs_filename, RTLD_LAZY);
        if (dlHandle == NULL)
        {
            Abort("%s, Library \"%s\" is not reachable", dlerror(), filename);
            return;
        }
    } else {
        Abort("%s, Library \"%s\" is not found", dlerror(), filename);
    }
    atexit (opaalExit);                   // cleanup

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
    RTdlsym( filename, dlHandle, "get_guard_count" );
    get_guard_matrix = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_guard_matrix" );
    get_guards = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_guards" );
    get_all_guards = (const int**(*)())
    RTdlsym( filename, dlHandle, "get_all_guards" );
    get_guard = (int(*)(void*,int,int*))
    RTdlsym( filename, dlHandle, "get_guard" );
    get_guard_all = (void(*)(void*,int*,int*))
    RTdlsym( filename, dlHandle, "get_guard_all" );
    get_guard_may_be_coenabled_matrix = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_guard_may_be_coenabled_matrix" );
    get_guard_nes_matrix = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_guard_nes_matrix" );
    get_guard_nds_matrix = (const int*(*)(int))
    RTdlsym( filename, dlHandle, "get_guard_nds_matrix" );

    // optionally load the covered_by method for partly symbolic states
    covered_by = (covered_by_grey_t)
        RTtrydlsym(dlHandle, "covered_by");
    covered_by_short = (covered_by_grey_t)
        RTtrydlsym(dlHandle, "covered_by_short");

    // check system_with_property
    if (have_property()) {
        buchi_is_accepting = (int(*)(void*,int*))
        RTdlsym( filename, dlHandle, "buchi_is_accepting" );
    }
}

void
opaalLoadGreyboxModel(model_t model, const char *filename)
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
        HREassert (extension != NULL, "No filename extension in %s", filename);
        ++extension;
        if (0==strcmp (extension, "so")) {
            opaalLoadDynamicLib(model, filename);
        } else {
            opaalCompileGreyboxModel(model, filename);
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
            Abort("wrong type number");
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
                  nguards +
                  (have_property() ? 1 : 0);

    // assumption on state labels:
    // state labels (idx): 0 - nguards-1 = guard state labels
    // state label  (idx): nguards = property state label
    lts_type_set_state_label_count (ltstype, sl_size);
    char buf[256];
    for(int i=0; i < nguards; i++) {
        snprintf(buf, 256, "guard_%d", i);
        lts_type_set_state_label_name (ltstype, i, buf);
        lts_type_set_state_label_typeno (ltstype, i, bool_type);
    }
    if (have_property()) {
        lts_type_set_state_label_name (ltstype, nguards,
                                       "buchi_accept_opaal");
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
        HREassert (idx_false == 0, "idx_false != 0 but %d", idx_false);
        HREassert (idx_true == 1, "idx_true != 1 but %d", idx_true);
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

    // set the group implementation
    sl_group_t* sl_group_all = RTmallocZero(sizeof(sl_group_t) + sl_size * sizeof(int));
    sl_group_all->count = sl_size;
    for(int i=0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    sl_group_t* sl_group_guards = RTmallocZero(sizeof(sl_group_t) + nguards * sizeof(int));
    sl_group_guards->count = nguards;
    for(int i=0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_ALL, sl_group_all);
    GBsetStateLabelGroupInfo(model, GB_SL_GUARDS, sl_group_guards);
    GBsetStateLabelsGroup(model, sl_group);

    GBsetStateLabelInfo(model, sl_info);
    if (sl_long != NULL) GBsetStateLabelLong(model, sl_long);
    if (sl_all  != NULL) GBsetStateLabelsAll(model, sl_all);

    // get initial state
    int state[state_length];
    get_initial_state((char*)state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, (next_method_black_t) get_successors);
    GBsetNextStateLong (model, (next_method_grey_t)  get_successor);
    GBsetIsCoveredBy (model, covered_by);
    GBsetIsCoveredByShort (model, covered_by_short);
}
