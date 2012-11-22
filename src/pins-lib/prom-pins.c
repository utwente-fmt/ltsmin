#include <hre/config.h>

#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dm/dm.h>
#include <hre/runtime.h>
#include <hre/unix.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/prom-pins.h>
#include <util-lib/chunk_support.h>
#include <util-lib/util.h>

#define GUARD_CHECK_MISSING(g) { \
	if(g) { Warning(info,"missing guard function: get_guard_count"); }

// Remove when LTSmin has RT_optdlsym() again
#define RT_optdlsym(f,h,s) dlsym(h,s)

// spinja ltsmin interface functions
// if(!get_guard_count) Warning(info,"missing guard function: get_guard_count");
void        (*spinja_get_initial_state)(int *to);
next_method_grey_t spinja_get_successor;
next_method_black_t spinja_get_successor_all;

int         (*prom_get_state_size)();
int         (*prom_get_transition_groups)();
const int*  (*prom_get_transition_read_dependencies)(int t);
const int*  (*prom_get_transition_write_dependencies)(int t);
const char* (*prom_get_state_variable_name)(int var);
int         (*prom_get_state_variable_type)(int var);
const char* (*prom_get_type_name)(int type);
int         (*prom_get_type_count)();
const char* (*prom_get_type_value_name)(int type, int value);
int         (*prom_get_type_value_count)(int type);
const char* (*prom_get_edge_name)(int type);
int         (*prom_get_edge_count)();
const char* (*prom_get_group_name)(int type);

int         (*prom_buchi_is_accepting)(void* model, int* state);

int         (*prom_get_guard_count)();
const int*  (*prom_get_guard_matrix)(int g);
const int*  (*prom_get_guards)(int t);
const int** (*prom_get_all_guards)();
int         (*prom_get_guard)(void*, int g, int *src);
void        (*prom_get_guard_all)(void*, int *src, int* guards);
const int*  (*prom_get_guard_may_be_coenabled_matrix)(int g);
const int*  (*prom_get_guard_nes_matrix)(int g);
const int*  (*prom_get_guard_nds_matrix)(int g);

static void
prom_popt (poptContext con,
             enum poptCallbackReason reason,
             const struct poptOption *opt,
             const char *arg, void *data)
{
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
        GBregisterPreLoader("pr", PromCompileGreyboxModel);
        GBregisterPreLoader("promela", PromCompileGreyboxModel);
        GBregisterPreLoader("prom", PromCompileGreyboxModel);
        GBregisterPreLoader("prm", PromCompileGreyboxModel);
        GBregisterPreLoader("pml", PromCompileGreyboxModel);
		GBregisterPreLoader("spinja", PromLoadDynamicLib);
        GBregisterLoader("pr", PromLoadGreyboxModel);
        GBregisterLoader("promela", PromLoadGreyboxModel);
        GBregisterLoader("prom", PromLoadGreyboxModel);
        GBregisterLoader("pml", PromLoadGreyboxModel);
        GBregisterLoader("prm", PromLoadGreyboxModel);
        GBregisterLoader("spinja", PromLoadGreyboxModel);
		Warning(info,"Precompiled spinja module initialized");
		return;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Abort("unexpected call to spinja_popt");
}

struct poptOption prom_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , (void*)&prom_popt, 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
    int todo;
} *gb_context_t;

static void* dlHandle = NULL;

static int sl_long_p (model_t model, int label, int *state) {
	if (label == GBgetAcceptingStateLabelIndex(model)) {
		return prom_buchi_is_accepting(model, state);
	} else {
		Abort("unexpected state label requested: %d", label);
	}
}

static void sl_all_p (model_t model, int *state, int *labels) {
	labels[0] = prom_buchi_is_accepting(model, state);
}

static int
sl_long_p_g (model_t model, int label, int *state)
{
    if (label == 0) {
        return prom_buchi_is_accepting(model, state);
    } else {
        return prom_get_guard(model, label - 1, state);
    }
}

