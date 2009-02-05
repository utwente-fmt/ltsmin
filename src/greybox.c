#include <stdlib.h>

#include "greybox.h"
#include "runtime.h"
#include "treedbs.h"
#include "dynamic-array.h"
#include "stringindex.h"

struct grey_box_model {
	lts_struct_t ltstype;
	edge_info_t e_info;
	state_info_t s_info;
	int *s0;
	void*context;
	next_method_grey_t next_short;
	next_method_grey_t next_long;
	next_method_black_t next_all;
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

static int default_long(model_t self,int group,int*src,TransitionCB cb,void*context){
	struct nested_cb info;
	info.len=self->e_info->length[group];
	info.indices=self->e_info->indices[group];
	info.s_len=self->ltstype->state_length;
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

static int default_all(model_t self,int*src,TransitionCB cb,void*context){
	int res=0;
	for(int i=0;i<self->e_info->groups;i++){
		res+=self->next_long(self,i,src,cb,context);
	}
	return res;
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
	model->newmap_context=NULL;
	model->newmap=NULL;
	model->int2chunk=NULL;
	model->chunk2int=NULL;
	model->map=NULL;
	model->get_count=NULL;
	return model;
}

struct group_cache {
	int len;
	string_index_t idx;
	//treedbs_t dbs;
	int explored;
	int visited;
	array_manager_t begin_man;
	int *begin;
	array_manager_t dest_man;
	int *label;
	int *dest;
//	int len;
//	TransitionCB cb;
//	void*user_context;
};

struct cache_context {
	model_t parent;
	struct group_cache *cache;
};

static void add_cache_entry(void*context,int*label,int*dst){
	struct group_cache *ctx=(struct group_cache *)context;
	//Warning(info,"adding entry %d",ctx->begin[ctx->explored]);
	//int dst_index=TreeFold(ctx->dbs,dst);
	int dst_index=SIputC(ctx->idx,(char*)dst,ctx->len);
	if (dst_index>=ctx->visited) ctx->visited=dst_index+1;
	ensure_access(ctx->dest_man,ctx->begin[ctx->explored]);
	ctx->label[ctx->begin[ctx->explored]]=label[0];
	ctx->dest[ctx->begin[ctx->explored]]=dst_index;
	ctx->begin[ctx->explored]++;
}
/*
static void check_cache(void*context,int*label,int*dst){
	struct group_cache *ctx=(struct group_cache *)context;
	int dst_index=TreeFold(ctx->dbs,dst);
	Warning(info,"==%d=>%d",label[0],dst_index);
	for(int i=0;i<ctx->len;i++) printf("%3d",dst[i]);
	printf("\n");
	ctx->cb(ctx->user_context,label,dst);
}
*/

static int cached_short(model_t self,int group,int*src,TransitionCB cb,void*user_context){
	//Warning(info,"enum for group %d",group);
	model_t parent=((struct cache_context *)(self->context))->parent;
	struct group_cache *ctx=&(((struct cache_context *)self->context)->cache[group]);
	int len=parent->e_info->length[group];
	int tmp[len];
	//int src_idx=TreeFold(ctx->dbs,src);
	int src_idx=SIputC(ctx->idx,(char*)src,ctx->len);
	if (src_idx==ctx->visited){
		//Warning(info,"exploring in group %d len=%d",group,len);
		ctx->visited++;
		while(ctx->explored<ctx->visited){
			//TreeUnfold(ctx->dbs,ctx->explored,tmp);
			memcpy(tmp,SIgetC(ctx->idx,ctx->explored,NULL),ctx->len);
			ctx->explored++;
			ensure_access(ctx->begin_man,ctx->explored);
			ctx->begin[ctx->explored]=ctx->begin[ctx->explored-1];
			GBgetTransitionsShort(parent,group,tmp,add_cache_entry,ctx);
		}
		//Warning(info,"exploration of group %d finished",group);
	}
	//Warning(info,"callbacks %d to %d",ctx->begin[src_idx],ctx->begin[src_idx+1]);
	for(int i=ctx->begin[src_idx];i<ctx->begin[src_idx+1];i++){
		//Warning(info,"calling");
		//Warning(info,"[%d] --%d->%d",group,ctx->label[i],ctx->dest[i]);
		//TreeUnfold(ctx->dbs,ctx->dest[i],tmp);
		memcpy(tmp,SIgetC(ctx->idx,ctx->dest[i],NULL),ctx->len);
		//for(int i=0;i<len;i++) printf("%3d",tmp[i]);
		//printf("\n");
		cb(user_context,&(ctx->label[i]),tmp);
	}
	//ctx->len=len;
	//ctx->cb=cb;
	//ctx->user_context=user_context;
	//return GBgetTransitionsShort(parent,group,src,check_cache,ctx);
	//Warning(info,"callbacks done");
	return (ctx->begin[src_idx+1]-ctx->begin[src_idx]);
}

model_t GBaddCache(model_t model){
	model_t cached=(model_t)RTmalloc(sizeof(struct grey_box_model));
	memcpy(cached,model,sizeof(struct grey_box_model));
	struct cache_context *ctx=(struct cache_context *)RTmalloc(sizeof(struct cache_context));
	int N=model->e_info->groups;
	ctx->parent=model;
	ctx->cache=(struct group_cache*)RTmalloc(N*sizeof(struct group_cache));
	for(int i=0;i<N;i++){
		int len=model->e_info->length[i];
		Warning(info,"group %d/%d depends on %d variables",i,N,len);
		ctx->cache[i].len=len*sizeof(int);
		ctx->cache[i].idx=SIcreate();
		//ctx->cache[i].dbs=TreeDBScreate(model->e_info->length[i]);
		ctx->cache[i].explored=0;
		ctx->cache[i].visited=0;
		ctx->cache[i].begin_man=create_manager(256);
		ctx->cache[i].begin=NULL;
		ADD_ARRAY(ctx->cache[i].begin_man,ctx->cache[i].begin,int);
		ctx->cache[i].begin[0]=0;
		ctx->cache[i].dest_man=create_manager(256);
		ctx->cache[i].label=NULL;
		ADD_ARRAY(ctx->cache[i].dest_man,ctx->cache[i].label,int);
		ctx->cache[i].dest=NULL;
		ADD_ARRAY(ctx->cache[i].dest_man,ctx->cache[i].dest,int);
	}
	cached->context=ctx;
	cached->next_short=cached_short;
	cached->next_long=default_long;
	cached->next_all=default_all;
	return cached;
}




void* GBgetContext(model_t model){
	return model->context;
}
void GBsetContext(model_t model,void* context){
	model->context=context;
}

void GBsetLTStype(model_t model,lts_struct_t info){
	if (model->ltstype != NULL)  Fatal(1,error,"ltstype already set");
	model->ltstype=info;
	model->map=(void**)RTmalloc(info->type_count*sizeof(void*));
	for(int i=0;i<info->type_count;i++){
		model->map[i]=model->newmap(model->newmap_context);
	}
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

void GBsetChunkMethods(model_t model,newmap_t newmap,void*newmap_context,
	int2chunk_t int2chunk,chunk2int_t chunk2int,get_count_t get_count){
	model->newmap_context=newmap_context;
	model->newmap=newmap;
	model->int2chunk=int2chunk;
	model->chunk2int=chunk2int;
	model->get_count=get_count;
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
  int N=model->ltstype->state_length;
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
