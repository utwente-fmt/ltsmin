#include <popt.h>
#include <archive.h>
#include <stdio.h>
#include <runtime.h>
#include <amconfig.h>
#include "treedbs.h"

#include <lts_enum.h>
#include <lts_io.h>
#include <greybox.h>
#include <stringindex.h>

#include <inttypes.h>

static int segments=0;

static  struct poptOption options[] = {
	{ "segments" , 0 , POPT_ARG_INT , &segments , 0 , "set the number of segments in the output. (default: same as input)" , "<count>" },
	{ NULL, 0 , POPT_ARG_INCLUDE_TABLE, lts_io_options , 0 , NULL ,NULL},
	POPT_TABLEEND
};

/*
copied from spec2lts-grey.c
 */
static void *new_string_index(void* context){
	(void)context;
	Warning(info,"creating a new string index");
	return SIcreate();
}

static void copy_seg_ofs(void*ctx,int seg,int ofs,int*vec){
	(void)ctx;
	vec[0]=seg;
	vec[1]=ofs;
}

static void convert_div_mod(void*ctx,int *vec,int *seg,int *ofs){
	uint64_t *offset=(uint64_t *)ctx;
	uint64_t state=offset[vec[0]+1]+vec[1];
	*seg=state%offset[0];
	*ofs=state/offset[0];
}

struct dbs_ctx {
	treedbs_t dbs;
	uint32_t *begin;
};

static void dbs_unfold(void*context,int seg,int ofs,int*vec){
	struct dbs_ctx *ctx=(struct dbs_ctx *)context;
	TreeUnfold(ctx->dbs,ctx->begin[seg]+ofs,vec);
}

static void dbs_fold(void*context,int *vec,int *seg,int *ofs){
	struct dbs_ctx *ctx=(struct dbs_ctx *)context;
	*seg=0;
	*ofs=TreeFold(ctx->dbs,vec);
}

int main(int argc, char *argv[]){
	char* files[2];
	RTinitPopt(&argc,&argv,options,2,2,files,NULL,"<input> <output>","Stream based file format conversion\n\nOptions");
	model_t model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);
	Warning(info,"copying %s to %s",files[0],files[1]);
	lts_input_t input=lts_input_open(files[0],model,0,1);
	if (segments==0) {
		segments=lts_input_segments(input);
	}
	char*input_mode=lts_input_mode(input);
	if (input_mode) {
		Warning(debug,"input was written in %s mode",input_mode);
		if (!strcmp(input_mode,"viv") || !strcmp(input_mode,"vsi")){
			if (segments!=1) Fatal(1,error,"cannot write more than one segment");
			lts_type_t ltstype=GBgetLTStype(model);
			int N=lts_type_get_state_length(ltstype);
			Warning(info,"state length is %d",N);
			treedbs_t dbs=TreeDBScreate(N);
			// we should convert the GB not and not hack the ltstype.
			lts_type_set_state_label_count(ltstype,0);
			lts_output_t output=lts_output_open(files[1],model,segments,0,1,"-ii",NULL);
			lts_output_set_root_idx(output,0,0);
			lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
			// We should have two passes (if the file is not in BFS order then the state numbers are wrong!)
			// pass 1: states with labels.
			// pass 2: edges with labels.
			// Currently the viv reader illegally does two passes.
			N=lts_input_segments(input);
			uint32_t begin[N];
			lts_count_t *count=lts_input_count(input);
			begin[0]=0;
			for(int i=1;i<N;i++) begin[i]=begin[i-1]+count->state[i-1];
			struct dbs_ctx ctx;
			ctx.dbs=dbs;
			ctx.begin=begin;
			lts_input_enum(input,N,N,N,lts_enum_convert(ecb,&ctx,dbs_fold,dbs_unfold,1));
			lts_output_end(output,ecb);
			lts_output_close(&output);
			uint32_t* root=lts_input_root(input);
			uint32_t root_no=TreeFold(dbs,(int*)root);
			if (root_no!=0){
				Fatal(1,error,"root is %u rather than 0",root_no);
			}
		} else {
			Warning(info,"input mode %s not supported",input_mode);
		}
	} else if (segments==lts_input_segments(input)){
		Warning(debug,"undefined input mode");
		lts_output_t output=lts_output_open(files[1],model,segments,0,1,"-ii",NULL);
		lts_output_set_root_idx(output,lts_root_segment(input),lts_root_offset(input));
		lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
		lts_input_enum(input,segments,segments,segments,ecb);
		lts_output_end(output,ecb);
		lts_output_close(&output);
	} else {
		Warning(debug,"undefined input mode with segment conversion");
		int N=lts_input_segments(input);
		uint64_t offset[N+1];
		lts_count_t *count=lts_input_count(input);
		offset[0]=segments;
		offset[1]=0;
		for(int i=1;i<N;i++){
			offset[i+1]=offset[i]+count->state[i-1];
		}
		uint64_t root=offset[lts_root_segment(input)+1]+lts_root_offset(input);
		lts_output_t output=lts_output_open(files[1],model,segments,0,1,"-ii",NULL);
		lts_output_set_root_idx(output,root%segments,root/segments);
		lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
		lts_input_enum(input,N,N,N,lts_enum_convert(ecb,offset,convert_div_mod,copy_seg_ofs,1));
		lts_output_end(output,ecb);
		lts_output_close(&output);
	}
	lts_input_close(&input);
	return 0;
}


