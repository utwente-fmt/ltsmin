#include <config.h>
#include <assert.h>
#include <inttypes.h>
#include <popt.h>
#include <stdio.h>

#include <greybox.h>
#include <hre/user.h>
#include <hre-io/archive.h>
#include <lts_enum.h>
#include <lts-io/user.h>
#include <stringindex.h>
#include <treedbs.h>

static int segments=0;
static model_t model=NULL;

static  struct poptOption options[] = {
	{ "segments" , 0 , POPT_ARG_INT , &segments , 0 , "set the number of segments in the output. (default: same as input)" , "<count>" },
	POPT_TABLEEND
};

/*
copied from spec2lts-grey.c
 */
static void *
new_string_index (void *context)
{
    (void)context;
    return SIcreate ();
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

static void convert_vector_input(lts_file_t input,char *output_name){
	if (segments!=1) Fatal(1,error,"cannot write more than one segment");
	lts_type_t ltstype=GBgetLTStype(model);
	int N=lts_type_get_state_length(ltstype);
	Warning(info,"state length is %d",N);
	treedbs_t dbs=TreeDBScreate(N);
	// we should convert the GB not and not hack the ltstype.
	lts_type_set_state_label_count(ltstype,0);
	lts_file_t output=lts_file_open(output_name,model,segments,0,1,"-ii",NULL);
	lts_output_set_root_idx(output,0,0);
	lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
	N=lts_file_get_segments(input);
	uint32_t begin[N];
	begin[0]=0;
	for(int i=1;i<N;i++) begin[i]=begin[i-1]+lts_get_state_count(input,i);
	struct dbs_ctx ctx;
	ctx.dbs=dbs;
	ctx.begin=begin;
	lts_enum_cb_t ecb_wrapped=lts_enum_convert(ecb,&ctx,dbs_fold,dbs_unfold,1);
	Warning(debug,"pass 1: states");
	lts_input_enum_all(input,LTS_ENUM_STATES,ecb_wrapped);
	Warning(debug,"pass 2: edges");
	lts_input_enum_all(input,LTS_ENUM_EDGES,ecb_wrapped);
	lts_output_end(output,ecb);
	lts_output_close(&output);
	assert (1 == lts_get_init_count(input)); // TODO
	uint32_t root;
	int root_seg;
	lts_read_init(input, &root_seg, &root);
	uint32_t root_no=TreeFold(dbs,(int*)&root);
	if (root_no!=0){
		Fatal(1,error,"root is %u rather than 0",root_no);
	}
}

static void convert_input(lts_file_t input,char *output_name){
	int N=lts_input_segments(input);
	uint64_t offset[N+1];
	lts_count_t *count=lts_input_count(input);
	offset[0]=segments;
	offset[1]=0;
	for(int i=1;i<N;i++){
		offset[i+1]=offset[i]+count->state[i-1];
	}
	uint64_t root=offset[lts_root_segment(input)+1]+lts_root_offset(input);
	lts_output_t output=lts_output_open(output_name,model,segments,0,1,"-ii",NULL);
	lts_output_set_root_idx(output,root%segments,root/segments);
	lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
	lts_enum_cb_t ecb_wrapped=lts_enum_convert(ecb,offset,convert_div_mod,copy_seg_ofs,1);
	Warning(debug,"pass 1: states");
	lts_input_enum_all(input,LTS_ENUM_STATES,ecb_wrapped);
	Warning(debug,"pass 2: edges");
	lts_input_enum_all(input,LTS_ENUM_EDGES,ecb_wrapped);
	lts_output_end(output,ecb);
	lts_output_close(&output);
}

static void copy_input(lts_input_t input,char *output_name){
	lts_output_t output=lts_output_open(output_name,model,segments,0,1,"-ii",NULL);
	lts_output_set_root_idx(output,lts_root_segment(input),lts_root_offset(input));
	lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
	Warning(debug,"pass 1: states");
	lts_input_enum_all(input,LTS_ENUM_STATES,ecb);
	Warning(debug,"pass 2: edges");
	lts_input_enum_all(input,LTS_ENUM_EDGES,ecb);
	lts_output_end(output,ecb);
	lts_output_close(&output);
}


int main(int argc, char *argv[]){
	char* files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Stream based file format conversion\n\nOptions");
    lts_lib_setup();
    HREinitStart(&argc,&argv,1,2,files,"<input> <output>");
	model=GBcreateBase();
	GBsetChunkMethods(model,new_string_index,NULL,
		(int2chunk_t)SIgetC,(chunk2int_t)SIputC,(get_count_t)SIgetCount);
	Warning(info,"copying %s to %s",files[0],files[1]);
	char*input_mode;
	lts_input_t input=lts_input_open(files[0],model,0,1,NULL,&input_mode);
	if (segments==0) {
		segments=lts_input_segments(input);
	}
	if (input_mode){
    	Warning(debug,"input mode is %s",input_mode);
	} else {
	    Fatal(1,error,"bad lts input module: input mode undefined!");
	}
	if (!strcmp(input_mode,"viv") || !strcmp(input_mode,"vsi")){
	    convert_vector_input(input,files[1]);
	} else if ((input_mode[0]=='-') &&
	           (input_mode[1]=='s' || input_mode[1]=='i') &&
	           (input_mode[2]=='s' || input_mode[2]=='i')
    ){
        if (segments==lts_input_segments(input)) {
            copy_input(input,files[1]);
        } else {
            convert_input(input,files[1]);
        }
	} else {
		Warning(info,"input mode %s not supported",input_mode);
	}
	lts_input_close(&input);
	return 0;
}


