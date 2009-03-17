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

/*
static void cannot_write_open(lts_output_t output){
	Fatal(1,error,"support for writing %s not available",output->name);
}
static lts_enum_cb_t cannot_write_begin(lts_output_t output,int which_state,int which_src,int which_dst){
	(void)which_state;(void)which_src;(void)which_dst;
	Fatal(1,error,"support for writing %s not available",output->name);
	return NULL;
}
static void cannot_write_end(lts_output_t output,lts_enum_cb_t writer){
	(void)writer;
	Fatal(1,error,"support for writing %s not available",output->name);
}
static void cannot_write_close(lts_output_t output){
	Fatal(1,error,"support for writing %s not available",output->name);
}
static void cannot_read_open(lts_input_t input){
	Fatal(1,error,"support for reading %s not available",input->name);
}
static void cannot_read_part(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	(void)which_state;(void)which_src;(void)which_dst;(void)output;
	Fatal(1,error,"support for reading %s not available",input->name);
}
static void cannot_read_close(lts_input_t input){
	Fatal(1,error,"support for reading %s not available",input->name);
}
*/

#ifdef HAVE_BCG_USER_H
#include <bcg_user.h>
extern struct lts_io_ops bcg_io_ops;
#else
struct lts_io_ops bcg_io_ops={
	cannot_write_open,
	cannot_write_begin,
	cannot_write_end,
	cannot_write_close,
	cannot_read_open,
	cannot_read_part,
	cannot_read_close
};
#endif
extern struct lts_io_ops dir_io_ops;
extern struct lts_io_ops gcf_io_ops;
extern struct lts_io_ops fmt_io_ops;


#ifdef HAVE_BCG_USER_H
#include "bcg_user.h"
static void bcg_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		Fatal(1,error,"unexpected call to bcg_popt");
	case POPT_CALLBACK_REASON_POST:
		BCG_INIT();
		return;
	case POPT_CALLBACK_REASON_OPTION:
		Fatal(1,error,"unexpected call to bcg_popt");
	}
}

static struct poptOption bcg_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , bcg_popt , 0 , NULL , NULL },
	POPT_TABLEEND
};
#endif

static struct poptOption dir_options[]= {
	POPT_TABLEEND
};

static struct lts_io_ops* get_ops_for(char*name){
	char* extension=strrchr(name,'.');
	if (extension) {
		extension++;
		if (!strcmp(extension,"bcg")){
			Warning(info,"detected BCG");
			return &bcg_io_ops;
		}
		if (!strcmp(extension,"dir")){
			Warning(info,"detected DIR");
			return &dir_io_ops;
		}
		if (!strcmp(extension,"gcf")){
			Warning(info,"detected GCF");
			return &gcf_io_ops;
		}
	}
	if (strstr(name,"%s")){
		Warning(info,"detected file pattern");
		return &fmt_io_ops;
	}
	return NULL;
}



extern int plain;
extern int blocksize;
extern int blockcount;


static struct poptOption gcf_options[]= {
	{ "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "The size of a block in bytes" , "<bytes>" },
	{ "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "The number of block in a cluster" , "<blocks>"},
	{ "plain" , 0 , POPT_ARG_VAL , &plain , 1 , "Disable compression of containers" , NULL},
	POPT_TABLEEND
};

struct poptOption lts_io_options[]= {
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , dir_options , 0 , "This system support the following file formats:"
	"\n * Directory format (*.dir and *.gcf)"
	#ifdef HAVE_BCG_USER_H
	"\n * Binary Coded Graphs (*.bcg)"
	#endif
	,NULL},
	#ifdef HAVE_BCG_USER_H
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , bcg_options , 0 , NULL , NULL }, 
	#endif
	{ NULL,0 , POPT_ARG_INCLUDE_TABLE , gcf_options , 0 ,
"The .dir format uses multiple files in a directory by default. It is also\n"
"possible to put these files in a GCF archive (*.gcf).\n"
"\n"
"Container I/O options",NULL},
	POPT_TABLEEND
};

lts_output_t lts_output_open(char *outputname,model_t model,int segment_count,int share,int share_count){
	return lts_output_open_root(outputname,model,segment_count,share,share_count,0,0);
}

lts_output_t lts_output_open_root(char *outputname,model_t model,int segment_count,int share,int share_count,
		uint32_t root_seg,uint64_t root_ofs){
	lts_output_t output=RT_NEW(struct lts_output_struct);
	output->name=strdup(outputname);
	output->model=model;
	output->share=share;
	output->share_count=share_count;
	output->segment_count=segment_count;
	output->root_seg=root_seg;
	output->root_ofs=root_ofs;
	lts_count_init(&(output->count),LTS_COUNT_STATE,(share_count==segment_count)?share:segment_count,segment_count);
	output->ops=get_ops_for(outputname);
	if (output->ops){
		output->ops->write_open(output);
		return output;
	} else {
		Fatal(1,error,"Could not detect file format of %s",outputname);
		return NULL;
	}
}

lts_enum_cb_t lts_output_begin(lts_output_t out,int which_state,int which_src,int which_dst){
	return out->ops->write_begin(out,which_state,which_src,which_dst);
}

void lts_output_end(lts_output_t out,lts_enum_cb_t e){
	out->ops->write_end(out,e);
}

lts_count_t *lts_input_count(lts_input_t in){
	return &(in->count);
}

lts_count_t *lts_output_count(lts_output_t out){
	return &(out->count);
}

uint32_t lts_root_segment(lts_input_t input){
	return input->root_seg;
}
uint64_t lts_root_offset(lts_input_t input){
	return input->root_ofs;
}

void lts_output_close(lts_output_t *out_p){
	lts_output_t out=*out_p;
	*out_p=NULL;
	out->ops->write_close(out);
	lts_count_fini(&(out->count));
	free(out->name);
	free(out);
}

lts_input_t lts_input_open(char*inputname,model_t model,int share,int share_count){
	lts_input_t input=RT_NEW(struct lts_input_struct);
	input->name=strdup(inputname);
	input->model=model;
	input->share=share;
	input->share_count=share_count;
	input->ops=get_ops_for(inputname);
	if (input->ops){
		input->ops->read_open(input);
		return input;
	} else {
		Fatal(1,error,"Could not detect file format of %s",inputname);
		return NULL;
	}
}

int lts_input_segments(lts_input_t input){
	return input->segment_count;
}

void lts_input_enum(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	input->ops->read_part(input,which_state,which_src,which_dst,output);
}

void lts_input_close(lts_input_t *input_p){
	lts_input_t input=*input_p;
	*input_p=NULL;
	free(input->name);
	free(input);
}

