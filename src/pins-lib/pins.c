#include <hre/config.h>
#include <stdlib.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <hre/stringindex.h>
#include <ltsmin-lib/ltsmin-standard.h>
#include <pins-lib/pins.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/pins2pins-cache.h>
#include <pins-lib/pins2pins-fork.h>
#include <pins-lib/pins2pins-group.h>
#include <pins-lib/pins2pins-guards.h>
#include <pins-lib/pins2pins-check.h>
#include <pins-lib/pins2pins-ltl.h>
#include <pins-lib/pins2pins-mucalc.h>
#include <pins-lib/pins2pins-fork.h>
#include <pins-lib/por/pins2pins-por.h>
#include <util-lib/treedbs.h>

/** \file pins.c */

struct grey_box_model {
	model_t parent;
	lts_type_t ltstype;
	matrix_t *dm_info;
	matrix_t *dm_read_info;
	matrix_t *dm_may_write_info;
	matrix_t *dm_must_write_info;
	matrix_t *sl_info;
    sl_group_t *sl_groups[GB_SL_GROUP_COUNT];
    guard_t **guards;
    matrix_t *commutes_info; // commutes info
    matrix_t *gce_info; // guard co-enabled info
    matrix_t *dna_info; // do not accord info
    matrix_t *gnes_info; // guard necessary enabling set
    matrix_t *gnds_info; // guard necessary disabling set
    int *group_visibility;
    int *label_visibility;
	int *s0;
	void *context;
    next_method_grey_t next_short;
    next_method_grey_t next_short_r2w;
	next_method_grey_t next_long;
    next_method_grey_t actions_short;
    next_method_grey_t actions_short_r2w;
    next_method_grey_t actions_long;
	next_method_matching_t next_matching;
	next_method_black_t next_all;
	get_label_method_t state_labels_short;
	get_label_method_t state_labels_long;
	get_label_group_method_t state_labels_group;
	get_label_all_method_t state_labels_all;
	groups_of_edge_t groups_of_edge;
	covered_by_grey_t covered_by;
    covered_by_grey_t covered_by_short;
    table_factory_t chunk_factory;
	void **map;
	chunk2pretty_t chunk2pretty;
	string_set_t default_filter;

	/** Index of static information matrices. */
	string_index_t static_info_index;
	/** Array of static information matrices. */
	struct static_info_matrix * static_info_matrices;

	ExitCB exit;

	int *var_perm;
	int *group_perm;
};

struct static_info_matrix{
    const char* name;
    matrix_t*matrix;
    pins_strictness_t strictness;
    index_class_t row_info;
    index_class_t column_info;
};

int GBsetMatrix(
    model_t model,
    const char*name,
    matrix_t *matrix,
    pins_strictness_t strictness,
    index_class_t row_info,
    index_class_t column_info
){
    int res=SIput(model->static_info_index,name);
    model->static_info_matrices[res].name=name;
    model->static_info_matrices[res].matrix=matrix;
    model->static_info_matrices[res].strictness=strictness;
    model->static_info_matrices[res].row_info=row_info;
    model->static_info_matrices[res].column_info=column_info;
    return res;
}


int GBgetMatrixID(model_t model,char*name){
    return SIlookup(model->static_info_index,name);
}

int GBgetMatrixCount(model_t model){
    return SIgetCount(model->static_info_index);
}

const char* GBgetMatrixName(model_t model,int ID){
    return model->static_info_matrices[ID].name;
}

matrix_t* GBgetMatrix(model_t model,int ID){
    return model->static_info_matrices[ID].matrix;
}

pins_strictness_t GBgetMatrixStrictness(model_t model,int ID){
    return model->static_info_matrices[ID].strictness;
}

index_class_t GBgetMatrixRowInfo(model_t model,int ID){
    return model->static_info_matrices[ID].row_info;
}

index_class_t GBgetMatrixColumnInfo(model_t model,int ID){
    return model->static_info_matrices[ID].column_info;
}

struct nested_cb {
	model_t model;
	int group;
	int *src;
	TransitionCB cb;
	void* user_ctx;
};

void project_dest(void*context,transition_info_t*ti,int*dst,int*cpy){
#define info ((struct nested_cb*)context)
    int len = dm_ones_in_row(GBgetDMInfo(info->model), info->group);
    int short_dst[len];

    dm_project_vector(GBgetDMInfo(info->model), info->group, dst, short_dst);
    ti->group = info->group;
    if (cpy != NULL) {
        int short_cpy[len];
        dm_project_vector(GBgetDMInfo(info->model), info->group, cpy, short_cpy);
        info->cb(info->user_ctx,ti,short_dst,short_cpy);
    } else {
        info->cb(info->user_ctx,ti,short_dst,NULL);
    }
#undef info
}

void project_dest_write(void*context,transition_info_t*ti,int*dst,int*cpy){
#define info ((struct nested_cb*)context)
    int len = dm_ones_in_row(GBgetDMInfoMayWrite(info->model), info->group);
    int short_dst[len];

    dm_project_vector(GBgetDMInfoMayWrite(info->model), info->group, dst, short_dst);
    ti->group = info->group;
    if (cpy != NULL) {
        int short_cpy[len];
        dm_project_vector(GBgetDMInfoMayWrite(info->model), info->group, cpy, short_cpy);
        info->cb(info->user_ctx,ti,short_dst,short_cpy);
    } else {
        info->cb(info->user_ctx,ti,short_dst,NULL);
    }
#undef info
}

