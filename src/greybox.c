#include <stdlib.h>

#include "greybox.h"
#include "runtime.h"

#define MAX_CONTEXT 1048576


struct grey_box_model {
	lts_struct_t ltstype;
	edge_info_t e_info;
	state_info_t s_info;
	int *s0;
	next_method_grey_t next_short;
	next_method_grey_t next_long;
	next_method_black_t next_all;
	uint64_t context[MAX_CONTEXT/8];
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

static int default_short(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.len=self->e_info->length[group];
	info.indices=self->e_info->indices[group];
	info.s_len=self->ltstype->state_length;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;
	int long_src[info.s_len];
	int k=0;
	for(int i=0;i<info.s_len;i++){
		if(info.indices[k]==i){
			long_src[i]=src[k];
			k++;
		} else {
			long_src[i]=self->s0[i];
		}
	}
	return self->next_long(self,group,long_src,project_dest,&info);
}

void expand_dest(void*context,int*labels,int*dst){
#define info ((struct nested_cb*)context)
	int long_dst[info->s_len];
	int k=0;
	for(int i=0;i<info->s_len;i++){
		if(info->indices[k]==i){
			long_dst[i]=dst[k];
			k++;
		} else {
			long_dst[i]=info->src[i];
		}
	}
	info->cb(info->user_ctx,labels,long_dst);
#undef info
}

static int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.len=self->e_info->length[group];
	info.indices=self->e_info->indices[group];
	info.s_len=self->ltstype->state_length;
	info.src=src;
	info.cb=cb;
	info.user_ctx=context;
	int src_short[info.len];
	for(int i=0;i<info.len;i++){
		src_short[i]=src[info.indices[i]];
	}
	return self->next_short(self,group,src_short,expand_dest,&info);
}

static int default_all(model_t self,int*src,TransitionCB cb,void*context){
	int res=0;
	for(int i=0;i<self->e_info->groups;i++){
		res+=self->next_long(self,i,src,cb,context);
	}
	return res;
}

model_t GBcreateBase(int context_size){
	int unused=((MAX_CONTEXT - context_size)%8)*8;
	model_t model=(model_t)RTmalloc(sizeof(struct grey_box_model)-unused);
	model->ltstype=NULL;
	model->e_info=NULL;
	model->s_info=NULL;
	model->s0=NULL;
	model->next_short=default_short;
	model->next_long=default_long;
	model->next_all=default_all;
}

void* GBgetContext(model_t model){
	return &(model->context);
}

void GBsetLTStype(model_t model,lts_struct_t info){
	if (model->ltstype != NULL)  Fatal(1,error,"ltstype already set");
	model->ltstype=info;
}

lts_struct_t GBgetLTStype(model_t model){
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
	if (model->s0 !=NULL) Fatal(1,error,"initial state already set");
	if (model->ltstype==NULL) Fatal(1,error,"must set ltstype before setting initial state");
	model->s0=(int*)RTmalloc(model->ltstype->state_length * sizeof(int));
	for(int i=0;i<model->ltstype->state_length;i++){
		model->s0[i]=state[i];
	}
}

void GBgetInitialState(model_t model,int *state){
	for(int i=0;i<model->ltstype->state_length;i++){
		state[i]=model->s0[i];
	}
}

void GBsetNextStateShort(model_t model,next_method_grey_t method){
	model->next_short=method;
}

int GBgetTransitionsShort(model_t model,int group,int*src,TransitionCB cb,void*context){
	model->next_short(model,group,src,cb,context);
}

void GBsetNextStateLong(model_t model,next_method_grey_t method){
	model->next_long=method;
}

int GBgetTransitionsLong(model_t model,int group,int*src,TransitionCB cb,void*context){
	model->next_long(model,group,src,cb,context);
}


void GBsetNextStateAll(model_t model,next_method_black_t method){
	model->next_all=method;
}

int GBgetTransitionsAll(model_t model,int*src,TransitionCB cb,void*context){
	return model->next_all(model,src,cb,context);
}

