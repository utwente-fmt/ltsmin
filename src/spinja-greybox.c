#include <config.h>
#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <spinja-greybox.h>
#include <dm/dm.h>
#include <chunk_support.h>
#include <runtime.h>
#include <unix.h>

#define GUARD_CHECK_MISSING(g) { \
	if(g) { Warning(info,"missing guard function: get_guard_count"); }

// Remove when LTSmin has RT_optdlsym() again
#define RT_optdlsym(f,h,s) dlsym(h,s)

// spinja ltsmin interface functions
// if(!get_guard_count) Warning(info,"missing guard function: get_guard_count");
void        (*spinja_get_initial_state)(int *to);
next_method_grey_t spinja_get_successor;
next_method_black_t spinja_get_successor_all;

int         (*spinja_get_state_size)();
int         (*spinja_get_transition_groups)();
const int*  (*spinja_get_transition_read_dependencies)(int t);
const int*  (*spinja_get_transition_write_dependencies)(int t);
const char* (*spinja_get_state_variable_name)(int var);
int         (*spinja_get_state_variable_type)(int var);
const char* (*spinja_get_type_name)(int type);
int         (*spinja_get_type_count)();
const char* (*spinja_get_type_value_name)(int type, int value);
int         (*spinja_get_type_value_count)(int type);

int         (*spinja_buchi_is_accepting)(void* model, int* state);

int         (*get_guard_count)();
const int*  (*get_guard_matrix)(int g);
const int*  (*get_guards)(int t);
const int** (*get_all_guards)();
int         (*get_guard)(void*, int g, int *src);
void        (*get_guard_all)(void*, int *src, int* guards);
const int*  (*get_guard_may_be_coenabled_matrix)(int g);
const int*  (*get_guard_nes_matrix)(int g);
const int*  (*get_guard_nds_matrix)(int g);

