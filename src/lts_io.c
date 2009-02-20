#include "config.h"
#include <stdlib.h>
#include <string.h>

#include <struct_io.h>
#include <lts_io.h>
#include <runtime.h>
#include <archive.h>
#include <dir_ops.h>
#include <inttypes.h>
#include "amconfig.h"
#ifdef HAVE_BCG_USER_H
#include "bcg_user.h"
#endif
#include <lts_count.h>

static int plain=0;
static int decode=0;
static int blocksize=32768;
static int blockcount=32;
static int bcg_write_used=0;

static struct option io_options[]={
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"The IO subsystem detects the following file formats:",
		"*.dir : uncompressed DIR format in a directory",
		"*.gcf : DIR format using a GCF archive",
#ifdef HAVE_BCG_USER_H
		"*.bcg : BCG file format"},
#else
		"*.bcg : BCG file format (get CADP and/or recompile to enable)"},
#endif
	{"",OPT_NORMAL,NULL,NULL,NULL,
		"*%s*  : DIR format using pattern substitution",
		NULL,NULL,NULL},
	{"-plain",OPT_NORMAL,set_int,&plain,NULL,
		"disable compression of the output",
		"applies to .gcf",NULL,NULL},
	{"-bs",OPT_REQ_ARG,parse_int,&blocksize,"-bs <block size>",
		"Set the block size to be used for streams.",
		"This is also used as the GCF block size.",
		NULL,NULL},
	{"-bc",OPT_REQ_ARG,parse_int,&blockcount,"-bc <block count>",
		"Set the number of blocks in one GCF cluster.",
		NULL,NULL,NULL},
	{"-io-help",OPT_NORMAL,usage,NULL,NULL,
		"print this help message",NULL,NULL,NULL},
	{0,0,0,0,0,0,0,0,0}
};

void lts_io_init(int *argcp,char*argv[]){
#ifdef HAVE_BCG_USER_H
	BCG_INIT();
#endif
	take_options(io_options,argcp,argv);
}

struct lts_output_struct{
	archive_t archive;
	model_t model;
	char *plain_code;
	char *write_mode;
	char *info_mode;

	lts_count_t count;

	uint32_t segment_count;

	stream_t **src_ofs;
	stream_t **lbl_one;
	stream_t **dst_ofs;

	int owned;
	int workers;
	int bcg;
	int typeno;
};

static int has_extension(const char* str,const char* ext){
	int str_len=strlen(str);
	int ext_len=strlen(ext);
	if (ext_len>str_len) return 0;
	return !strcmp(str+(str_len-ext_len),ext);
}