static void
sl_all_p_g (model_t model, int *state, int *labels)
{
    labels[0] = prom_buchi_is_accepting(model, state);
    prom_get_guard_all(model, state, labels + 1);
}

static void
sl_group (model_t model, sl_group_enum_t group, int *state, int *labels)
{
    switch (group) {
        case GB_SL_ALL:
            GBgetStateLabelsAll(model, state, labels);
            return;
        case GB_SL_GUARDS:
            prom_get_guard_all(model, state, labels);
            return;
        default:
            return;
    }
}

void
PromCompileGreyboxModel(model_t model, const char *filename)
{
    struct stat st;
    int ret;

    // check file exists
    if ((ret = stat (filename, &st)) != 0)
        Abort("File not found: %s", filename);

    // compile opaal model
    char command[4096];
    if (snprintf(command, sizeof command, "spinjal '%s'", filename) >= (ssize_t)sizeof command)
        Abort("Cannot compile `%s', paths too long", filename);

    if ((ret = system(command)) != 0)
        HREassert(ret >= 0, "Command failed with exit code %d: %s", ret, command);

    char *basename = gnu_basename ((char *)filename);
    // check existence of bin file
    char *bin_fname = RTmalloc(strlen(basename) + strlen(".spinja") + 1);
    strncpy(bin_fname, basename, strlen(basename));
    char *ext = bin_fname + strlen(basename);
    strncpy(ext, ".spinja", strlen(".spinja"));

    if ((ret = stat (bin_fname, &st)) != 0)
        HREassert(ret >= 0, "File not found: %s", bin_fname);

    PromLoadDynamicLib(model, bin_fname);
    RTfree (bin_fname);
}

