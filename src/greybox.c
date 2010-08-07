#include <config.h>
#include <stdlib.h>

#include "greybox.h"
#include "runtime.h"
#include "treedbs.h"
#include "dm/dm.h"

struct grey_box_model {
	model_t parent;
	lts_type_t ltstype;
	matrix_t *dm_info;
	matrix_t *dm_read_info;
	matrix_t *dm_write_info;
	matrix_t *sl_info;
	int *s0;
	void*context;
	next_method_grey_t next_short;
	next_method_grey_t next_long;
	next_method_black_t next_all;
	get_label_method_t state_labels_short;
	get_label_method_t state_labels_long;
	get_label_all_method_t state_labels_all;
	void* newmap_context;
	newmap_t newmap;
	int2chunk_t int2chunk;
	chunk2int_t chunk2int;
	get_count_t get_count;
	void** map;
};

struct nested_cb {
	model_t model;
	int group;
	int *src;
	TransitionCB cb;
	void* user_ctx;
};

void project_dest(void*context,int*labels,int*dst){
#define info ((struct nested_cb*)context)
	int len = dm_ones_in_row(GBgetDMInfo(info->model), info->group);
	int short_dst[len];
	dm_project_vector(GBgetDMInfo(info->model), info->group, dst, short_dst);
	info->cb(info->user_ctx,labels,short_dst);
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

void expand_dest(void*context,int*labels,int*dst){
#define info ((struct nested_cb*)context)
	int long_dst[dm_ncols(GBgetDMInfo(info->model))];
	dm_expand_vector(GBgetDMInfo(info->model), info->group, info->src, dst, long_dst);
	info->cb(info->user_ctx,labels,long_dst);
#undef info
}

int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.model = self;
	info.group = group;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;

	int len = dm_ones_in_row(GBgetDMInfo(self), group);
	int src_short[len];
	dm_project_vector(GBgetDMInfo(self), group, src, src_short);

	return self->next_short(self,group,src_short,expand_dest,&info);
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
    int long_state[dm_nrows(sl_info)];
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
state_labels_default_all(model_t model, int *state, int *labels)
{
	for(int i=0;i<dm_nrows(GBgetStateLabelInfo(model));i++) {
		labels[i]=model->state_labels_long(model,i,state);
	}
}

int
wrapped_default_short (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsShort (GBgetParent(self), group, src, cb, context);
}

int
wrapped_default_long (model_t self,int group,int*src,TransitionCB cb,void*context)
{
    return GBgetTransitionsLong (GBgetParent(self), group, src, cb, context);
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
wrapped_state_labels_default_all(model_t model, int *state, int *labels)
{
    return GBgetStateLabelsAll(GBgetParent(model), state, labels);
}


model_t GBcreateBase(){
	model_t model=(model_t)RTmalloc(sizeof(struct grey_box_model));
        model->parent=NULL;
	model->ltstype=NULL;
	model->dm_info=NULL;
	model->dm_read_info=NULL;
	model->dm_write_info=NULL;
	model->sl_info=NULL;
	model->s0=NULL;
	model->context=0;
	model->next_short=default_short;
	model->next_long=default_long;
	model->next_all=default_all;
	model->state_labels_short=state_labels_default_short;
	model->state_labels_long=state_labels_default_long;
	model->state_labels_all=state_labels_default_all;
	model->newmap_context=NULL;
	model->newmap=NULL;
	model->int2chunk=NULL;
	model->chunk2int=NULL;
	model->map=NULL;
	model->get_count=NULL;
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
	if (model->dm_info == NULL)
		GBsetDMInfo(model, GBgetDMInfo(default_src));
	if (model->dm_read_info == NULL)
		GBsetDMInfoRead(model, GBgetDMInfoRead(default_src));
	if (model->dm_write_info == NULL)
		GBsetDMInfoWrite(model, GBgetDMInfoWrite(default_src));
    if (model->sl_info == NULL)
        GBsetStateLabelInfo(model, GBgetStateLabelInfo(default_src));
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
        model->next_long == default_long &&
        model->next_all == default_all) {
        GBsetNextStateShort (model, wrapped_default_short);
        GBsetNextStateLong (model, wrapped_default_long);
        GBsetNextStateAll (model, wrapped_default_all);
    }

    if (model->state_labels_short == state_labels_default_short &&
        model->state_labels_long == state_labels_default_long &&
        model->state_labels_all == state_labels_default_all) {
        GBsetStateLabelShort (model, wrapped_state_labels_default_short);
        GBsetStateLabelLong (model, wrapped_state_labels_default_long);
        GBsetStateLabelsAll (model, wrapped_state_labels_default_all);
    }
}

void* GBgetContext(model_t model){
	return model->context;
}
void GBsetContext(model_t model,void* context){
	model->context=context;
}

void GBsetLTStype(model_t model,lts_type_t info){
	if (model->ltstype != NULL)  Fatal(1,error,"ltstype already set");
    lts_type_validate(info);
	model->ltstype=info;
    if (model->map==NULL){
	    int N=lts_type_get_type_count(info);
	    model->map=RTmalloc(N*sizeof(void*));
	    for(int i=0;i<N;i++){
		    model->map[i]=model->newmap(model->newmap_context);
	    }
    }
}

lts_type_t GBgetLTStype(model_t model){
	return model->ltstype;
}

void GBsetDMInfo(model_t model, matrix_t *dm_info) {
	if (model->dm_info != NULL) Fatal(1, error, "dependency matrix already set");
	model->dm_info=dm_info;
}

matrix_t *GBgetDMInfo(model_t model) {
	return model->dm_info;
}

void GBsetDMInfoRead(model_t model, matrix_t *dm_info) {
	if (model->dm_read_info != NULL) Fatal(1, error, "dependency matrix already set");
	model->dm_read_info=dm_info;
}

matrix_t *GBgetDMInfoRead(model_t model) {
	if (model->dm_read_info == NULL) {
        Warning(info, "read dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
	return model->dm_read_info;
}

void GBsetDMInfoWrite(model_t model, matrix_t *dm_info) {
	if (model->dm_write_info != NULL) Fatal(1, error, "dependency matrix already set");
	model->dm_write_info=dm_info;
}

matrix_t *GBgetDMInfoWrite(model_t model) {
	if (model->dm_write_info == NULL) {
        Warning(info, "write dependency matrix not set, returning combined matrix");
        return model->dm_info;
    }
	return model->dm_write_info;
}

void GBsetStateLabelInfo(model_t model, matrix_t *info){
	if (model->sl_info != NULL)  Fatal(1,error,"state info already set");
	model->sl_info=info;
}

matrix_t *GBgetStateLabelInfo(model_t model){
	return model->sl_info;
}

void GBsetInitialState(model_t model,int *state){
	if (model->ltstype==NULL)
            Fatal(1,error,"must set ltstype before setting initial state");
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

void GBsetNextStateShort(model_t model,next_method_grey_t method){
	model->next_short=method;
}

int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
	return model->next_short(model,group,src,cb,context);
}

void GBsetNextStateLong(model_t model,next_method_grey_t method){
	model->next_long=method;
}

int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	return model->next_long(model,group,src,cb,context);
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

void GBgetStateLabelsAll(model_t model,int*state,int*labels){
	model->state_labels_all(model,state,labels);
}

void GBsetChunkMethods(model_t model,newmap_t newmap,void*newmap_context,
	int2chunk_t int2chunk,chunk2int_t chunk2int,get_count_t get_count){
	model->newmap_context=newmap_context;
	model->newmap=newmap;
	model->int2chunk=int2chunk;
	model->chunk2int=chunk2int;
	model->get_count=get_count;
}

void GBcopyChunkMaps(model_t dst, model_t src)
/* XXX This method should be removed when factoring out the chunk
 * business from the PINS interface.  If src->map is replaced after
 * copying, bad things are likely to happen when dst is used.
 */
{
    dst->newmap_context = src->newmap_context;
    dst->newmap = src->newmap;
    dst->int2chunk = src->int2chunk;
    dst->chunk2int = src->chunk2int;
    dst->get_count = src->get_count;
    dst->map = src->map;
}

int GBchunkPut(model_t model,int type_no,const chunk c){
	return model->chunk2int(model->map[type_no],c.data,c.len);
}

chunk GBchunkGet(model_t model,int type_no,int chunk_no){
	chunk_len len;
	int tmp;
	char* data=(char*)model->int2chunk(model->map[type_no],chunk_no,&tmp);
	len=(chunk_len)tmp;
	return chunk_ld(len,data);
}

int GBchunkCount(model_t model,int type_no){
	return model->get_count(model->map[type_no]);
}


void GBprintDependencyMatrix(FILE* file, model_t model) {
	dm_print(file, GBgetDMInfo(model));
}

/**********************************************************************
 * Grey box factory functionality
 */

#define MAX_TYPES 16
static char* model_type[MAX_TYPES];
static pins_loader_t model_loader[MAX_TYPES];
static int registered=0;
static int cache=0;
static const char *regroup_options = NULL;

void
GBloadFile (model_t model, const char *filename, model_t *wrapped)
{
    char               *extension = strrchr (filename, '.');
    if (extension) {
        extension++;
        for (int i = 0; i < registered; i++) {
            if (!strcmp (model_type[i], extension)) {
                model_loader[i] (model, filename);
                if (wrapped) {
                    if (regroup_options != NULL)
                        model = GBregroup (model, regroup_options);
                    if (cache)
                        model = GBaddCache (model);
                    *wrapped = model;
                }
                return;
            }
        }
        Fatal (1, error, "No factory method has been registered for %s models",
               extension);
    } else {
        Fatal (1, error, "filename %s doesn't have an extension",
               filename);
    }
}

void GBregisterLoader(const char*extension,pins_loader_t loader){
	if (registered<MAX_TYPES){
		model_type[registered]=strdup(extension);
		model_loader[registered]=loader;
		registered++;
	} else {
		Fatal(1,error,"model type registry overflow");
	}
}

struct poptOption greybox_options[]={
	{ "cache" , 'c' , POPT_ARG_VAL , &cache , 1 , "Enable caching of grey box calls." , NULL },
	{ "regroup" , 'r' , POPT_ARG_STRING, &regroup_options , 0 ,
          "Enable regrouping; available transformations T: "
          "gs, ga, gc, gr, cs, cn, cw, ca, rs, rn, ru", "<(T,)+>" },
	POPT_TABLEEND	
};
