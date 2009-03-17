#include <popt.h>
#include <archive.h>
#include <stdio.h>
#include <runtime.h>
#include <amconfig.h>
#ifdef HAVE_BCG_USER_H
#include <bcg_user.h>
#endif

#include <lts_enum.h>
#include <lts_io.h>
#include <greybox.h>
#include <stringindex.h>

#include <inttypes.h>

static int segments=0;

static  struct poptOption options[] = {
	{ "segments" , 0 , POPT_ARG_INT , &segments , 0 , "Set the number of segments in the output. (default: same as input)" , "<count>" },
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
	if (segments==lts_input_segments(input)){
		lts_output_t output=lts_output_open_root(files[1],model,segments,0,1,
			lts_root_segment(input),lts_root_offset(input));
		lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
		lts_input_enum(input,segments,segments,segments,ecb);
		lts_output_end(output,ecb);
		lts_output_close(&output);
	} else {
		int N=lts_input_segments(input);
		uint64_t offset[N+1];
		lts_count_t *count=lts_input_count(input);
		offset[0]=segments;
		offset[1]=0;
		for(int i=1;i<N;i++){
			offset[i+1]=offset[i]+count->state[i-1];
		}
		uint64_t root=offset[lts_root_segment(input)+1]+lts_root_offset(input);
		lts_output_t output=lts_output_open_root(files[1],model,segments,0,1,root%segments,root/segments);
		lts_enum_cb_t ecb=lts_output_begin(output,segments,segments,segments);
		lts_input_enum(input,N,N,N,lts_enum_convert(ecb,offset,convert_div_mod,copy_seg_ofs,1));
		lts_output_end(output,ecb);
		lts_output_close(&output);
	}
	lts_input_close(&input);
	return 0;
}