void
PromLoadDynamicLib(model_t model, const char *filename)
{
    // Open spinja file
    char abs_filename[PATH_MAX];
    char *ret_filename = realpath(filename, abs_filename);
    if (ret_filename != NULL) {
        dlHandle = dlopen (abs_filename, RTLD_LAZY);
        if (dlHandle == NULL) {
            Abort("%s, Library \"%s\" is not reachable", dlerror(), filename);
            return;
        }
    } else {
        Abort("%s, Library \"%s\" is not found", dlerror(), filename);
    }

    // load dynamic library functionality
    spinja_get_initial_state = (void(*)(int*))
        RTdlsym( filename, dlHandle, "spinja_get_initial_state" );
    spinja_get_successor = (next_method_grey_t)
        RTdlsym( filename, dlHandle, "spinja_get_successor" );
    spinja_get_successor_all = (next_method_black_t)
        RTdlsym( filename, dlHandle, "spinja_get_successor_all" );

    prom_get_state_size = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_state_size" );

    prom_get_transition_groups = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_transition_groups" );

    prom_get_transition_read_dependencies = (const int*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_transition_read_dependencies" );
    prom_get_transition_write_dependencies = (const int*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_transition_write_dependencies" );

    prom_get_state_variable_name = (const char*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_state_variable_name" );
    prom_get_state_variable_type = (int (*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_state_variable_type" );
    prom_get_type_name = (const char*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_type_name" );
    prom_get_type_count = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_type_count" );
    prom_get_type_value_name = (const char*(*)(int,int))
        RTdlsym( filename, dlHandle, "spinja_get_type_value_name" );
    prom_get_type_value_count = (int(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_type_value_count" );
    prom_buchi_is_accepting = (int(*)(void*arg,int*state))
        RT_optdlsym( filename, dlHandle, "spinja_buchi_is_accepting" );

    prom_get_edge_name = (const char*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_edge_name" );
    prom_get_edge_count = (int(*)())
        RT_optdlsym( filename, dlHandle, "spinja_get_edge_count" );
    prom_get_group_name = (const char*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_group_name" );

    // optional, guard support (used for por)
    prom_get_guard_count = (int(*)())
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_count" );
    prom_get_guard_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_matrix" );
    prom_get_guards = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_guards" );
    prom_get_all_guards = (const int**(*)())
        RT_optdlsym( filename, dlHandle, "spinja_get_all_guards" );
    prom_get_guard = (int(*)(void*,int,int*))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard" );
    prom_get_guard_all = (void(*)(void*,int*,int*))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_all" );
    prom_get_guard_may_be_coenabled_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_may_be_coenabled_matrix" );
    prom_get_guard_nes_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_nes_matrix" );
    prom_get_guard_nds_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "spinja_get_guard_nds_matrix" );

    (void)model;
}

int
cmpEnd(char *str, char *m)
{
    if (strlen(str) < strlen(m)) return 0;
    return strcmp(&str[strlen(str) - strlen(m)], m);
}

void
PromLoadGreyboxModel(model_t model, const char *filename)
{
    lts_type_t ltstype;
    matrix_t *dm_info = RTmalloc (sizeof *dm_info);
    matrix_t *dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_write_info = RTmalloc(sizeof(matrix_t));
    matrix_t *sl_info = RTmalloc (sizeof *sl_info);

    //assume sequential use:
    if (NULL == dlHandle) {
        char *extension = strrchr (filename, '.');
        HREassert (extension != NULL, "No filename extension in %s", filename);
        if (0==strcmp (extension, ".spinja")) {
            PromLoadDynamicLib (model, filename);
        } else {
            PromCompileGreyboxModel(model, filename);
        }
    }

    gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model,ctx);

    // get ltstypes
    int state_length = prom_get_state_size();
    ltstype=lts_type_create();

    // adding types
    int ntypes = prom_get_type_count();
    for (int i = 0; i < ntypes; i++) {
        const char* type_name = prom_get_type_name(i);
        HREassert (type_name != NULL, "invalid type name");
        if (lts_type_add_type(ltstype, type_name, NULL) != i) {
            Abort("wrong type number");
        }
        int type_value_count = prom_get_type_value_count(i);
        Debug("Promela type %s (%d) has %d values.", type_name, i, type_value_count);
        if (0 == type_value_count) {
            lts_type_set_format (ltstype, i, LTStypeDirect);
        } else {
             lts_type_set_format (ltstype, i, LTStypeEnum);
        }
    }

    int bool_is_new, bool_type = lts_type_add_type (ltstype, "bool", &bool_is_new);

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for (int i=0; i < state_length; ++i) {
        const char* name = prom_get_state_variable_name(i);
        const int   type = prom_get_state_variable_type(i);
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }

    int action_type = 0;
    int statement_type = 0;
    if (prom_get_edge_count() > 0) {
         action_type = lts_type_add_type(ltstype, LTSMIN_EDGE_TYPE_ACTION_PREFIX, NULL);
         statement_type = lts_type_add_type(ltstype, LTSMIN_EDGE_TYPE_STATEMENT, NULL);
    }
    GBsetLTStype(model, ltstype);

    if (bool_is_new) {
        GBchunkPutAt(model, bool_type, chunk_str("false"), 0);
        GBchunkPutAt(model, bool_type, chunk_str("true"), 1);
    }

    // setting values for types
    for(int i=0; i < ntypes; i++) {
        int type_value_count = prom_get_type_value_count(i);
        for(int j=0; j < type_value_count; ++j) {
            const char* type_value = prom_get_type_value_name(i, j);
            GBchunkPutAt(model, i, chunk_str((char*)type_value), j);
        }
    }

    if (prom_get_edge_count() > 0) {
        lts_type_set_edge_label_count(ltstype, 2);

        // All actions are assert statements. We do not export their values.
        lts_type_set_edge_label_name(ltstype, 0, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        lts_type_set_edge_label_type(ltstype, 0, LTSMIN_EDGE_TYPE_ACTION_PREFIX);
        lts_type_set_edge_label_typeno(ltstype, 0, action_type);
        for (int i = 0; i < prom_get_edge_count(); i++) {
           chunk c = chunk_str((char *)prom_get_edge_name(i));
           GBchunkPutAt(model, action_type, c, i);
        }

        lts_type_set_edge_label_name(ltstype, 1, LTSMIN_EDGE_TYPE_STATEMENT);
        lts_type_set_edge_label_type(ltstype, 1, LTSMIN_EDGE_TYPE_STATEMENT);
        lts_type_set_edge_label_typeno(ltstype, 1, statement_type);
        for (int i = 0; i < prom_get_transition_groups(); i++) {
            chunk c = chunk_str((char *)prom_get_group_name(i));
            GBchunkPutAt(model, statement_type, c, i);
        }
    }

    // get initial state
    int state[state_length];
    spinja_get_initial_state(state);
    GBsetInitialState(model,state);

    // check for guards
    int model_has_guards = 0;
    if (prom_get_guard_count
     || prom_get_guard_matrix
     || prom_get_guards
     || prom_get_all_guards
     || prom_get_guard
     || prom_get_guard_all ) {
        if (!prom_get_guard_count
         || !prom_get_guard_matrix
         || !prom_get_guards
         || !prom_get_all_guards
         || !prom_get_guard
         || !prom_get_guard_all) {
            Warning(info,"SpinJa guard functionality only partially available in model, ignoring guards");
        } else {
            model_has_guards = 1;
        }
    }
    
    // check for property
    int model_is_buchi = 0;
    int property_index = 0;
    for(int i = state_length; i--;) {
        char *name = lts_type_get_state_name(ltstype, i);
        if(!strcmp("never._pc", name)) {
            model_is_buchi = 1;
            property_index = i;
        }
    }

    int ngroups = prom_get_transition_groups();
    dm_create(dm_info, ngroups, state_length);
    dm_create(dm_read_info, ngroups, state_length);
    dm_create(dm_write_info, ngroups, state_length);
    for (int i=0; i < dm_nrows(dm_info); i++) {
        int* proj = (int*)prom_get_transition_read_dependencies(i);
        HREassert (proj != NULL, "No SpinJa read dependencies");
        for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
            if (proj[j]) dm_set(dm_read_info, i, j);
        }
        proj = (int*)prom_get_transition_write_dependencies(i);
        HREassert (proj != NULL, "No SpinJa write dependencies");
        for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
            if (proj[j]) dm_set(dm_write_info, i, j);
        }
    }
    GBsetDMInfo(model, dm_info);
    GBsetDMInfoRead(model, dm_read_info);
    GBsetDMInfoWrite(model, dm_write_info);

	// init state labels
	int sl_size = 0
	            + (model_has_guards   ? prom_get_guard_count() : 0)
	            + 1 // property is either accepting state or valid end state
	            ;
	lts_type_set_state_label_count (ltstype, sl_size);

	int sl_current = 0;
    lts_type_set_state_label_name   (ltstype, sl_current, "buchi_accept_spinja");
    lts_type_set_state_label_typeno (ltstype, sl_current, bool_type);
    GBsetAcceptingStateLabelIndex(model, sl_current);
    ++sl_current;

    dm_create(sl_info, sl_size, state_length);

    if (model_is_buchi) {
        dm_set(sl_info, GBgetAcceptingStateLabelIndex(model), property_index);
    } else {
        // overload accepting state semantics with valid end states semantics!
        for (int i = state_length; i--;) {
            char *name = lts_type_get_state_name(ltstype, i);
            if((!cmpEnd(name, "._pc")) || (!cmpEnd(name, "._nr_pr"))) {
                dm_set(sl_info, GBgetAcceptingStateLabelIndex(model), i);
            }
        }
    }

	if (model_has_guards) {
		char buf[256];
		int nguards = prom_get_guard_count();
		int guards_max = sl_current + prom_get_guard_count();
		for(;sl_current < guards_max; ++sl_current) {
			snprintf(buf, 256, "%s_%d", LTSMIN_LABEL_TYPE_GUARD_PREFIX, sl_current);
			lts_type_set_state_label_name (ltstype, sl_current, buf);
			lts_type_set_state_label_typeno (ltstype, sl_current, bool_type);
		}

	    // set the guards per transition group
	    GBsetGuardsInfo(model, (guard_t**) prom_get_all_guards());

	    // initialize state label matrix
	    // assumption, guards come first or second (0--nguards-1 | 1--nguards)
	    for(int i = 0; i < nguards; i++) {
	        int* guards = (int*)prom_get_guard_matrix(i);
	        for(int j=0; j<state_length; j++) {
	            if (guards[j]) dm_set(sl_info, i + 1, j);
	        }
	    }

	    // set guard may be co-enabled relation
	    if (prom_get_guard_may_be_coenabled_matrix) {
            matrix_t *gce_info = RTmalloc(sizeof(matrix_t));
	        dm_create(gce_info, nguards, nguards);
	        for(int i=0; i < nguards; i++) {
	            int* guardce = (int*)prom_get_guard_may_be_coenabled_matrix(i);
	            for(int j=0; j<nguards; j++) {
	                if (guardce[j]) dm_set(gce_info, i, j);
	            }
	        }
	        GBsetGuardCoEnabledInfo(model, gce_info);
	    }

	    // set guard necessary enabling set info
	    if (prom_get_guard_nes_matrix) {
            matrix_t *gnes_info = RTmalloc(sizeof(matrix_t));
	        dm_create(gnes_info, nguards, ngroups);
	        for(int i=0; i < nguards; i++) {
	            int* guardnes = (int*)prom_get_guard_nes_matrix(i);
	            for(int j=0; j<ngroups; j++) {
	                if (guardnes[j]) dm_set(gnes_info, i, j);
	            }
	        }
	        GBsetGuardNESInfo(model, gnes_info);
	    }

	    // set guard necessary disabling set info
	    if (prom_get_guard_nds_matrix) {
            matrix_t *gnds_info = RTmalloc(sizeof(matrix_t));
	        dm_create(gnds_info, nguards, ngroups);
	        for(int i=0; i < nguards; i++) {
	            int* guardnds = (int*)prom_get_guard_nds_matrix(i);
	            for(int j=0; j<ngroups; j++) {
	                if (guardnds[j]) dm_set(gnds_info, i, j);
	            }
	        }
	        GBsetGuardNDSInfo(model, gnds_info);
	    }
	}
    HREassert (0 == GBgetAcceptingStateLabelIndex(model), "Wrong accepting state label index");
	HREassert (sl_current==sl_size, "State labels wrongly initialized");

	GBsetStateLabelInfo(model, sl_info);

	lts_type_validate(ltstype);

    // set the group implementation
    sl_group_t* sl_group_all = RTmallocZero(sizeof(sl_group_t) + sl_size * sizeof(int));
    sl_group_all->count = sl_size;
    for(int i=0; i < sl_group_all->count; i++) sl_group_all->sl_idx[i] = i;
    GBsetStateLabelGroupInfo(model, GB_SL_ALL, sl_group_all);
    if (model_has_guards) {
        sl_group_t* sl_group_guards = RTmallocZero(sizeof(sl_group_t) + prom_get_guard_count() * sizeof(int));
        sl_group_guards->count = prom_get_guard_count();
        for(int i=0; i < sl_group_guards->count; i++) sl_group_guards->sl_idx[i] = i + 1;
        GBsetStateLabelGroupInfo(model, GB_SL_GUARDS, sl_group_guards);
    }
    GBsetStateLabelsGroup(model, sl_group);

    // get next state
    GBsetNextStateAll  (model, spinja_get_successor_all);
    GBsetNextStateLong (model, spinja_get_successor);

    // get state labels
    if (model_has_guards) {
        GBsetStateLabelLong(model, sl_long_p_g);
        GBsetStateLabelsAll(model, sl_all_p_g);
    } else {
        GBsetStateLabelLong(model, sl_long_p);
        GBsetStateLabelsAll(model, sl_all_p);
    }
}
