#include "config.h"
#include <stdlib.h>
#include <string.h>

#include <struct_io.h>
#include <lts_io_internal.h>
#include <runtime.h>
#include <archive.h>
#include <dir_ops.h>
#include <inttypes.h>
#include "amconfig.h"
#include <lts_count.h>


#ifdef HAVE_BCG_USER_H
#include <bcg_user.h>
extern struct poptOption bcg_io_options[];
#endif

extern struct poptOption archive_io_options[];

static struct poptOption dummy_options[]={
	POPT_TABLEEND
};

struct poptOption lts_io_options[]= {
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , dummy_options , 0 , "This system support the following file formats:"
	"\n * Directory format (*.dir, *.dz and *.gcf)"
	#ifdef HAVE_BCG_USER_H
	"\n * Binary Coded Graphs (*.bcg)"
	#endif
	,NULL},
	#ifdef HAVE_BCG_USER_H
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , bcg_io_options , 0 , NULL , NULL }, 
	#endif
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , archive_io_options , 0 ,
"The .dir format uses multiple files in a directory, the .dz format uses\n"
"compressed files in a directory and the .gcf format folds multiple files\n"
"into a single archive with optional compression.\n"
"\n"
"Container I/O options",NULL},
	POPT_TABLEEND
};

lts_enum_cb_t lts_output_begin(lts_output_t out,int which_state,int which_src,int which_dst){
	return out->ops.write_begin(out,which_state,which_src,which_dst);
}

void lts_output_end(lts_output_t out,lts_enum_cb_t e){
	out->ops.write_end(out,e);
}

lts_count_t *lts_input_count(lts_input_t in){
	return &(in->count);
}

lts_count_t *lts_output_count(lts_output_t out){
	return &(out->count);
}

void lts_output_set_root_vec(lts_output_t output,uint32_t * root){
	int N=lts_type_get_state_length(GBgetLTStype(output->model));
	output->root_vec=RTmalloc(N*sizeof(uint32_t));
	for(int i=0;i<N;i++){
		output->root_vec[i]=root[i];
	}
}

void lts_output_set_root_idx(lts_output_t output,uint32_t root_seg,uint32_t root_ofs){
	output->root_seg=root_seg;
	output->root_ofs=root_ofs;
}

uint32_t lts_root_segment(lts_input_t input){
	return input->root_seg;
}
uint32_t lts_root_offset(lts_input_t input){
	return input->root_ofs;
}

void lts_output_close(lts_output_t *out_p){
	lts_output_t out=*out_p;
	*out_p=NULL;
	out->ops.write_close(out);
	lts_count_fini(&(out->count));
	free(out->name);
	free(out);
}

uint32_t* lts_input_root(lts_input_t input){
	return input->root_vec;
}

char* lts_input_mode(lts_input_t input){
	return input->mode;
}

int lts_input_segments(lts_input_t input){
	return input->segment_count;
}

void lts_input_enum(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	input->ops.read_part(input,which_state,which_src,which_dst,output);
}

void lts_input_close(lts_input_t *input_p){
	lts_input_t input=*input_p;
	*input_p=NULL;
	free(input->name);
	free(input);
}




#define MAX_TYPES 16
static char* write_type[MAX_TYPES];
static lts_write_open_t write_open[MAX_TYPES];
static int write_registered=0;

void lts_write_register(char*extension,lts_write_open_t open){
	if (write_registered<MAX_TYPES){
		write_type[write_registered]=extension;
		write_open[write_registered]=open;
		write_registered++;
	} else {
		Fatal(1,error,"write type registry overflow");
	}
}

lts_output_t lts_output_open(
	char *outputname,
	model_t model,
	int segment_count,
	int share,
	int share_count,
	const char *requested_mode,
	char **actual_mode
){
	Warning(info,"opening %s",outputname);
	char* extension=strrchr(outputname,'.');
	if (extension) {
		extension++;
		for(int i=0;i<write_registered;i++){
			if(!strcmp(write_type[i],extension)){
				lts_output_t output=RT_NEW(struct lts_output_struct);
				output->name=strdup(outputname);
				output->mode=strdup(requested_mode);
				output->model=model;
				output->share=share;
				output->share_count=share_count;
				output->segment_count=segment_count;
				output->root_seg=segment_count;
				lts_count_init(&(output->count),LTS_COUNT_STATE,(share_count==segment_count)?share:segment_count,segment_count);
				write_open[i](output);
				if(actual_mode){
					*actual_mode=output->mode;
				}
				if (strcmp(output->mode,requested_mode)){
					Warning(info,"providing mode %s instead of %s",output->mode,requested_mode);
					if (!actual_mode) {
						Fatal(1,error,"required write mode unsupported");
					}
				}
				return output;
			}
		}
		Fatal(1,error,"No factory method has been registered for %s files",extension);
	} else {
		Fatal(1,error,"filename %s doesn't have an extension",outputname);
	}
	return NULL;
}


static char* read_type[MAX_TYPES];
static lts_read_open_t read_open[MAX_TYPES];
static int read_registered=0;

void lts_read_register(char*extension,lts_read_open_t open){
	if (read_registered<MAX_TYPES){
		read_type[read_registered]=extension;
		read_open[read_registered]=open;
		read_registered++;
	} else {
		Fatal(1,error,"read type registry overflow");
	}
}

lts_input_t lts_input_open(char*inputname,model_t model,int share,int share_count){
	char* extension=strrchr(inputname,'.');
	if (extension) {
		extension++;
		for(int i=0;i<read_registered;i++){
			if(!strcmp(read_type[i],extension)){
				lts_input_t input=RT_NEW(struct lts_input_struct);
				input->name=strdup(inputname);
				input->model=model;
				input->share=share;
				input->share_count=share_count;
				read_open[i](input);
				return input;
			}
		}
		Fatal(1,error,"No factory method has been registered for %s files",extension);
	} else {
		Fatal(1,error,"filename %s doesn't have an extension",inputname);
	}
	return NULL;
}







