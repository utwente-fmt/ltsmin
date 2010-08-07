#include <config.h>
#include <stdlib.h>
#include <string.h>

#include <struct_io.h>
#include <lts_io_internal.h>
#include <runtime.h>
#include <archive.h>
#include <dir_ops.h>
#include <inttypes.h>
#include <lts_count.h>

int IO_PLAIN=0;
int IO_BLOCKSIZE=32768;
int IO_BLOCKCOUNT=32;

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

int lts_input_chmod(lts_input_t input,const char *mode){
	if (!input->mode && (!strcmp(mode,"-si") || !strcmp(mode,"-is"))) {
		input->mode=strdup(mode);
		return 1;
	} else {
		return 0;
	}
}

int lts_input_segments(lts_input_t input){
	return input->segment_count;
}

void lts_input_enum_all(lts_input_t input,int flags,lts_enum_cb_t output){
	Warning(debug,"input mode is %s, output mode is %s",input->mode,enum_get_mode(output));
	for(int i=0;i<input->segment_count;i++){
		input->ops.read_part(input,i,flags,output);
	}
}

void lts_input_enum_part(lts_input_t input,int part,int flags,lts_enum_cb_t output){
	Warning(debug,"input mode is %s, output mode is %s",input->mode,enum_get_mode(output));
	input->ops.read_part(input,part,flags,output);
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

/**
 * @param outputname file name
 * @param model model to store
 * @return lts output info
 */
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

lts_input_t lts_input_open(
    char*inputname,
    model_t model,
    int share,
    int share_count,
	const char *requested_mode,
	char **actual_mode
){
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
				read_open[i](input,requested_mode,actual_mode);
				return input;
			}
		}
		Fatal(1,error,"No factory method has been registered for %s files",extension);
	} else {
		Fatal(1,error,"filename %s doesn't have an extension",inputname);
	}
	return NULL;
}

/*
int lts_input_vt_count(lts_input_t input){
    return lts_type_get_type_count(GBgetLTStype(input->model));
}
*/

char* lts_input_vt_type(lts_input_t input,int table){
    return lts_type_get_type(GBgetLTStype(input->model),table);
}

lts_type_t lts_input_ltstype(lts_input_t input){
    return GBgetLTStype(input->model);
}


void lts_input_vt_set(lts_input_t input,int table,value_table_t vt){
    if (!input->value_table) {
        int N=lts_type_get_type_count(GBgetLTStype(input->model));
        RangeCheckInt(table,0,N-1);
        input->value_table=RTmallocZero(N*sizeof(value_table_t));
    }
    input->value_table[table]=vt;
}

void lts_input_state_table_set(lts_input_t input,int segment,matrix_table_t mt){
    RangeCheckInt(segment,0,input->segment_count-1);
    if (!input->state_table) {
        input->state_table=RTmallocZero(input->segment_count*sizeof(matrix_table_t));
    }
    input->state_table[segment]=mt;
}

void lts_input_edge_table_set(lts_input_t input,int segment,matrix_table_t mt){
    RangeCheckInt(segment,0,input->segment_count-1);
    if (!input->edge_table) {
        input->edge_table=RTmallocZero(input->segment_count*sizeof(matrix_table_t));
    }
    input->edge_table[segment]=mt;
}

void lts_input_load(lts_input_t input){
    if (input->ops.load_lts){
        input->ops.load_lts(input);
    } else {
        Fatal(1,error,"Load method unimplemented");
    }
}
/*
    if(input->state_table){
        for(int i=0;i<input->segments;i++) if (input->state_table[i]) {
		    lts_enum_cb_t ecb=
		    lts_input_enum(input,,,,ecb);
        }
    } else {
        Warning(debug,"ignoring state information");
    }
    if(input->edge_table){
        for(int i=0;i<input->segments;i++) if (input->edge_table[i]) {
            
        }
    } else {
        Warning(debug,"ignoring edge information");
    }
    if(input->value_table) {
        Warning(debug,"loading value tables");
        int N=lts_type_get_type_count(GBgetLTStype(input->model));
        for(int i=0;i<N;i++){
            if (input->value_table[i]){
                uint32_t K=GBchunkCount(input->model,i);
                for(uint32_t j=0;j<K;j++){
                    if (j!=VTputChunk(input->value_table[i],GBchunkGet(input->model,i,j))){
                        Fatal(1,error,"value table does not follow indexed set rules");
                    }
                }
            }
        }
    } else {
        Warning(debug,"no value tables were requested");
    }
*/






