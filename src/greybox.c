#include <stdlib.h>

#include "greybox.h"
#include "runtime.h"
#include "treedbs.h"

struct grey_box_model {
	lts_type_t ltstype;
	edge_info_t e_info;
	state_info_t s_info;
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
	int len;
	int *indices;
	int s_len;
	int *src;
	TransitionCB cb;
	void* user_ctx;
};

void project_dest(void*context,int*labels,int*dst){
#define info ((struct nested_cb*)context)
	int short_dst[info->len];
	for(int i=0;i<info->len;i++){
		short_dst[i]=dst[info->indices[i]];
	}
	info->cb(info->user_ctx,labels,short_dst);
#undef info
}

int default_short(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.len=self->e_info->length[group];
	info.indices=self->e_info->indices[group];
	info.s_len=lts_type_get_state_length(self->ltstype);
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;
	int long_src[info.s_len];
	int k=0;
	//printf("group %d len %d\ndef_short",group,info.len);
	for(int i=0;i<info.s_len;i++){
		if(k<info.len && info.indices[k]==i){
			long_src[i]=src[k];
			//printf("%2d*",long_src[i]);
			k++;
		} else {
			long_src[i]=self->s0[i];
			//printf("%2d ",long_src[i]);
		}
	}
	//printf("\n");
	return self->next_long(self,group,long_src,project_dest,&info);
}

void expand_dest(void*context,int*labels,int*dst){
#define info ((struct nested_cb*)context)
	int long_dst[info->s_len];
	int k=0;
	for(int i=0;i<info->s_len;i++){
		if(k<info->len && info->indices[k]==i){
			long_dst[i]=dst[k];
			k++;
		} else {
			long_dst[i]=info->src[i];
		}
	}
	info->cb(info->user_ctx,labels,long_dst);
#undef info
}

int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.len=self->e_info->length[group];
	info.indices=self->e_info->indices[group];
	info.s_len=lts_type_get_state_length(self->ltstype);
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;
	int src_short[info.len];
	//printf("def_long");
	//for(int i=0;i<info.s_len;i++){
	//	printf("%3d",src[i]);
	//}
	//printf("\n group %d len %d",group,info.len);
	for(int i=0;i<info.len;i++){
		src_short[i]=src[info.indices[i]];
		//printf("%3d",src_short[i]);
	}
	//printf("\n");
	return self->next_short(self,group,src_short,expand_dest,&info);
}

int default_all(model_t self,int*src,TransitionCB cb,void*context){
	int res=0;
	for(int i=0;i<self->e_info->groups;i++){
		res+=self->next_long(self,i,src,cb,context);
	}
	return res;
}

static int state_default_short(model_t model,int label,int *state){
	int N=lts_type_get_state_length(model->ltstype);
	int len=model->s_info->length[label];
	int k=0;
	int long_state[N];
	int *proj=model->s_info->indices[label];
	for(int i=0;i<N;i++){
		if(k<len && proj[k]==i){
			long_state[i]=state[k];
			k++;
		} else {
			long_state[i]=model->s0[i];
		}
	}
	return model->state_labels_long(model,label,long_state);
}

static int state_default_long(model_t model,int label,int *state){
	int len=model->s_info->length[label];
	int short_state[len];
	int *proj=model->s_info->indices[label];
	for(int i=0;i<len;i++) short_state[i]=state[proj[i]];
	return model->state_labels_short(model,label,short_state);
}

static void state_default_all(model_t model,int*state,int*labels){
	for(int i=0;i<model->s_info->labels;i++) {
		labels[i]=model->state_labels_long(model,i,state);
	}
}

model_t GBcreateBase(){
	model_t model=(model_t)RTmalloc(sizeof(struct grey_box_model));
	model->ltstype=NULL;
	model->e_info=NULL;
	model->s_info=NULL;
	model->s0=NULL;
	model->context=0;
	model->next_short=default_short;
	model->next_long=default_long;
	model->next_all=default_all;
	model->state_labels_short=state_default_short;
	model->state_labels_long=state_default_long;
	model->state_labels_all=state_default_all;
	model->newmap_context=NULL;
	model->newmap=NULL;
	model->int2chunk=NULL;
	model->chunk2int=NULL;
	model->map=NULL;
	model->get_count=NULL;
	return model;
}