int default_short(model_t self,int group,int*src,TransitionCB cb,void*context){
    struct nested_cb info;
    info.model = self;
    info.group = group;
    info.src=src;
    info.cb=cb;
    info.user_ctx=context;

    int long_src[dm_ncols(GBgetDMInfo(self))];
    dm_expand_vector(GBgetDMInfo(self), group, self->s0, src, long_src);

    return self->next_long(self,group,long_src,project_dest,&info);
}

int default_short_r2w(model_t self,int group,int*src,TransitionCB cb,void*context){
    struct nested_cb info;
    info.model = self;
    info.group = group;
    info.src=src;
    info.cb=cb;
    info.user_ctx=context;

    int long_src[dm_ncols(GBgetDMInfoRead(self))];
    dm_expand_vector(GBgetDMInfoRead(self), group, self->s0, src, long_src);

    return self->next_long(self,group,long_src,project_dest_write,&info);
}

void expand_dest(void*context,transition_info_t*ti,int*dst, int*cpy){
#define info ((struct nested_cb*)context)
    int len = dm_ncols (GBgetDMInfo(info->model));
    int long_dst[len];
	dm_expand_vector(GBgetDMInfo(info->model), info->group, info->src, dst, long_dst);
    if (cpy != NULL) {
        int long_cpy[len];
        dm_expand_vector(GBgetDMInfo(info->model), info->group, info->src, dst, long_cpy);
        info->cb(info->user_ctx,ti,long_dst,long_cpy);
    } else {
        info->cb(info->user_ctx,ti,long_dst,NULL);
    }
#undef info
}

int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.model = self;
	info.group = group;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;

	const int len = dm_ones_in_row(GBgetDMInfo(self), group);
	int src_short[len];
	dm_project_vector(GBgetDMInfo(self), group, src, src_short);

	return self->next_short(self,group,src_short,expand_dest,&info);
}

int default_actions_short(model_t self,int group,int*src,TransitionCB cb,void*context){
    struct nested_cb info;
    info.model = self;
    info.group = group;
    info.src=src;
    info.cb=cb;
    info.user_ctx=context;

    int long_src[dm_ncols(GBgetDMInfo(self))];
    dm_expand_vector(GBgetDMInfo(self), group, self->s0, src, long_src);
    return self->actions_long(self,group,long_src,project_dest,&info);
}

int default_actions_short_r2w(model_t self,int group,int*src,TransitionCB cb,void*context){
    struct nested_cb info;
    info.model = self;
    info.group = group;
    info.src=src;
    info.cb=cb;
    info.user_ctx=context;

    matrix_t* read = GBgetMatrix(self, GBgetMatrixID(self, LTSMIN_MATRIX_ACTIONS_READS));

    int long_src[dm_ncols(read)];
    dm_expand_vector(read, group, self->s0, src, long_src);
    return self->actions_long(self,group,long_src,project_dest_write,&info);
}

int default_actions_long(model_t self,int group,int*src,TransitionCB cb,void*context){
    struct nested_cb info;
    info.model = self;
    info.group = group;
    info.src=src;
    info.cb=cb;
    info.user_ctx=context;

    matrix_t* read = GBgetMatrix(self, GBgetMatrixID(self, LTSMIN_MATRIX_ACTIONS_READS));

    const int len = dm_ones_in_row(read, group);
    int src_short[len];
    dm_project_vector(read, group, src, src_short);

    return self->actions_short(self,group,src_short,expand_dest,&info);
}

int GBgetTransitionsMarked(model_t self,matrix_t* matrix,int row,int*src,TransitionCB cb,void*context){
    int N=dm_ncols(matrix);
    int res=0;
    for(int i=0;i<N;i++){
        if (dm_is_set(matrix,row,i)){
            res+=self->next_long(self,i,src,cb,context);
        }
    }
    return res;
}

int default_all(model_t self,int*src,TransitionCB cb,void*context){
	int res=0;
	for(int i=0; i < dm_nrows(GBgetDMInfo(self)); i++) {
		res+=self->next_long(self,i,src,cb,context);
	}
	return res;
}

static int
state_labels_default_short(model_t model, int label, int *state)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);
    int long_state[dm_ncols(sl_info)];
    dm_expand_vector(sl_info, label, model->s0, state, long_state);
    return model->state_labels_long(model, label, long_state);
}

static int
state_labels_default_long(model_t model, int label, int *state)
{
    matrix_t *sl_info = GBgetStateLabelInfo(model);
    int len = dm_ones_in_row(sl_info, label);
    int short_state[len];
    dm_project_vector(sl_info, label, state, short_state);
    return model->state_labels_short(model, label, short_state);
}

static void
state_labels_default_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    switch (group)
    {
        case GB_SL_ALL:
            GBgetStateLabelsAll(model, state, labels);
            return;
        case GB_SL_GUARDS:
            /**
             * This could potentially return a trivial guard for each transition group
             * by calling state next, return 1 if a next state is found and 0 if not.
             * This setup should be synchronized with the other guard functionality
             */
            Abort( "No default for guard group available in GBgetStateLabelsGroup" );
        default:
            Abort( "Unknown group in GBgetStateLabelsGroup" );
    }
}

static void
state_labels_default_all(model_t model, int *state, int *labels)
{
	for(int i=0;i<dm_nrows(GBgetStateLabelInfo(model));i++) {
		labels[i]=model->state_labels_long(model,i,state);
	}
}

static int
groups_of_edge_default(model_t model, int edgeno, int index, int** groups)
{
    (void) edgeno; (void) index;
    *groups = RTmalloc(sizeof(int) * pins_get_group_count(model));
    for (size_t i = 0; i < pins_get_group_count(model); i++) (*groups)[i] = i;

    return pins_get_group_count(model);
}