static void
spinja_popt (poptContext con,
             enum poptCallbackReason reason,
             const struct poptOption *opt,
             const char *arg, void *data)
{
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		GBregisterPreLoader("spinja", SpinJaloadDynamicLib);
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

static void* dlHandle = NULL;

#define SYSFAIL(cond,...)                                               \
    do { if (cond) FatalCall(__VA_ARGS__) Fatal(__VA_ARGS__); } while (0)
void
SpinJacompileGreyboxModel(model_t model, const char *filename)
{
    (void)model;
    (void)filename;
    /* FIXME */
    FatalCall(1, error, "Unimplemented: automatic compilation of promela code, please do this manually");
}
#undef SYSFAIL

static int sl_long_p (model_t model, int label, int *state) {
	if (label == GBgetAcceptingStateLabelIndex(model)) {
		return spinja_buchi_is_accepting(model, state);
	} else {
		Abort("unexpected state label requested: %d", label);
	}
}

static void sl_all_p (model_t model, int *state, int *labels) {
	assert (labels != NULL);
	labels[GBgetAcceptingStateLabelIndex(model)] = spinja_buchi_is_accepting(model, state);
}

void
SpinJaloadDynamicLib(model_t model, const char *filename)
{
    // Open spinja file
    char abs_filename[PATH_MAX];
    char *ret_filename = realpath(filename, abs_filename);
    if (ret_filename != NULL) {
        dlHandle = dlopen (abs_filename, RTLD_LAZY);
        if (dlHandle == NULL) {
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
    spinja_get_successor_all = (next_method_black_t)
        RTdlsym( filename, dlHandle, "spinja_get_successor_all" );

    spinja_get_state_size = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_state_size" );

    spinja_get_transition_groups = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_transition_groups" );

    spinja_get_transition_read_dependencies = (const int*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_transition_read_dependencies" );
    spinja_get_transition_write_dependencies = (const int*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_transition_write_dependencies" );

    spinja_get_state_variable_name = (const char*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_state_variable_name" );
    spinja_get_state_variable_type = (int (*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_state_variable_type" );
    spinja_get_type_name = (const char*(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_type_name" );
    spinja_get_type_count = (int(*)())
        RTdlsym( filename, dlHandle, "spinja_get_type_count" );
    spinja_get_type_value_name = (const char*(*)(int,int))
        RTdlsym( filename, dlHandle, "spinja_get_type_value_name" );
    spinja_get_type_value_count = (int(*)(int))
        RTdlsym( filename, dlHandle, "spinja_get_type_value_count" );
    spinja_buchi_is_accepting = (int(*)())
        RT_optdlsym( filename, dlHandle, "spinja_buchi_is_accepting" );

    // optional, guard support (used for por)
    get_guard_count = (int(*)())
        RT_optdlsym( filename, dlHandle, "get_guard_count" );
    get_guard_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "get_guard_matrix" );
    get_guards = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "get_guards" );
    get_all_guards = (const int**(*)())
        RT_optdlsym( filename, dlHandle, "get_all_guards" );
    get_guard = (int(*)(void*,int,int*))
        RT_optdlsym( filename, dlHandle, "get_guard" );
    get_guard_all = (void(*)(void*,int*,int*))
        RT_optdlsym( filename, dlHandle, "get_guard_all" );
    get_guard_may_be_coenabled_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "get_guard_may_be_coenabled_matrix" );
    get_guard_nes_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "get_guard_nes_matrix" );
    get_guard_nds_matrix = (const int*(*)(int))
        RT_optdlsym( filename, dlHandle, "get_guard_nds_matrix" );

    (void)model;
}

void
SpinJaloadGreyboxModel(model_t model, const char *filename)
{
    lts_type_t ltstype;
    matrix_t *dm_info = RTmalloc (sizeof *dm_info);
    matrix_t *dm_read_info = RTmalloc(sizeof(matrix_t));
    matrix_t *dm_write_info = RTmalloc(sizeof(matrix_t));
    matrix_t *sl_info = RTmalloc (sizeof *sl_info);

    //assume sequential use:
    if (NULL == dlHandle) {
        SpinJaloadDynamicLib (model, filename);
    }

    gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model,ctx);

    // get ltstypes
    int state_length = spinja_get_state_size(); /* FIXME: _state_var_count() */
    ltstype=lts_type_create();

    // adding types
    int ntypes = spinja_get_type_count();
    for (int i=0; i < ntypes; i++) {
        const char* type_name = spinja_get_type_name(i);
        if(!type_name) {
            Fatal(1,error,"invalid type name");
        }
        if (lts_type_add_type(ltstype,type_name,NULL) != i) {
            Fatal(1,error,"wrong type number");
        }
    }
    int bool_is_new, bool_type = lts_type_add_type (ltstype, "bool", &bool_is_new);

    lts_type_set_state_length(ltstype, state_length);

    // set state name & type
    for (int i=0; i < state_length; ++i) {
        const char* name = spinja_get_state_variable_name(i);
        const int   type = spinja_get_state_variable_type(i);
        lts_type_set_state_name(ltstype,i,name);
        lts_type_set_state_typeno(ltstype,i,type);
    }

    GBsetLTStype(model, ltstype);

    // setting values for types
    for(int i=0; i < ntypes; i++) {
        int type_value_count = spinja_get_type_value_count(i);
        for(int j=0; j < type_value_count; ++j) {
            const char* type_value = spinja_get_type_value_name(i, j);
            GBchunkPut(model, i, chunk_str((char*)type_value));
        }
    }

    // check for guards
    int model_has_guards = 0;
    if (get_guard_count
     || get_guard_matrix
     || get_guards
     || get_all_guards
     || get_guard
     || get_guard_all ) {
        if (!get_guard_count
         || !get_guard_matrix
         || !get_guards
         || !get_all_guards
         || !get_guard
         || !get_guard_all) {
            Warning(info,"SpinJa guard functionality only partially available in model, ignoring guards");
        } else {
            model_has_guards = 1;
        }
    }
    
    // check for property
    int model_has_property = 0;
    if ( spinja_buchi_is_accepting ) {
        for(int i=state_length; i--;) {
            if(!strcmp("never.pc",lts_type_get_state_name(ltstype,i))) {
                model_has_property = 1;
            }
        }
    }

    int ngroups = spinja_get_transition_groups(); /* FIXME: _count */
    dm_create(dm_info, ngroups, state_length);
    dm_create(dm_read_info, ngroups, state_length);
    dm_create(dm_write_info, ngroups, state_length);
    for (int i=0; i < dm_nrows(dm_info); i++) {
        int* proj = (int*)spinja_get_transition_read_dependencies(i);
        assert (proj != NULL);
        for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
        }
        proj = (int*)spinja_get_transition_write_dependencies(i);
        assert (proj != NULL);
        for(int j=0; j<state_length; j++) {
            if (proj[j]) dm_set(dm_info, i, j);
        }
    }
    GBsetDMInfo(model, dm_info);
    GBsetDMInfoRead(model, dm_read_info);
    GBsetDMInfoWrite(model, dm_write_info);

	// init state labels
	int sl_size = 0
	            + (model_has_guards   ? get_guard_count() : 0)
	            + (model_has_property ? 1                 : 0)
	            ;
	lts_type_set_state_label_count (ltstype, sl_size);

	int sl_current = 0;
	if(model_has_property) {
		lts_type_set_state_label_name   (ltstype, sl_current, "buchi_accept_spinja");
		lts_type_set_state_label_typeno (ltstype, sl_current, bool_type);
		GBsetAcceptingStateLabelIndex(model,sl_current);
		++sl_current;
	}
	if (model_has_guards) {
		char buf[256];
		int guards_max = sl_current + get_guard_count();
		for(;sl_current < guards_max; ++sl_current) {
			snprintf(buf, 256, "guard_%d", sl_current);
			lts_type_set_state_label_name (ltstype, sl_current, buf);
			lts_type_set_state_label_typeno (ltstype, sl_current, bool_type);
		}
	}
	
	assert(sl_current==sl_size);

	dm_create(sl_info, sl_size, state_length);
	
	if(model_has_property) {
		for(int i=state_length; i--;) {
			if(!strcmp("never.pc",lts_type_get_state_name(ltstype,i))) {
				printf("found property: %i\n",i);
				dm_set(sl_info, GBgetAcceptingStateLabelIndex(model), i);
			}
		}
	}
	
	GBsetStateLabelInfo(model, sl_info);

	lts_type_validate(ltstype);

    // get initial state
    int state[state_length];
    spinja_get_initial_state(state);
    GBsetInitialState(model,state);

    GBsetNextStateAll  (model, spinja_get_successor_all);
    GBsetNextStateLong (model, spinja_get_successor);
	if(model_has_property) {
		GBsetStateLabelLong(model, sl_long_p);
		GBsetStateLabelsAll(model, sl_all_p);
	}
}