void GBinitModelDefaults (model_t *p_model, model_t default_src)
{
    model_t model = *p_model;
    if (model->ltstype == NULL) {
        GBcopyChunkMaps(model, default_src);
        GBsetLTStype(model, GBgetLTStype(default_src));
    }
    if (model->e_info == NULL)
        GBsetEdgeInfo(model, GBgetEdgeInfo(default_src));
    if (model->s_info == NULL)
        GBsetStateInfo(model, GBgetStateInfo(default_src));
    if (model->s0 == NULL) {
        int N = lts_type_get_state_length (GBgetLTStype (default_src));
        int s0[N];
        GBgetInitialState(default_src, s0);
        GBsetInitialState(model, s0);
    }
    if (model->context == NULL)
        GBsetContext(model, GBgetContext(default_src));

    if (model->next_short == NULL)
        GBsetNextStateShort(model, default_src->next_short);
    if (model->next_long == NULL)
        GBsetNextStateLong(model, default_src->next_long);
    if (model->next_all == NULL)
        GBsetNextStateAll(model, default_src->next_all);

    if (model->state_labels_short == NULL)
        GBsetStateLabelShort(model, default_src->state_labels_short);
    if (model->state_labels_long == NULL)
        GBsetStateLabelLong(model, default_src->state_labels_long);
    if (model->state_labels_all == NULL)
        GBsetStateLabelsAll(model, default_src->state_labels_all);

}

void* GBgetContext(model_t model){
	return model->context;
}
void GBsetContext(model_t model,void* context){
	model->context=context;
}

void GBsetLTStype(model_t model,lts_type_t info){
	if (model->ltstype != NULL)  Fatal(1,error,"ltstype already set");
	model->ltstype=info;
	int N=lts_type_get_type_count(info);
	model->map=RTmalloc(N*sizeof(void*));
	for(int i=0;i<N;i++){
		model->map[i]=model->newmap(model->newmap_context);
	}
}

lts_type_t GBgetLTStype(model_t model){
	return model->ltstype;
}

void GBsetEdgeInfo(model_t model,edge_info_t info){
	if (model->s_info != NULL)  Fatal(1,error,"edge info already set");
	model->e_info=info;
}

edge_info_t GBgetEdgeInfo(model_t model){
	return model->e_info;
}

void GBsetStateInfo(model_t model,state_info_t info){
	if (model->s_info != NULL)  Fatal(1,error,"state info already set");
	model->s_info=info;
}

state_info_t GBgetStateInfo(model_t model){
	return model->s_info;
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

int GBchunkPut(model_t model,int type_no,chunk c){
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
  edge_info_t e = GBgetEdgeInfo(model);
  int N=lts_type_get_state_length(model->ltstype);
  for (int i=0;i<e->groups;i++) {
    for (int j=0,c=0;j<N;j++) {
      if (c<e->length[i] && j==e->indices[i][c]) {
	fprintf(file,"+");
	c++;
      }
      else
	fprintf(file,"-");
    }
    fprintf(file,"\n");
  }
}

/**********************************************************************
 * Grey box factory functionality
 */

#define MAX_TYPES 16
static char* model_type[MAX_TYPES];
static pins_loader_t model_loader[MAX_TYPES];
static int registered=0;
static int cache=0;
static char *group_file=NULL;

void GBloadFile(model_t model,const char *filename,model_t *wrapped){
	char* extension=strrchr(filename,'.');
	if (extension) {
		extension++;
		for(int i=0;i<registered;i++){
			if(!strcmp(model_type[i],extension)){
				model_loader[i](model,filename);
				if (wrapped) {
				  if (group_file) model=GBregroup(model,group_file);
				  if (cache) model=GBaddCache(model);
				  *wrapped=model;
				}
				return;
			}
		}
		Fatal(1,error,"No factory method has been registered for %s models",extension);
	} else {
		Fatal(1,error,"filename %s doesn't have an extension",filename);
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
	{ "regroup" , 'r' , POPT_ARG_STRING , &group_file , 0 , "Regrouping wrapper, using group specification" ,"<file>" },
	POPT_TABLEEND	
};