int
wrapped_default_short (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsShort (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_short_r2w (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsShortR2W (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_long (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsLong (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_actions_short (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetActionsShort (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_actions_short_r2w (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetActionsShortR2W (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_actions_long (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetActionsLong (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_all (model_t self,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsAll(GBgetParent(self), src, cb, context);
}

static int
wrapped_state_labels_default_short (model_t model, int label, int *state)
{
    return GBgetStateLabelShort(GBgetParent(model), label, state);
}

static int
wrapped_state_labels_default_long(model_t model, int label, int *state)
{
    return GBgetStateLabelLong(GBgetParent(model), label, state);
}

static void
wrapped_state_labels_default_group(model_t model, sl_group_enum_t group, int *state, int *labels)
{
    GBgetStateLabelsGroup(GBgetParent(model), group, state, labels);
}

static void
wrapped_state_labels_default_all(model_t model, int *state, int *labels)
{
    return GBgetStateLabelsAll(GBgetParent(model), state, labels);
}

static void
wrapped_exit_default(model_t model)
{
    GBExit(GBgetParent(model));
}

static int
wrapped_default_groups_of_edge(model_t model, int edgeno, int index, int** groups)
{
    return GBgroupsOfEdge(GBgetParent(model), edgeno, index, groups);
}

struct filter_context {
    void *user_ctx;
    TransitionCB user_cb;
    int idx;
    int value;
    int count;
    int group_idx;
    int group_val;
};

static void matching_callback(void*context,transition_info_t*ti,int*dst,int*cpy){
    struct filter_context* ctx=(struct filter_context*)context;
    if (ti->labels[ctx->idx]==ctx->value){
        ctx->user_cb(ctx->user_ctx,ti,dst,cpy);
        if (ctx->group_idx>=0){
            if (ti->labels[ctx->group_idx]==0 || ti->labels[ctx->group_idx]!=ctx->group_val){
                ctx->group_val=ti->labels[ctx->group_idx];
                ctx->count++;
            }
        } else {
            ctx->count++;
        }
    }
}

static int next_matching_default(model_t model,int label_idx,int value,int*src,TransitionCB cb,void*context){
    struct filter_context ctx;
    ctx.user_ctx=context;
    ctx.user_cb=cb;
    ctx.idx=label_idx;
    ctx.group_idx=lts_type_find_edge_label(model->ltstype,LTSMIN_EDGE_TYPE_HYPEREDGE_GROUP);
    ctx.value=value;
    ctx.count=0;
    ctx.group_val=0;
    GBgetTransitionsAll(model,src,matching_callback,&ctx);
    return ctx.count;
}


model_t GBcreateBase(){
	model_t model=(model_t)RTmalloc(sizeof(struct grey_box_model));
    model->parent=NULL;
	model->ltstype=NULL;
	model->dm_info=NULL;
	model->dm_read_info=NULL;
	model->dm_may_write_info=NULL;
	model->dm_must_write_info=NULL;
	model->sl_info=NULL;
    for(int i=0; i < GB_SL_GROUP_COUNT; i++)
        model->sl_groups[i]=NULL;
    model->guards=NULL;
    model->group_visibility=NULL;
    model->label_visibility=NULL;
    model->commutes_info=NULL;
    model->gce_info=NULL;
    model->dna_info=NULL;
    model->gnes_info=NULL;
    model->gnds_info=NULL;
	model->s0=NULL;
	model->context=0;
    model->next_short=default_short;
    model->next_short_r2w=default_short_r2w;
	model->next_long=default_long;
    model->actions_short=default_actions_short;
    model->actions_short_r2w=default_actions_short_r2w;
    model->actions_long=default_actions_long;
	model->next_matching=next_matching_default;
	model->next_all=default_all;
	model->state_labels_short=state_labels_default_short;
	model->state_labels_long=state_labels_default_long;
	model->state_labels_group=state_labels_default_group;
	model->state_labels_all=state_labels_default_all;
	model->groups_of_edge=groups_of_edge_default;
	model->map=NULL;
	model->chunk_factory=NULL;

	model->static_info_index=SIcreate();
	model->static_info_matrices=NULL;
	ADD_ARRAY(SImanager(model->static_info_index),model->static_info_matrices,struct static_info_matrix);

	model->exit = NULL;

	model->var_perm = NULL;
	model->group_perm = NULL;

	return model;
}

model_t
GBgetParent(model_t model)
{
    return model->parent;
}

void GBinitModelDefaults (model_t *p_model, model_t default_src)
{
    model_t model = *p_model;
    model->parent = default_src;
    if (model->ltstype == NULL) {
        GBcopyChunkMaps(model, default_src);
        GBsetLTStype(model, GBgetLTStype(default_src));
    }

    /* Copy dependency matrices. We cannot use the GBgetDMInfoRead and
     * GBgetDMInfoMayWrite calls here, as these default to the combined
     * matrix of the parent, which can be the wrong matrices in case
     * a certain pins2pins layer has restricted functionality and only
     * overwrites the combined matrix (like the regrouping layer before
     * becoming aware of the read and write matrices).  In this degenerated
     * case do the conservative thing and use the combined matrix that was
     * set.
     */
    if (model->dm_read_info == NULL) {
        if (model->dm_info == NULL)
            model->dm_read_info = default_src->dm_read_info;
        else
            model->dm_read_info = model->dm_info;
    }
    if (model->dm_may_write_info == NULL) {
        if (model->dm_info == NULL)
            model->dm_may_write_info = default_src->dm_may_write_info;
        else
            model->dm_may_write_info = model->dm_info;
    }
    if (model->dm_must_write_info == NULL) {
        if (model->dm_info == NULL)
            model->dm_must_write_info = default_src->dm_must_write_info;
        else
            model->dm_must_write_info = model->dm_info;
    }
    if (model->dm_info == NULL)
        model->dm_info = default_src->dm_info;

    if (model->sl_info == NULL)
        GBsetStateLabelInfo(model, GBgetStateLabelInfo(default_src));

    for(int i=0; i < GB_SL_GROUP_COUNT; i++)
        GBsetStateLabelGroupInfo(model, i, GBgetStateLabelGroupInfo(default_src, i));

    if (model->guards == NULL)
        GBsetGuardsInfo(model, GBgetGuardsInfo(default_src));

    if (model->gce_info == NULL)
        GBsetGuardCoEnabledInfo(model, GBgetGuardCoEnabledInfo (default_src));

    if (model->dna_info == NULL)
        GBsetDoNotAccordInfo(model, GBgetDoNotAccordInfo (default_src));

    if (model->commutes_info == NULL)
        GBsetCommutesInfo(model, GBgetCommutesInfo (default_src));

    if (model->group_visibility == NULL)
        GBsetPorGroupVisibility (model, GBgetPorGroupVisibility(default_src));

    if (model->label_visibility == NULL)
        GBsetPorStateLabelVisibility (model, GBgetPorStateLabelVisibility(default_src));

    if (model->gnes_info == NULL)
        GBsetGuardNESInfo(model, GBgetGuardNESInfo (default_src));

    if (model->gnds_info == NULL)
        GBsetGuardNDSInfo(model, GBgetGuardNDSInfo (default_src));

    if (model->default_filter == NULL)
        GBsetDefaultFilter (model, GBgetDefaultFilter(default_src));

    for (int i = 0; i < GBgetMatrixCount(default_src); i++) {
        const char* name = GBgetMatrixName(default_src, i);
        if (GBgetMatrixID(model, (char*) name) == SI_INDEX_FAILED) {
            pins_strictness_t strictness = GBgetMatrixStrictness(default_src, i);
            index_class_t row_info = GBgetMatrixRowInfo(default_src, i);
            index_class_t column_info = GBgetMatrixColumnInfo(default_src, i);
            GBsetMatrix(model, name, GBgetMatrix(default_src, i), strictness, row_info, column_info);
        }
    }

    if (model->s0 == NULL) {
        int N = lts_type_get_state_length (GBgetLTStype (default_src));
        int s0[N];
        GBgetInitialState(default_src, s0);
        GBsetInitialState(model, s0);
    }
    if (model->context == NULL)
        GBsetContext(model, GBgetContext(default_src));

    /* Since the model->next_{short,long,all} functions have mutually
     * recursive implementations, at least one needs to be overridden,
     * and the others need to call the overridden one (and not the
     * ones in the parent layer).
     *
     * If neither function is overridden, we pass through to the
     * parent layer.  However, we need to strip down the passed
     * model_t parameter, hence the wrapped_* functions.
     *
     * This scheme has subtle consequences: Assume a wrapper which
     * only implements a next_short function, but no next_long or
     * next_all.  If next_all is called, it will end up in the
     * next_short call (via the mutually recursive default
     * implementations), and eventually call through to the parent
     * layer's next_short.
     *
     * Hence, even if the parent layer also provides an optimized
     * next_all, it will never be called, unless the wrapper also
     * implements a next_all.
     */
    if (model->next_short == default_short &&
        model->next_short_r2w == default_short_r2w &&
        model->next_long == default_long &&
        model->actions_short == default_actions_short &&
        model->actions_short_r2w == default_actions_short_r2w &&
        model->actions_long == default_actions_long &&
        model->next_all == default_all) {
        GBsetNextStateShort (model, wrapped_default_short);
        GBsetNextStateShortR2W (model, wrapped_default_short_r2w);
        GBsetNextStateLong (model, wrapped_default_long);
        GBsetNextStateAll (model, wrapped_default_all);
        GBsetActionsLong (model, wrapped_default_actions_long);
        GBsetActionsShort (model, wrapped_default_actions_short);
        GBsetActionsShortR2W (model, wrapped_default_actions_short_r2w);
    }

    if (model->state_labels_short == state_labels_default_short &&
        model->state_labels_long == state_labels_default_long &&
        model->state_labels_all == state_labels_default_all) {
        GBsetStateLabelShort (model, wrapped_state_labels_default_short);
        GBsetStateLabelLong (model, wrapped_state_labels_default_long);
        GBsetStateLabelsGroup (model, wrapped_state_labels_default_group);
        GBsetStateLabelsAll (model, wrapped_state_labels_default_all);
    }
    if (model->covered_by == NULL)
        GBsetIsCoveredBy(model, default_src->covered_by);
    if (model->covered_by_short == NULL)
        GBsetIsCoveredByShort(model, default_src->covered_by_short);

    model->exit = wrapped_exit_default;

    model->var_perm = default_src->var_perm;
    model->group_perm = default_src->group_perm;

    if (model->groups_of_edge == groups_of_edge_default) {
        GBsetGroupsOfEdge(model, wrapped_default_groups_of_edge);
    }
}

void* GBgetContext(model_t model){
	return model->context;
}
void GBsetContext(model_t model,void* context){
	model->context=context;
}

void GBsetLTStype(model_t model,lts_type_t info){
	if (model->ltstype != NULL)  Abort("ltstype already set");
    lts_type_validate(info);
	model->ltstype=info;
    if (model->map==NULL){
	    int N=lts_type_get_type_count(info);
	    model->map=RTmallocZero(N*sizeof(void*));
	    for(int i=0;i<N;i++){
		    model->map[i] = TFnewTable (model->chunk_factory);
	    }
    }
}

lts_type_t GBgetLTStype(model_t model){
	return model->ltstype;
}

void GBsetDMInfo(model_t model, matrix_t *dm_info) {
	if (model->dm_info != NULL) Abort("dependency matrix already set");
	model->dm_info=dm_info;

    // Since the "actions_reads" matrix is a subset of the dependencies
    // of the combined matrix we may also set the "actions_reads" matrix.
    if (GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS) == SI_INDEX_FAILED) {
        GBsetMatrix(model, LTSMIN_MATRIX_ACTIONS_READS, dm_info, PINS_MAY_SET,
            PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);
    }
}

matrix_t *GBgetDMInfo(model_t model) {
    if (model->dm_info == NULL) Abort("dependency matrix not set");
	return model->dm_info;
}

void GBsetDMInfoRead(model_t model, matrix_t *dm_info) {
	if (model->dm_read_info != NULL) Abort("dependency matrix already set");

	model->dm_read_info=dm_info;

	// Since the "actions_reads" matrix is a subset of the dependencies
	// of the read matrix we may also set the "actions_reads" matrix.
	if (GBgetMatrixID(model, LTSMIN_MATRIX_ACTIONS_READS) == SI_INDEX_FAILED) {
	    GBsetMatrix(model, LTSMIN_MATRIX_ACTIONS_READS, dm_info, PINS_MAY_SET,
            PINS_INDEX_GROUP, PINS_INDEX_STATE_VECTOR);
	}
}

matrix_t *GBgetDMInfoRead(model_t model) {
	if (model->dm_read_info == NULL) {
        Warning(debug, "read dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
	return model->dm_read_info;
}

static int write_checked=0;

static void check_write_matrices(model_t model) {

    if (write_checked) return;
    write_checked=1;

    matrix_t* test = RTmalloc(sizeof(matrix_t));
    dm_copy(GBgetDMInfoMayWrite(model), test);
    dm_apply_or(test, GBgetDMInfoMustWrite(model));
    if (!dm_equals(test, GBgetDMInfoMayWrite(model)))
        Abort("Must-write matrix should be a subset of the may-write matrix.");
}

void GBsetDMInfoMayWrite(model_t model, matrix_t *dm_info) {
	if (model->dm_may_write_info != NULL) Abort("dependency matrix already set");
	model->dm_may_write_info=dm_info;
}

matrix_t *GBgetDMInfoMayWrite(model_t model) {
    check_write_matrices(model);
	if (model->dm_may_write_info == NULL) {
        Warning(debug, "May-write dependency matrix not set, returning must-write matrix");
        return GBgetDMInfoMustWrite(model);
    }
	return model->dm_may_write_info;
}

void GBsetDMInfoMustWrite(model_t model, matrix_t *dm_info) {
    if (model->dm_must_write_info != NULL) Abort("dependency matrix already set");
    model->dm_must_write_info=dm_info;
}

matrix_t *GBgetDMInfoMustWrite(model_t model) {
    check_write_matrices(model);
    if (model->dm_must_write_info == NULL) {
        if (model->dm_may_write_info != NULL) {
            Abort("If the may-write matrix is set, then the must-write matrix must also be set");
        }
        Warning(debug, "must-write dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
    return model->dm_must_write_info;
}

void GBsetStateLabelInfo(model_t model, matrix_t *info){
	if (model->sl_info != NULL)  Abort("state info already set");
	model->sl_info=info;
}

matrix_t *GBgetStateLabelInfo(model_t model){
	return model->sl_info;
}

void GBsetInitialState(model_t model,int *state){
	if (model->ltstype==NULL)
            Abort("must set ltstype before setting initial state");
	RTfree (model->s0);
	int len=lts_type_get_state_length(model->ltstype);
	model->s0=(int*)RTmalloc(len * sizeof(int));
	for(int i=0;i<len;i++){
		model->s0[i]=state[i];
	}
}

void GBgetInitialState(model_t model,int *state){
	int len=lts_type_get_state_length(model->ltstype);
	for(int i=0;i<len;i++){
		state[i]=model->s0[i];
	}
}

void GBsetActionsShort(model_t model,next_method_grey_t method){
    model->actions_short=method;
}

void GBsetActionsShortR2W(model_t model, next_method_grey_t method) {
    model->actions_short_r2w=method;
}

int GBgetActionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
    return model->actions_short(model,group,src,cb,context);
}

int GBgetActionsShortR2W(model_t model,int group,int*src,TransitionCB cb,void*context){
    return model->actions_short_r2w(model,group,src,cb,context);
}

void GBsetActionsLong(model_t model,next_method_grey_t method){
    model->actions_long=method;
}

int GBgetActionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
    return model->actions_long(model,group,src,cb,context);
}

void GBsetNextStateShort(model_t model,next_method_grey_t method){
    model->next_short=method;
}

void GBsetNextStateShortR2W(model_t model,next_method_grey_t method){
    model->next_short_r2w=method;
}

int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
    return model->next_short(model,group,src,cb,context);
}

int GBgetTransitionsShortR2W(model_t model,int group,int*src,TransitionCB cb,void*context){
    return model->next_short_r2w(model,group,src,cb,context);
}

void GBsetNextStateLong(model_t model,next_method_grey_t method){
	model->next_long=method;
}

int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	return model->next_long(model,group,src,cb,context);
}

void GBsetNextStateMatching(model_t model,next_method_matching_t method){
	model->next_matching=method;
}

int GBgetTransitionsMatching(model_t model,int label_idx,int value,int*src,TransitionCB cb,void*context){
    return model->next_matching(model,label_idx,value,src,cb,context);
}

void GBsetIsCoveredBy(model_t model,covered_by_grey_t covered_by){
    model->covered_by = covered_by;
}

void GBsetIsCoveredByShort(model_t model,covered_by_grey_t covered_by_short){
    model->covered_by_short = covered_by_short;
}

int GBisCoveredByShort(model_t model,int*a,int*b) {
    if (NULL == model->covered_by_short)
        Abort("No symbolic comparison function (covered_by_short) present for loaded model.");
    return model->covered_by_short(a,b);
}

int GBisCoveredBy(model_t model,int*a,int*b) {
    if (NULL == model->covered_by)
        Abort("No symbolic comparison function (covered_by) present for loaded model.");
    return model->covered_by(a,b);
}

void GBsetNextStateAll(model_t model,next_method_black_t method){
	model->next_all=method;
}

int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
	return model->next_all(model,src,cb,context);
}

void GBsetStateLabelsAll(model_t model,get_label_all_method_t method){
	model->state_labels_all=method;
}

void GBsetStateLabelsGroup(model_t model,get_label_group_method_t method){
	model->state_labels_group=method;
}

void GBsetStateLabelLong(model_t model,get_label_method_t method){
	model->state_labels_long=method;
}

void GBsetStateLabelShort(model_t model,get_label_method_t method){
	model->state_labels_short=method;
}

int GBgetStateLabelShort(model_t model,int label,int *state){
	return model->state_labels_short(model,label,state);
}

int GBgetStateLabelLong(model_t model,int label,int *state){
	return model->state_labels_long(model,label,state);
}

void GBgetStateLabelsGroup(model_t model,sl_group_enum_t group,int*state,int*labels){
	model->state_labels_group(model,group,state,labels);
}

void GBgetStateLabelsAll(model_t model,int*state,int*labels){
	model->state_labels_all(model,state,labels);
}

sl_group_t* GBgetStateLabelGroupInfo(model_t model, sl_group_enum_t group) {
    return model->sl_groups[group];
}

void GBsetStateLabelGroupInfo(model_t model, sl_group_enum_t group, sl_group_t* group_info)
{
    model->sl_groups[group] = group_info;
}

int GBhasGuardsInfo(model_t model) { return model->guards != NULL; }

void GBsetGuardsInfo(model_t model, guard_t** guards) {
    model->guards = guards;
}

guard_t** GBgetGuardsInfo(model_t model) {
    return model->guards;
}

void GBsetGuard(model_t model, int group, guard_t* guard) {
    HREassert (guard->count > 0, "group must have at least one guard");
    model->guards[group] = guard;
}

guard_t* GBgetGuard(model_t model, int group) {
    return model->guards[group];
}

void GBsetGuardCoEnabledInfo(model_t model, matrix_t *info) {
    HREassert (model->gce_info == NULL, "guard may be co-enabled info already set");
    model->gce_info = info;
}

void GBsetDoNotAccordInfo(model_t model, matrix_t *info) {
    HREassert (model->dna_info == NULL, "do-not-accord info already set");
    model->dna_info = info;
}

void GBsetCommutesInfo(model_t model, matrix_t *info) {
    HREassert (model->commutes_info == NULL, "transition commutes info already set");
    model->commutes_info = info;
}

void GBsetPorGroupVisibility(model_t model, int*visibility) {
    HREassert (model->group_visibility == NULL, "POR group visibility already set");
    model->group_visibility = visibility;
}

int *GBgetPorGroupVisibility(model_t model) {
    return model->group_visibility;
}

void GBsetPorStateLabelVisibility(model_t model, int*visibility) {
    HREassert (model->label_visibility == NULL, "POR label visibility already set");
    model->label_visibility = visibility;
}

int *GBgetPorStateLabelVisibility(model_t model) {
    return model->label_visibility;
}

matrix_t *GBgetGuardCoEnabledInfo(model_t model) {
    return model->gce_info;
}

matrix_t *GBgetDoNotAccordInfo(model_t model) {
    return model->dna_info;
}

matrix_t *GBgetCommutesInfo(model_t model) {
    return model->commutes_info;
}

void GBsetGuardNESInfo(model_t model, matrix_t *info) {
    HREassert (model->gnes_info == NULL, "guard NES info already set");
    model->gnes_info = info;
}

matrix_t *GBgetGuardNESInfo(model_t model) {
    return model->gnes_info;
}

void GBsetGuardNDSInfo(model_t model, matrix_t *info) {
    HREassert (model->gnds_info == NULL, "guard NDS info already set");
    model->gnds_info = info;
}

matrix_t *GBgetGuardNDSInfo(model_t model) {
    return model->gnds_info;
}

void GBsetGroupsOfEdge(model_t model,groups_of_edge_t method){
    model->groups_of_edge=method;
}

int GBgroupsOfEdge(model_t model, int edgeno, int index, int** groups){
    return model->groups_of_edge(model,edgeno,index,groups);
}

void GBsetChunkMap(model_t model, table_factory_t factory){
	model->chunk_factory = factory;
}

void GBcopyChunkMaps(model_t dst, model_t src)
/* XXX This method should be removed when factoring out the chunk
 * business from the PINS interface.  If src->map is replaced after
 * copying, bad things are likely to happen when dst is used.
 */
{
    dst->chunk_factory = src->chunk_factory;

    int N    = lts_type_get_type_count(GBgetLTStype(src));
    dst->map = RTmallocZero(N*sizeof(void*));
    for(int i = 0; i < N; i++) {
        HREassert(src->map != NULL, "Map not correctly initialized, make sure to call GBsetLTStype, before using chunk mapping.");
        dst->map[i] = src->map[i];
    }
}

void GBgrowChunkMaps(model_t model, int old_n)
{
    void **old_map = model->map;
    int N = lts_type_get_type_count(GBgetLTStype(model));
    model->map = RTmallocZero(N*sizeof(void*));
    for(int i=0; i < N; i++) {
        HREassert(old_map != NULL, "Map not correctly initialized, make sure to call GBsetLTStype, before using chunk mapping.");
        if (i < old_n) {
            model->map[i] = old_map[i];
        } else {
            model->map[i] = TFnewTable (model->chunk_factory);
        }
    }
    RTfree (old_map);
}

void GBsetPrettyPrint(model_t model, chunk2pretty_t chunk2pretty) {
    model->chunk2pretty = chunk2pretty;
}

int GBchunkPrettyPrint(model_t model, int pos, int chunk_no){
    if (model->chunk2pretty == NULL) {
        if (model->parent == NULL) return chunk_no;
        return GBchunkPrettyPrint(model->parent, pos, chunk_no);
    }
    return model->chunk2pretty(model, pos, chunk_no);
}

void GBprintDependencyMatrix(FILE* file, model_t model) {
    Printf (info, "\nDependency matrix (combined read/write):\n");
    dm_print(file, GBgetDMInfo(model));
}

void GBprintDependencyMatrixRead(FILE* file, model_t model) {
    Printf (info, "\nRead dependencies:\n");
    dm_print(file, GBgetDMInfoRead(model));
}

void GBprintDependencyMatrixMayWrite(FILE* file, model_t model) {
    Printf (info, "\nMay-write dependencies:\n");
    dm_print(file, GBgetDMInfoMayWrite(model));
}

void GBprintDependencyMatrixMustWrite(FILE* file, model_t model) {
    Printf (info, "\nMust-write dependencies:\n");
    dm_print(file, GBgetDMInfoMustWrite(model));
}

void GBprintDependencyMatrixCombined(FILE* file, model_t model) {
    matrix_t *dm_r = GBgetDMInfoRead(model);
    matrix_t *dm_may_w = GBgetDMInfoMayWrite(model);
    matrix_t *dm_must_w = GBgetDMInfoMustWrite(model);

    Printf (info, "\nRead/write dependencies:\n");
    dm_print_combined(file, dm_r, dm_may_w, dm_must_w);
}

void GBprintPORMatrix(FILE* file, model_t model) {
    if (GBgetDoNotAccordInfo(model) != NULL) {
        Printf (info, "\nDo Not Accord matrix:\n");
        dm_print(file, GBgetDoNotAccordInfo(model));
    }

    if (GBgetCommutesInfo(model) != NULL) {
        Printf (info, "\nCommutes matrix:\n");
        dm_print(file, GBgetCommutesInfo(model));
    }

    if (GBgetGuardCoEnabledInfo(model) != NULL) {
        Printf (info, "\nMaybe coenabled matrix:\n");
        dm_print(file, GBgetGuardCoEnabledInfo(model));
    }

    if (GBgetGuardNESInfo(model) != NULL) {
        Printf (info, "\nNecessary enabling matrix:\n");
        dm_print(file, GBgetGuardNESInfo(model));
    }

    if (GBgetGuardNDSInfo(model) != NULL) {
        Printf (info, "\nNecessary disabling matrix:\n");
        dm_print(file, GBgetGuardNDSInfo(model));
    }

    for (int i = 0; i < GBgetMatrixCount(model); i++) {
        matrix_t *m = GBgetMatrix(model, i);
        const char *name = GBgetMatrixName(model, i);
        //index_class_t k = GBgetMatrixRowInfo(model, i);
        //index_class_t n = GBgetMatrixColumnInfo(model, i);
        pins_strictness_t s = GBgetMatrixStrictness(model, i);
        char *S = (s == PINS_MAY_CLEAR ? "may_clear" :
                   (s == PINS_MAY_SET ? "may_set" : "strict"));
        Printf (info, "\n%s : %s (%d X %d):\n", name, S, dm_nrows(m), dm_ncols(m));
        dm_print(file, m);
    }
}

void GBprintStateLabelInfo(FILE* file, model_t model) {
    Printf (info, "\nState labeling dependencies:\n");
    dm_print(file, GBgetStateLabelInfo(model));
}

void GBprintStateLabelGroupInfo(FILE* file, model_t model) {
    if (GBhasGuardsInfo(model))
    {
        int nGroups = dm_nrows (GBgetDMInfo (model));
        Printf(info, "State label group info:\n");
        for (int i = 0; i < nGroups; i++) {
            guard_t* guards = GBgetGuard (model, i);
            fprintf (file, "%d (%d): ", i, guards->count);
            for (int j = 0; j < guards->count; j++) {
                fprintf (file, "%d,", guards->guard[j]);
            }
            fprintf (file, "\n");
        }
    }
}


/**********************************************************************
 * Grey box factory functionality
 */

#define                 MAX_TYPES 16
static char*            model_type[MAX_TYPES];
static pins_loader_t    model_loader[MAX_TYPES];
static int              registered=0;
static char            *model_type_pre[MAX_TYPES];
static pins_loader_t    model_preloader[MAX_TYPES];
static int              registered_pre=0;
static int              matrix=0;
static int              labels=0;

void chunk_table_print(log_t log, model_t model) {
    lts_type_t t = GBgetLTStype(model);
    HREprintf(log,"The registered types values are:\n");
    int N=lts_type_get_type_count(t);
    int idx = 0;
    for(int i=0;i<N;i++){
        char *type = lts_type_get_type(t, i);
        table_iterator_t it = pins_chunk_iterator  (model, i);
        while (IThasNext(it)) {
            chunk c = ITnext (it);
            char name[c.len*2+6];
            chunk2string(c, sizeof name, name);
            HREprintf(log,"%4d: %s (%s)\n",idx, name, type);
            idx++;
        }
    }
}

static model_t
wrapModel(model_t model)
{
    /* add fork layer */
    model = GBaddFork (model);

    /* add GBlong guard evaluation layer (Deprecated) */
    model = GBaddGuards (model);

    /* add dependency checking layer */
    model = GBaddCheck (model);

    /* add partial order reduction */
    model = GBaddPOR (model);

    /* add LTL crossproduct layer */
    model = GBaddLTL (model);

    /* add regrouping */
    model = GBregroup (model);

    /* add mu calculus */
    model = GBaddMucalc (model); // Only adds LTL when a mu formula is provided

    /* add cache */
    model = GBaddCache (model);

    /* if 'print matrix', print matrix and abort */
    if (matrix) {
        if (HREme(HREglobal()) == 0) {
            /* if we are the main process, then print */
            GBprintDependencyMatrixCombined(stdout, model);
            if (log_active(infoLong)) {
                GBprintStateLabelInfo(stdout, model);
                GBprintStateLabelGroupInfo(stdout, model);
                GBprintPORMatrix(stdout, model);
            }
            /* synchronize with other processes */
            HREbarrier(HREglobal());
        } else {
            /* wait for main process */
            HREbarrier(HREglobal());
        }
        HREabort(LTSMIN_EXIT_SUCCESS);
    }

    /* if 'print labels', print and abort */
    if (labels) {
        if (HREme(HREglobal()) == 0) {
            /* if we are the main process, then print */
            if (log_active(info)) {
                lts_type_printf(info, GBgetLTStype(model));
            }
            chunk_table_print(info, model);
            /* synchronize with other processes */
            HREbarrier(HREglobal());
        } else {
            /* wait for main process */
            HREbarrier(HREglobal());
        }
        HREabort(LTSMIN_EXIT_SUCCESS);
    }

    return model;
}

static pins_loader_t
find_loader (const char* filename)
{
    char* extension = strrchr (filename, '.');
    if (extension) {
        extension++;
        for (int i = 0; i < registered; i++) {
            if (0 == strcmp (model_type[i], extension)) {
                return model_loader[i];
            }
        }
        Abort("No factory method has been registered for %s models", extension);
    } else {
        Abort("filename %s doesn't have an extension", filename);
    }
}

void
GBloadFile(model_t model, const char *filename, model_t *wrapped)
{
    pins_loader_t   model_loader = find_loader (filename);
    model_loader (model, filename);
    *wrapped = wrapModel(model);
}

void
GBloadFileShared (model_t model, const char *filename)
{
    char               *extension = strrchr (filename, '.');
    if (extension) {
        extension++;
        for (int i = 0; i < registered_pre; i++) {
            if (0==strcmp (model_type_pre[i], extension)) {
                model_preloader[i] (model, filename);
                return;
            }
        }
    } else {
        Abort("filename %s doesn't have an extension", filename);
    }
}

void GBregisterLoader(const char*extension,pins_loader_t loader){
	if (registered<MAX_TYPES){
		model_type[registered]=strdup(extension);
		model_loader[registered]=loader;
		registered++;
	} else {
		Abort("model type registry overflow");
	}
}

void GBregisterPreLoader(const char*extension,pins_loader_t loader){
    if (registered_pre<MAX_TYPES){
        model_type_pre[registered_pre]=strdup(extension);
        model_preloader[registered_pre]=loader;
        registered_pre++;
    } else {
        Abort("model type registry overflow");
    }
}

struct poptOption greybox_options[]={
    { "labels", 0, POPT_ARG_VAL, &labels, 1, "print state variable and type names, and state and action labels", NULL },
	{ "matrix" , 'm' , POPT_ARG_VAL , &matrix , 1 , "print the dependency matrix for the model and exit" , NULL},
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, mucalc_options, 0 , NULL, NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, cache_options, 0 , NULL, NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, guards_options, 0 , NULL, NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, check_options, 0 , NULL, NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, por_options , 0 , "Partial Order Reduction options", NULL },
    { NULL, 0 , POPT_ARG_INCLUDE_TABLE, group_options, 0 , "Regrouping options", NULL },
	POPT_TABLEEND
};

value_table_t
GBgetChunkMap(model_t model,int type_no)
{
    HREassert(model->map != NULL, "Map not correctly initialized, make sure to call GBsetLTStype, before using chunk mapping.");
	return model->map[type_no];
}

void GBsetDefaultFilter(model_t model,string_set_t filter){
    model->default_filter=filter;
}

string_set_t GBgetDefaultFilter(model_t model){
    return model->default_filter;
}

void ltsmin_abort(int code) {
    HREabort (code);
}

void GBsetExit(model_t model, ExitCB exit)
{
    model->exit = exit;
}

void GBExit(model_t model)
{
    if (model->exit != NULL) model->exit(model);
}

void GBsetVarPerm(model_t model, int* perm) {
    model->var_perm = perm;
}

int* GBgetVarPerm(model_t model) {
    return model->var_perm;
}

void GBsetGroupPerm(model_t model, int* perm) {
    model->group_perm = perm;
}

int* GBgetGroupPerm(model_t model) {
    return model->group_perm;
}