lts_output_t lts_output_open(
	char *outputname,
	model_t model,
	int segment_count,
	int state_segment,
	int src_segment,
	int dst_segment
){
	char *write_mode=prop_get_S("write_mode","iii");
	char *info_mode=prop_get_S("info_mode","dir");
	lts_output_t out=(lts_output_t)RTmalloc(sizeof(struct lts_output_struct));
	bzero(out,sizeof(struct lts_output_struct));
	out->workers=segment_count;
	if (state_segment<segment_count) {
		out->owned=state_segment;
	} else if (src_segment<segment_count) {
		out->owned=src_segment;
	} else if (dst_segment<segment_count) {
		out->owned=dst_segment;
	} else {
		out->owned=0;
		out->workers=1;
	}
	Warning(info,"worker %d of %d for %s",out->owned,out->workers,outputname);
	int flags;
	int which;
	if (out->workers<segment_count || segment_count==1) {
		which=segment_count; // own everything
		flags=LTS_COUNT_ALL;
	} else {
		which=out->owned; // own one segment
		flags=LTS_COUNT_STATE;
		if (src_segment==segment_count) {
			flags=flags|LTS_COUNT_IN|LTS_COUNT_CROSS_IN;
		} else if (dst_segment==segment_count) {
			flags=flags|LTS_COUNT_OUT|LTS_COUNT_CROSS_OUT;
		} else {
			Fatal(1,error,"neither all in nor all our owned");
		}
	}
	lts_count_init(&(out->count),flags,which,segment_count);
	out->model=model;
	out->write_mode=write_mode;
	out->info_mode=info_mode;
	out->segment_count=segment_count;
	lts_type_t ltstype=GBgetLTStype(model);
	if (has_extension(outputname,".bcg")){
#ifdef HAVE_BCG_USER_H
		if (bcg_write_used){
			Fatal(1,error,"cannot write more than one BCG file at once.");
		}
		bcg_write_used=1;
		out->bcg=1;
		out->typeno=lts_type_get_edge_label_typeno(ltstype,0);
#else
		Fatal(1,error,"BCG support disabled");
#endif
	}
	if (has_extension(outputname,".dir")&&strcmp(info_mode,"dir")){
		Fatal(1,error,".dir is restricted to dir type info files");
	}
	if (!strcmp(info_mode,"dir") || out->bcg){
		if (lts_type_get_state_label_count(ltstype)){
			Warning(info,"The state labels will not be written to file.");
		}
		switch(lts_type_get_edge_label_count(ltstype)){
		case 0:
			Fatal(1,error,"Cannot encode the absense of any label.");
			break;
		case 1:
			break;
		default:
			Warning(info,"Only recording the first edge label.");
			break;
		}
	}
	out->archive=NULL;
	if (has_extension(outputname,".dir")){
		if ((segment_count>1)&&(state_segment<segment_count||src_segment<segment_count||dst_segment<segment_count)){
			Fatal(1,error,"Parallel directory creation is unsafe.");
		}
		plain=1;
		out->archive=arch_dir_create(outputname,blocksize,DELETE_ALL);
	} else if (has_extension(outputname,".gcf")){
		out->archive=arch_gcf_create(raf_unistd(outputname),blocksize,blocksize*blockcount,out->owned,out->workers);
		out->plain_code="";		
	} else if (strstr(outputname,"%s")){
		out->archive=arch_fmt(outputname,file_input,file_output,blocksize);
#ifdef HAVE_BCG_USER_H
	} else if (has_extension(outputname,".bcg")){
		Warning(info,"assuming initial state is state 0");
		if (segment_count==1){
			BCG_IO_WRITE_BCG_BEGIN (outputname,0,1,get_label(),0);
		} else {
			if(strstr(outputname,"%d")){
				char fname[1024];
				if (src_segment<segment_count && dst_segment==segment_count) {
					sprintf(fname,outputname,src_segment);
				} else if (src_segment==segment_count && dst_segment<segment_count){
					sprintf(fname,outputname,dst_segment);
				} else {
					Fatal(1,error,"improperly partitioned output for PBG");
				}
				BCG_IO_WRITE_BCG_BEGIN (fname,0,1,get_label(),0);
			} else {
				Fatal(1,error,"When using BCG in a partitioned setting, the file name must contain %%d");
			}
		}
#endif
	} else {
		Fatal(1,error,"Failed to open %s",outputname);
	}
	if (!strcmp(info_mode,"dir") && out->archive){
		Warning(info,"opening streams");
		out->src_ofs=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
		out->lbl_one=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
		out->dst_ofs=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
		bzero(out->src_ofs,segment_count*sizeof(stream_t*));
		bzero(out->lbl_one,segment_count*sizeof(stream_t*));
		bzero(out->dst_ofs,segment_count*sizeof(stream_t*));
		int i_from=(src_segment==segment_count)?0:src_segment;
		int i_to=(src_segment==segment_count)?segment_count:(src_segment+1);
		for(int i=i_from;i<i_to;i++){
			out->src_ofs[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
			out->lbl_one[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
			out->dst_ofs[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
			bzero(out->src_ofs[i],segment_count*sizeof(stream_t));
			bzero(out->lbl_one[i],segment_count*sizeof(stream_t));
			bzero(out->dst_ofs[i],segment_count*sizeof(stream_t));
			int j_from=(dst_segment==segment_count)?0:dst_segment;
			int j_to=(dst_segment==segment_count)?segment_count:(dst_segment+1);
			for(int j=j_from;j<j_to;j++){
				Warning(info,"opening for %d %d",i,j);
				char fname[128];
				sprintf(fname,"src-%d-%d",i,j);
				out->src_ofs[i][j]=arch_write(out->archive,fname,plain?out->plain_code:"diff32|gzip",1);
				sprintf(fname,"label-%d-%d",i,j);
				out->lbl_one[i][j]=arch_write(out->archive,fname,plain?out->plain_code:"gzip",1);
				sprintf(fname,"dest-%d-%d",i,j);
				out->dst_ofs[i][j]=arch_write(out->archive,fname,plain?out->plain_code:"diff32|gzip",1);
			}
		}
	}
	return out;
}


/*
		if (strstr(outputarch,"%s")) {
			arch=arch_fmt(outputarch,file_input,file_output,prop_get_U32("bs",65536));
		} else {
			uint32_t bs=prop_get_U32("bs",65536);
			uint32_t bc=prop_get_U32("bc",128);
			arch=arch_gcf_create(raf_unistd(outputarch),bs,bs*bc,0,1);
		}
		char* state_names[state_labels+1];
		for(int i=0;i<state_labels;i++) {
			state_names[i]=lts_type_get_state_label_name(ltstype,i);
			Warning(info,"state label %d is %s",i,state_names[i]);
		}
		char* edge_names[edge_labels+1];
		for(int i=0;i<edge_labels;i++) {
			edge_names[i]=lts_type_get_edge_label_name(ltstype,i);
			Warning(info,"edge label %d is %s",i,edge_names[i]);
		}
		output=lts_output_open(arch,0,1,use_vset?N:0,state_labels,state_names,
				0,0,use_vset?N:1,edge_labels,edge_names);
		if (use_vset) {
			output_handle=lts_enum_base(N,
				NULL,NULL,NULL,
				output,lts_write_vec,NULL,
				NULL,lts_write_seg_vec_cb,NULL);
		} else {
			output_handle=lts_enum_base(N,
				NULL,NULL,NULL,
				output,NULL,lts_write_seg,
				NULL,NULL,lts_write_seg_seg_cb);
		}


lts_output_t lts_output_open(
	archive_t archive,
	int segment,
	int segment_count,
	int src_len,
	int state_labels,
	char **state_label_names,
	int record_src_seg,
	int record_dst_seg,
	int dst_len,
	int edge_labels,
	char **edge_label_names
){
	lts_output_t out=(lts_output_t)RTmalloc(sizeof(struct lts_output_struct));
	out->archive=archive;

	char base[1024];

	Warning(info,"opening state files");
	sprintf(base,"SV-%d-%%d",segment);
	out->src_vec=src_len?arch_write_vec_U32(archive,base,src_len,"gzip",1):NULL;
	sprintf(base,"SL-%d-%%s",segment);
	out->state_label_vec=state_labels?arch_write_vec_U32_named(archive,base,state_labels,state_label_names,"gzip",1):NULL;

	Warning(info,"opening edge files");
	sprintf(base,"ES-%d-seg",segment);
	out->src_seg=record_src_seg?arch_write(archive,base,"gzip",1):NULL;
	sprintf(base,"ES-%d-ofs",segment);
	out->src_ofs=arch_write(archive,base,"diff32|gzip",1);
	sprintf(base,"ED-%d-seg",segment);
	out->dst_seg=record_dst_seg?arch_write(archive,base,"gzip",1):NULL;
	sprintf(base,"ED-%d-%%d",segment);
	out->dst_vec=arch_write_vec_U32(archive,base,dst_len,"gzip",1);
	sprintf(base,"EL-%d-%%s",segment);
	out->edge_label_vec=edge_labels?arch_write_vec_U32_named(archive,base,edge_labels,edge_label_names,"gzip",1):NULL;

	Warning(info,"everything opened");
	return out;
}
*/

/* Replaced by lts_enum callbacks
void lts_write_state(lts_output_t out,int *src,int* state_labels){
	if (out->src_vec) DSwriteStruct(out->src_vec,src);
	if (out->state_label_vec) DSwriteStruct(out->state_label_vec,state_labels);
}

void lts_write_edge(lts_output_t out,int src_seg,int src_ofs,int dst_seg,int* dst,int* edge_labels){
	if (out->src_seg) DSwriteU32(out->src_seg,src_seg);
	DSwriteU32(out->src_ofs,src_ofs);
	if (out->dst_seg) DSwriteU32(out->dst_seg,dst_seg);
	DSwriteStruct(out->dst_vec,dst);
	if (out->edge_label_vec) DSwriteStruct(out->edge_label_vec,edge_labels);
}
*/

lts_count_t *lts_output_count(lts_output_t out){
	return &(out->count);
}

static inline void close_stream_matrix(stream_t **mat,int N){
	if (mat) {
		for(int i=0;i<N;i++){
			if(mat[i]) {
				for(int j=0;j<N;j++){
					if (mat[i][j]) DSclose(&(mat[i][j]));
				}
				free(mat[i]);				
			}
		}
		free(mat);
	}	
}

static void write_dir_info(stream_t ds,lts_count_t *count,int action_count,int tau){
	DSwriteU32(ds,31);
	DSwriteS(ds,"lts_io.c");
	DSwriteU32(ds,count->segments);
	DSwriteU32(ds,0); // Assuming root segment 0
	DSwriteU32(ds,0); // Assuming root ofs 0
	DSwriteU32(ds,action_count);
	DSwriteU32(ds,tau);
	DSwriteU32(ds,0); // This field was either top count or vector length. CHECK!
	for(int i=0;i<count->segments;i++){
		Warning(info,"output segment %d has %d states",i,(int)count->state[i]);
		DSwriteU32(ds,count->state[i]);
	}
	for(int i=0;i<count->segments;i++){
		for(int j=0;j<count->segments;j++){
			Warning(info,"output edge count from %d to %d is %d",i,j,(int)count->cross[i][j]);
			DSwriteU32(ds,count->cross[i][j]);
		}
	}
}

void lts_output_close(lts_output_t *out_p){
	lts_output_t out=*out_p;
	*out_p=NULL;

	close_stream_matrix(out->src_ofs,out->segment_count);
	close_stream_matrix(out->lbl_one,out->segment_count);
	close_stream_matrix(out->dst_ofs,out->segment_count);

#ifdef HAVE_BCG_USER_H
	if (out->bcg){
		BCG_IO_WRITE_BCG_END ();
		bcg_write_used=0;
	}
#endif

	lts_type_t ltstype=GBgetLTStype(out->model);
	if (out->owned==0 && out->archive){
		if (!strcmp(out->info_mode,"dir")){
			stream_t ds;
			ds=arch_write(out->archive,"TermDB",plain?out->plain_code:"gzip",1);
			int typeno=lts_type_get_edge_label_typeno(ltstype,0);
			int act_count=GBchunkCount(out->model,typeno);
			int tau=-1;
			for(int i=0;i<act_count;i++){
				chunk c=GBchunkGet(out->model,typeno,i);
				DSwrite(ds,c.data,c.len);
				DSwrite(ds,"\n",1);
				if (!strncmp(c.data,"tau",c.len)) {
					tau=i;
				}
			}
			DSclose(&ds);
			Warning(info,"TermDB contains %d actions",act_count);
			//lts_set_labels(lts_info,act_count);
			ds=arch_write(out->archive,"info",plain?out->plain_code:"",1);
			//lts_write_info(lts_info,ds,LTS_INFO_DIR);
			write_dir_info(ds,&(out->count),act_count,tau);
			DSclose(&ds);
		} else if (!strcmp(out->info_mode,"vec")) {
			stream_t ds;
			int type_count=lts_type_get_type_count(ltstype);
			for(int i=0;i<type_count;i++){
				char stream_name[1024];
				sprintf(stream_name,"CT-%s",lts_type_get_type(ltstype,i));
				ds=arch_write(out->archive,stream_name,plain?out->plain_code:"gzip",1);
				int element_count=GBchunkCount(out->model,i);
				Warning(info,"type %d has %d elements",i,element_count);
				for(int j=0;j<element_count;j++){
					chunk c=GBchunkGet(out->model,i,j);
					DSwriteVL(ds,c.len);
					DSwrite(ds,c.data,c.len);
				}
				DSclose(&ds);
			}
			ds=arch_write(out->archive,"info",plain?out->plain_code:"",1);
			DSwriteS(ds,"no info format for vec has been defined");
			DSclose(&ds);
		} else {
			Fatal(1,error,"unsupported info format %s",out->info_mode);
		}
	}
	if (out->archive) arch_close(&(out->archive));
	free(out);
}


/*
static void lts_write_vec(void* lts_output,int* state,int* labels){
#define out ((lts_output_t)lts_output)
	out->state_count++;
//	if (out->state_vec) DSwriteStruct(out->state_vec,state);
//	if (out->state_label_vec) DSwriteStruct(out->state_label_vec,labels);
#undef out
}
*/

static void lts_write_seg(void* lts_output,int seg,int ofs,int* labels){
#define out ((lts_output_t)lts_output)
	LTS_CHECK_STATE(out->count,seg,ofs);
//	(void)seg; //A writer is for one segment only, so we ignore segment.
//	(void)ofs; //Writing convention says that offset is equal to record number.
//	if (out->state_label_vec) DSwriteStruct(out->state_label_vec,labels);
#undef out
}

/*
static void lts_write_seg_vec(void* lts_output,int src_seg,int src_ofs,int* dst,int*labels){
#define out ((lts_output_t)lts_output)
//	if (out->src_seg) DSwriteU32(out->src_seg,src_seg);
//	DSwriteU32(out->src_ofs,src_ofs);
//	DSwriteStruct(out->dst_vec,dst);
//	if (out->edge_label_vec) DSwriteStruct(out->edge_label_vec,labels);
#undef out
}
*/


static inline void matrixU32(stream_t **mat,int i,int j,uint32_t v){
	if(mat) {
		if(mat[i]){
			if(mat[i][j]){
				DSwriteU32(mat[i][j],v);
			}
		}
	}
}

static void lts_write_seg_seg(void* lts_output,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	lts_output_t out=((lts_output_t)lts_output);
	LTS_CHECK_STATE(out->count,src_seg,src_ofs);
	LTS_CHECK_STATE(out->count,dst_seg,dst_ofs);
	LTS_INCR_CROSS(out->count,src_seg,dst_seg);
	LTS_INCR_IN(out->count,dst_seg);
	LTS_INCR_OUT(out->count,src_seg);
	matrixU32(out->src_ofs,src_seg,dst_seg,src_ofs);
	matrixU32(out->lbl_one,src_seg,dst_seg,labels[0]);
	matrixU32(out->dst_ofs,src_seg,dst_seg,dst_ofs);
#ifdef HAVE_BCG_USER_H
	if (out->bcg){
		char buffer[1024];
		chunk c=chunk_ld(1024,buffer);
		chunk_encode_copy(c,GBchunkGet(out->model,out->typeno,labels[0]),'\\');
		BCG_IO_WRITE_BCG_EDGE ((src_ofs*out->segment_count)+src_seg,strcmp(buffer,"tau")?buffer:"i",(dst_ofs*out->segment_count)+dst_seg);
	}
#endif
//	if (out->src_seg) DSwriteU32(out->src_seg,src_seg);
//	DSwriteU32(out->src_ofs,src_ofs);
//	if (out->dst_seg) DSwriteU32(out->dst_seg,dst_seg);
//	DSwriteU32(out->dst_ofs,dst_ofs);
//	if (out->edge_label_vec) DSwriteStruct(out->edge_label_vec,labels);
//	if (out->lbl)  DSwriteU32(out->lbl,labels[0]);
}



lts_enum_cb_t lts_output_enum(lts_output_t out){
	lts_type_t ltstype=GBgetLTStype(out->model);
	int N=lts_type_get_state_length(ltstype);
	return lts_enum_iii(N,	out,lts_write_seg,lts_write_seg_seg);
}


struct lts_input_struct {
	char *name;
	model_t model;
	int share;
	int share_count;
	int segments;
	lts_count_t count;
	archive_t archive;
	char *decode;
#ifdef HAVE_BCG_USER_H
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
#endif
};

lts_count_t *lts_input_count(lts_input_t in){
	return &(in->count);
}


static void load_headers(lts_input_t input){
	stream_t ds=arch_read(input->archive,"info",NULL);
	char description[1024];
	DSreadS(ds,description,1024);
	if (strlen(description)==0) {
		switch(DSreadU16(ds)){
		case 0:
			if (DSreadU16(ds)!=31) {
				Fatal(1,error,"cannot identify input format");
				return NULL;
			}
			Log(info,"input uses headers");
			input->decode="auto";
			break;
		case 31:
			Log(info,"input has no headers");
			break;
		default:
			Fatal(1,error,"cannot identify input format");
			return NULL;
		}
	} else {
		Log(info,"input uses headers");
		input->decode="auto";
		ds=stream_setup(ds,description);
		if (DSreadU32(ds)!=31){
			Fatal(1,error,"cannot identify input format");
		}
	}
	Warning(info,"comment is %s",DSreadSA(ds));
	input->segments=DSreadU32(ds);
	lts_count_init(&(input->count),0,input->segments,input->segments); 
	if (DSreadU32(ds)) Fatal(1,error,"root segment is not 0");
	if (DSreadU32(ds)) Fatal(1,error,"root offset is not 0");
	DSreadU32(ds); // skip label count;
	int tau=DSreadS32(ds); // ignore tau
	//if (tau>=0) SIputAt(lts->string_index,"tau",tau);
	DSreadU32(ds); // skip top count;
	for(int i=0;i<input->segments;i++){
		input->count.state[i]=DSreadU32(ds);
		Warning(info,"input segment %d has %d states",i,(int)input->count.state[i]);
	}
	for(int i=0;i<input->segments;i++){
		for(int j=0;j<input->segments;j++){
			input->count.cross[i][j]=DSreadU32(ds);
			Warning(info,"input edge count from %d to %d is %d",i,j,(int)input->count.cross[i][j]);
		}
	}
	DSclose(&ds);
	
	lts_type_t ltstype=lts_type_create();
	lts_type_set_state_length(ltstype,2);
	int action_type=lts_type_add_type(ltstype,"action",NULL);
	lts_type_set_edge_label_count(ltstype,1);
	lts_type_set_edge_label_name(ltstype,0,"action");
	lts_type_set_edge_label_type(ltstype,0,"action");
	GBsetLTStype(input->model,ltstype);

	ds=arch_read(input->archive,"TermDB",input->decode);
	for(int L=0;;L++){
		char*lbl=DSreadLN(ds);
		int len=strlen(lbl);
		if (len==0) {
			Warning(info,"read %d labels",L);
			break;
		}
		char*str;
		if (lbl[0]=='"' && lbl[len-1]=='"') {
			Warning(info,"stripping double quotes from %s",lbl);
			lbl[len-1]=0;
			str=lbl+1;
		} else {
			str=lbl;
		}
		if (GBchunkPut(input->model,action_type,chunk_str(lbl))!=L){
			Fatal(1,error,"position of label %d was not %d",L,L);
		}
		free(lbl);
	}
	DSclose(&ds);
}

lts_input_t lts_input_open(char*inputname,model_t model,int share,int share_count){
	lts_input_t input=RT_NEW(struct lts_input_struct);
	input->archive=NULL;
	if (is_a_dir(inputname)){
		input->archive=arch_dir_open(inputname,blocksize);
	} else if (has_extension(inputname,".gcf")) {
		input->archive=arch_gcf_read(raf_unistd(inputname));
#ifdef HAVE_BCG_USER_H
	} else if (has_extension(inputname,".bcg")) {
		input->segments=1;
		lts_count_init(&(input->count),0,1,1); 
		Warning(info,"%s recognized as BCG",inputname);
		if (share_count!=1) Fatal(1,error,"parallel reading not supported");
		lts_type_t ltstype=lts_type_create();
		lts_type_set_state_length(ltstype,2);
		int action_type=lts_type_add_type(ltstype,"action",NULL);
		lts_type_set_edge_label_count(ltstype,1);
		lts_type_set_edge_label_name(ltstype,0,"action");
		lts_type_set_edge_label_type(ltstype,0,"action");
		GBsetLTStype(model,ltstype);

		BCG_TYPE_C_STRING bcg_comment;
		BCG_OT_READ_BCG_BEGIN (inputname, &(input->bcg_graph), 0);
		BCG_READ_COMMENT (BCG_OT_GET_FILE (input->bcg_graph), &bcg_comment);
		Warning(info,"comment is: %s",bcg_comment);
		if(BCG_OT_INITIAL_STATE (input->bcg_graph)!=0){
			Fatal(1,error,"initial state of %s is not 0",input->name);
		}
		int label_count=BCG_OT_NB_LABELS (input->bcg_graph);
		for(int i=0;i<label_count;i++){
			char *lbl=BCG_OT_LABEL_STRING (input->bcg_graph,i);
			if (!BCG_OT_LABEL_VISIBLE (input->bcg_graph,i)){
				lbl="tau";
			}
			if (GBchunkPut(model,action_type,chunk_str(lbl))!=i){
				Fatal(1,error,"position of label %d was not %d",i,i);
			}
		}
		input->count.state[0]=BCG_OT_NB_STATES (input->bcg_graph);
		input->count.in[0]=BCG_OT_NB_EDGES (input->bcg_graph);
		input->count.out[0]=input->count.in[0];
		input->count.cross[0][0]=input->count.in[0];
#endif
	} else if (strstr(inputname,"%s")){
		input->archive=arch_fmt(inputname,file_input,file_output,blocksize);
	} else {
		Fatal(1,error,"type of input %s not recognized",inputname);
	}
	input->name=strdup(inputname);
	input->share=share;
	input->model=model;
	input->share_count=share_count;
	if (input->archive) {
		if (share_count!=1) Fatal(1,error,"parallel reading not supported yet.");
		load_headers(input);
	}
	return input;
}

int lts_input_segments(lts_input_t input){
	return input->segments;
}

void lts_input_enum(lts_input_t input,int states,int edges,lts_enum_cb_t output){
	if (input->archive) {
		if(input->share_count!=1) Fatal(1,error,"parallel reading not implemented yet!");
		int N=input->segments;
		if(edges) {
			for(int i=0;i<N;i++){
				for(int j=0;j<N;j++){
					char name[1024];
					sprintf(name,"src-%d-%d",i,j);
					stream_t src_in=arch_read(input->archive,name,input->decode);
					sprintf(name,"label-%d-%d",i,j);
					stream_t lbl_in=arch_read(input->archive,name,input->decode);
					sprintf(name,"dest-%d-%d",i,j);
					stream_t dst_in=arch_read(input->archive,name,input->decode);
					for(;;){
						if (DSempty(src_in)) break;
						uint32_t s=DSreadU32(src_in);
						uint32_t l=DSreadU32(lbl_in);
						uint32_t d=DSreadU32(dst_in);
						enum_seg_seg(output,i,s,j,d,&l);
					}
					DSclose(&src_in);
					DSclose(&lbl_in);
					DSclose(&dst_in);
				}
			}
		}
#ifdef HAVE_BCG_USER_H
	} else if (has_extension(input->name,".bcg")) {
		if (edges){
			bcg_type_state_number bcg_s1, bcg_s2;
			BCG_TYPE_LABEL_NUMBER bcg_label_number;
			BCG_OT_ITERATE_PLN (input->bcg_graph, bcg_s1, bcg_label_number, bcg_s2) {
				enum_seg_seg(output,0,bcg_s1,0,bcg_s2,&bcg_label_number);
			} BCG_OT_END_ITERATE;
		}
#endif
	} else {
		Fatal(1,error,"Cannot enumerate %s",input->name);
	}
}

void lts_input_close(lts_input_t *input_p){
	lts_input_t input=*input_p;
	*input_p=NULL;
#ifdef HAVE_BCG_USER_H
	if (has_extension(input->name,".bcg")){
		BCG_OT_READ_BCG_END (&(input->bcg_graph));	
	}
#endif
	free(input->name);
	free(input);
}




