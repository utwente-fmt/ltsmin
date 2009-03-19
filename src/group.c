#include <stdio.h>
#include "greybox.h"
#include "aterm2.h"
#include "runtime.h"

// this part should be encapsulated in greybox.h

#include "dynamic-array.h"
#include "lts-type.h"
extern int default_short(model_t self,int group,int*src,TransitionCB cb,void*context);
extern int default_all(model_t self,int*src,TransitionCB cb,void*context);

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
	array_manager_t map_manager;
};

 // Here is the real start...

typedef struct group_context {
  model_t parent;
  int len;
  int* transbegin;
  int* transmap;
  int* statemap;
  TransitionCB cb;
  void* user_context;
} *group_context_t;

static void group_cb(void* context, int* labels, int*olddst) {
  group_context_t ctx = (group_context_t)(context);
  int len=ctx->len;
  int newdst[len];
  for (int i=0; i<len; i++)
    newdst[i]=olddst[ctx->statemap[i]];
  ctx->cb(ctx->user_context,labels,newdst);
}

static int group_long(model_t self,int group,int*newsrc,TransitionCB cb,void*user_context) {
  group_context_t ctx = (group_context_t)self->context;
  model_t parent=ctx->parent;
  int len=ctx->len;
  int oldsrc[len];
  int Ntrans=0;
  int begin=ctx->transbegin[group];
  int end=ctx->transbegin[group+1];
  ctx->cb = cb;
  ctx->user_context = user_context;
  
  for (int i=0; i<len; i++)
    oldsrc[ctx->statemap[i]]=newsrc[i];

  for (int j=begin;j<end;j++) {
    int g = ctx->transmap[j];
    Ntrans += GBgetTransitionsLong(parent,g,oldsrc,group_cb,ctx);
  }
  return Ntrans;
}

model_t GBregroup(model_t model,char* group_spec){
  FILE* fp = fopen(group_spec,"r");
  if (!fp) Fatal(-1,error,"Group specification file not found: %s",group_spec);
  
  model_t group=(model_t)RTmalloc(sizeof(struct grey_box_model));
  memcpy(group,model,sizeof(struct grey_box_model));
  struct group_context *ctx=(struct group_context *)RTmalloc(sizeof(struct group_context));

  ctx->parent=model;
  GBsetContext(group,ctx);

  group->next_long = group_long;
  group->next_short= default_short;
  group->next_all  = default_all;
  group->s0 = NULL; // redefined later
  
  // not supported yet (should permute states)
  group->s_info = NULL;
  group->state_labels_short = NULL;
  group->state_labels_long = NULL;
  group->state_labels_all = NULL;
  
  // fill statemapping: assumption this is a bijection
  { 
  ATermList statemapping = (ATermList)ATreadFromFile(fp);
  int Nparts = ATgetLength(statemapping);
  if (Nparts!=lts_type_get_state_length(GBgetLTStype(model)))
    Fatal(-1,error,"state mapping in file doesn't match the specification");
  ctx->len = Nparts;
  ctx->statemap=(int*)RTmalloc(Nparts*sizeof(int));
  for (int i=0;i<Nparts;i++) {
    ATerm first=ATgetFirst(statemapping);
    int s = ATgetInt((ATermInt)ATgetFirst((ATermList)ATgetArgument(first,1)));
    ctx->statemap[i]=s;
    statemapping = ATgetNext(statemapping);
  }
  }
  
  // fill transition mapping: assumption: this is a surjection
  {
  ATermList transmapping = (ATermList)ATreadFromFile(fp);
  int oldNgroups = GBgetEdgeInfo(model)->groups;
  int newNgroups = ATgetLength(transmapping);
  Warning(info,"Regrouping: %d->%d groups",oldNgroups,newNgroups);
  ctx->transbegin=(int*)RTmalloc((1+newNgroups)*sizeof(int));
  ctx->transmap=(int*)RTmalloc(oldNgroups*sizeof(int));
  int p=0;
  for (int i=0;i<newNgroups;i++) {
    ATerm first=ATgetFirst(transmapping);
    ATermList tail = (ATermList)ATgetArgument(first,1);
    int n = ATgetLength(tail);
    ctx->transbegin[i]=p;
    for (int j=0;j<n;j++) {
      ctx->transmap[p+j]=ATgetInt((ATermInt)ATgetFirst(tail));
      tail = ATgetNext(tail);
    }
    p = p+n;
    transmapping = ATgetNext(transmapping);
  }
  ctx->transbegin[newNgroups]=p;
  }

  //  fill edge_info
  {
  ATermList dependencies = (ATermList)ATreadFromFile(fp);
  int newNgroups = ATgetLength(dependencies);
  edge_info_t e_info =  (edge_info_t)RTmalloc(sizeof(struct edge_info));
  e_info->groups = newNgroups;
  e_info->length = (int*)RTmalloc(newNgroups*sizeof(int));
  e_info->indices = (int**)RTmalloc(newNgroups*sizeof(int*));

  for (int i=0;i<newNgroups;i++) {
    ATermList deps=(ATermList)ATgetArgument(ATgetFirst(dependencies),1);
    int n = ATgetLength(deps);
    e_info->length[i]=n;
    e_info->indices[i]=(int*)RTmalloc(n*sizeof(int));
    for (int j=0;j<n;j++) {
      int d = ATgetInt((ATermInt)ATgetFirst(deps));
      e_info->indices[i][j] = d;
      deps = ATgetNext(deps);
    }
    dependencies = ATgetNext(dependencies);
  }
  GBsetEdgeInfo(group,e_info);
  }

  // permute initial state
  {
  int len=ctx->len;
  int* news0 = (int*)RTmalloc(len*sizeof(int));
  for (int i=0;i<len;i++)
    news0[i] = (model->s0)[ctx->statemap[i]];
  GBsetInitialState(group,news0);
  }

  return group;
}
