#include <lts_enum.h>
#include <runtime.h>

struct lts_enum_struct{
	int len;
	void* cb_context;
	state_cb s_cb;
	state_vec_cb vec_cb;
	state_seg_cb seg_cb;
	edge_cb e_cb;
	edge_vec_vec_cb vec_vec_cb;
	edge_seg_vec_cb seg_vec_cb;
	edge_seg_seg_cb seg_seg_cb;
	lts_enum_cb_t base;
	void* state_context;
	state_fold_t fold;
	state_unfold_t unfold;
	int force;
};

void* enum_get_context(lts_enum_cb_t e){
	return e->cb_context;
}


void enum_state(lts_enum_cb_t sink,int seg,int* state,int* labels){
	sink->s_cb(sink->cb_context,seg,state,labels);
}

void enum_vec(lts_enum_cb_t sink,int* state,int* labels){
	sink->vec_cb(sink->cb_context,state,labels);
}

void enum_seg(lts_enum_cb_t sink,int seg,int ofs,int* labels){
	sink->seg_cb(sink->cb_context,seg,ofs,labels);
}

void enum_edge(lts_enum_cb_t sink,int src_seg,int* src,int dst_seg,int* dst,int*labels){
	sink->e_cb(sink->cb_context,src_seg,src,dst_seg,dst,labels);
}

void enum_vec_vec(lts_enum_cb_t sink,int* src,int* dst,int*labels){
	sink->vec_vec_cb(sink->cb_context,src,dst,labels);
}

void enum_seg_vec(lts_enum_cb_t sink,int src_seg,int src_ofs,int* dst,int*labels){
	sink->seg_vec_cb(sink->cb_context,src_seg,src_ofs,dst,labels);
}

void enum_seg_seg(lts_enum_cb_t sink,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	sink->seg_seg_cb(sink->cb_context,src_seg,src_ofs,dst_seg,dst_ofs,labels);
}


static void missing_vec(void*ctx,int* state,int* labels){
	(void)ctx;(void)state;(void)labels;
	Fatal(1,error,"missing callback");
}

static void missing_seg(void*ctx,int seg,int ofs,int* labels){
	(void)ctx;(void)seg;(void)ofs;(void)labels;
	Fatal(1,error,"missing callback");
}

static void missing_vec_vec(void*ctx,int* src,int* dst,int*labels){
	(void)ctx;(void)src;(void)dst;(void)labels;
	Fatal(1,error,"missing callback");
}

static void missing_seg_vec(void*ctx,int src_seg,int src_ofs,int* dst,int*labels){
	(void)ctx;(void)src_seg;(void)src_ofs;(void)dst;(void)labels;
	Fatal(1,error,"missing callback");
}

static void missing_seg_seg(void*ctx,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	(void)ctx;(void)src_seg;(void)src_ofs;(void)dst_seg;(void)dst_ofs;(void)labels;
	Fatal(1,error,"missing callback");
}

lts_enum_cb_t lts_enum_viv(int len,void* context,state_vec_cb state_cb,edge_seg_vec_cb edge_cb){
	lts_enum_cb_t output=RT_NEW(struct lts_enum_struct);
	output->len=len;
	output->cb_context=context;
	output->vec_cb=state_cb;
	output->seg_cb=missing_seg;
	output->vec_vec_cb=missing_vec_vec;
	output->seg_vec_cb=edge_cb;
	output->seg_seg_cb=missing_seg_seg;
	return output;
}

lts_enum_cb_t lts_enum_iii(int len,void* context,state_seg_cb state_cb,edge_seg_seg_cb edge_cb){
	lts_enum_cb_t output=RT_NEW(struct lts_enum_struct);
	output->len=len;
	output->cb_context=context;
	output->vec_cb=missing_vec;
	output->seg_cb=state_cb;
	output->vec_vec_cb=missing_vec_vec;
	output->seg_vec_cb=missing_seg_vec;
	output->seg_seg_cb=edge_cb;
	return output;
}

lts_enum_cb_t lts_enum_vii(int len,void* context,state_vec_cb state_cb,edge_seg_seg_cb edge_cb){
	lts_enum_cb_t output=RT_NEW(struct lts_enum_struct);
	output->len=len;
	output->cb_context=context;
	output->vec_cb=state_cb;
	output->seg_cb=missing_seg;
	output->vec_vec_cb=missing_vec_vec;
	output->seg_vec_cb=missing_seg_vec;
	output->seg_seg_cb=edge_cb;
	return output;
}

static void convert_vec(void*ctx,int* state,int* labels){
	lts_enum_cb_t output=(lts_enum_cb_t)ctx;
	int seg;
	int ofs;
	output->fold(output->state_context,state,&seg,&ofs);
	output->base->seg_cb(output->base->cb_context,seg,ofs,labels);
}

static void convert_seg(void*ctx,int seg,int ofs,int* labels){
	(void)ctx;(void)seg;(void)ofs;(void)labels;
	Fatal(1,error,"TODO 2");
}

static void convert_vec_vec(void*ctx,int* src,int* dst,int*labels){
	(void)ctx;(void)src;(void)dst;(void)labels;
	Fatal(1,error,"TODO 3");
}

static void convert_seg_vec(void*ctx,int src_seg,int src_ofs,int* dst,int*labels){
	lts_enum_cb_t output=(lts_enum_cb_t)ctx;
	int src[output->len];
	int dst_seg;
	int dst_ofs;
	output->unfold(output->state_context,src_seg,src_ofs,src);
	output->fold(output->state_context,src,&src_seg,&src_ofs);
	output->fold(output->state_context,dst,&dst_seg,&dst_ofs);
	output->base->seg_seg_cb(output->base->cb_context,src_seg,src_ofs,dst_seg,dst_ofs,labels);
}

static void convert_seg_seg(void*ctx,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	lts_enum_cb_t output=(lts_enum_cb_t)ctx;
	int src[output->len];
	int dst[output->len];
	output->unfold(output->state_context,src_seg,src_ofs,src);
	output->unfold(output->state_context,dst_seg,dst_ofs,dst);
	output->fold(output->state_context,src,&src_seg,&src_ofs);
	output->fold(output->state_context,dst,&dst_seg,&dst_ofs);
	output->base->seg_seg_cb(output->base->cb_context,src_seg,src_ofs,dst_seg,dst_ofs,labels);
}

lts_enum_cb_t lts_enum_convert(lts_enum_cb_t base,void*context,state_fold_t fold,state_unfold_t unfold,int idx_convert){
	lts_enum_cb_t output=RT_NEW(struct lts_enum_struct);
	output->len=base->len;
	output->cb_context=output;
	output->vec_cb=convert_vec;
	output->seg_cb=convert_seg;;
	output->vec_vec_cb=convert_vec_vec;
	output->seg_vec_cb=convert_seg_vec;
	output->seg_seg_cb=convert_seg_seg;
	output->base=base;
	output->state_context=context;
	output->fold=fold;
	output->unfold=unfold;
	output->force=idx_convert;
	return output;
}




