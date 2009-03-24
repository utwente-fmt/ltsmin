#include <config.h>
#include <unistd.h>
#include <string.h>
#include <runtime.h>
#include <lts_io_internal.h>
#include <archive.h>
#include <dir_ops.h>
#include <struct_io.h>
#include <fifo.h>

/* Global I/O parameters */
static int plain=0;
static int blocksize=32768;
static int blockcount=32;

/* common data types for archive based formats */

struct archive_io {
	archive_t archive;
	int plain;
	char*plain_code;
	char*decode;
};

static void arch_read_close(lts_input_t input){
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	arch_close(&(ctx->archive));
}

/* DIR format I/O */
/* The DIR I/O code is capable of writing multiple partitions at once */

struct dir_output_struct {
	lts_count_t *count_p;
	stream_t **src_ofs;
	stream_t **lbl_one;
	stream_t **dst_ofs;
};

static void dir_write_state(void* lts_output,int seg,int ofs,int* labels){
	(void)lts_output;
	(void)seg;
	(void)ofs;
	(void)labels;
}

static inline void matrixU32(stream_t **mat,int i,int j,uint32_t v){
	if(mat) {
		if(mat[i]){
			if(mat[i][j]){
				DSwriteU32(mat[i][j],v);
			}
		}
	}
}

static void dir_write_edge(void* lts_output,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	struct dir_output_struct* out=(struct dir_output_struct*)lts_output;
	LTS_CHECK_STATE((*(out->count_p)),(uint32_t)src_seg,(uint32_t)src_ofs);
	LTS_CHECK_STATE((*(out->count_p)),(uint32_t)dst_seg,(uint32_t)dst_ofs);
	LTS_INCR_CROSS((*(out->count_p)),(uint32_t)src_seg,(uint32_t)dst_seg);
	matrixU32(out->src_ofs,src_seg,dst_seg,src_ofs);
	matrixU32(out->lbl_one,src_seg,dst_seg,labels[0]);
	matrixU32(out->dst_ofs,src_seg,dst_seg,dst_ofs);
}

static lts_enum_cb_t dir_write_begin(lts_output_t output,int which_state,int which_src,int which_dst){
	(void)which_state;
	struct dir_output_struct *out=RT_NEW(struct dir_output_struct);
	struct archive_io *ctx=(struct archive_io *)output->ops_context;
	lts_type_t ltstype=GBgetLTStype(output->model);
	int N=lts_type_get_state_length(ltstype);
	int segment_count=output->segment_count;

	out->count_p=&(output->count);
	out->src_ofs=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
	out->lbl_one=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
	out->dst_ofs=(stream_t**)RTmalloc(segment_count*sizeof(stream_t*));
	bzero(out->src_ofs,segment_count*sizeof(stream_t*));
	bzero(out->lbl_one,segment_count*sizeof(stream_t*));
	bzero(out->dst_ofs,segment_count*sizeof(stream_t*));
	int i_from=(which_src==segment_count)?0:which_src;
	int i_to=(which_src==segment_count)?segment_count:(which_src+1);
	for(int i=i_from;i<i_to;i++){
		out->src_ofs[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
		out->lbl_one[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
		out->dst_ofs[i]=(stream_t*)RTmalloc(segment_count*sizeof(stream_t));
		bzero(out->src_ofs[i],segment_count*sizeof(stream_t));
		bzero(out->lbl_one[i],segment_count*sizeof(stream_t));
		bzero(out->dst_ofs[i],segment_count*sizeof(stream_t));
		int j_from=(which_dst==segment_count)?0:which_dst;
		int j_to=(which_dst==segment_count)?segment_count:(which_dst+1);
		for(int j=j_from;j<j_to;j++){
			Warning(debug,"opening for %d -> %d",i,j);
			char fname[128];
			sprintf(fname,"src-%d-%d",i,j);
			out->src_ofs[i][j]=arch_write(ctx->archive,fname,ctx->plain?ctx->plain_code:"diff32|gzip",1);
			sprintf(fname,"label-%d-%d",i,j);
			out->lbl_one[i][j]=arch_write(ctx->archive,fname,ctx->plain?ctx->plain_code:"gzip",1);
			sprintf(fname,"dest-%d-%d",i,j);
			out->dst_ofs[i][j]=arch_write(ctx->archive,fname,ctx->plain?ctx->plain_code:"diff32|gzip",1);
			output->count.cross[i][j]=0;
		}
	}
	return lts_enum_iii(N,out,dir_write_state,dir_write_edge);
}

static inline void close_stream_matrix(stream_t **mat,int N){
	if (mat) {
		for(int i=0;i<N;i++){
			if(mat[i]) {
				for(int j=0;j<N;j++){
					if (mat[i][j]) {
						Warning(debug,"closing for %d -> %d",i,j);
						DSclose(&(mat[i][j]));
					}
				}
				free(mat[i]);				
			}
		}
		free(mat);
	}	
}

static void dir_write_end(lts_output_t output,lts_enum_cb_t writer){
	struct dir_output_struct *out=(struct dir_output_struct *)enum_get_context(writer);
	close_stream_matrix(out->src_ofs,output->segment_count);
	close_stream_matrix(out->lbl_one,output->segment_count);
	close_stream_matrix(out->dst_ofs,output->segment_count);
	free(out);
}

static void write_dir_info(stream_t ds,lts_count_t *count,int action_count,int tau,uint32_t root_seg,uint32_t root_ofs){
	DSwriteU32(ds,31);
	DSwriteS(ds,"lts_io.c");
	DSwriteU32(ds,count->segments);
	DSwriteU32(ds,root_seg); // Assuming root segment 0
	DSwriteU32(ds,root_ofs); // Assuming root ofs 0
	DSwriteU32(ds,action_count);
	DSwriteU32(ds,tau);
	DSwriteU32(ds,0); // This field was either top count or vector length. CHECK!
	for(int i=0;i<count->segments;i++){
		Warning(debug,"output segment %d has %d states",i,(int)count->state[i]);
		DSwriteU32(ds,count->state[i]);
	}
	for(int i=0;i<count->segments;i++){
		for(int j=0;j<count->segments;j++){
			Warning(debug,"output edge count from %d to %d is %d",i,j,(int)count->cross[i][j]);
			DSwriteU32(ds,count->cross[i][j]);
		}
	}
}

static void dir_write_close(lts_output_t output){
	struct archive_io *ctx=(struct archive_io *)output->ops_context;
	if (output->share==0){
		lts_type_t ltstype=GBgetLTStype(output->model);
		stream_t ds;
		ds=arch_write(ctx->archive,"TermDB",ctx->plain?ctx->plain_code:"gzip",1);
		int typeno=lts_type_get_edge_label_typeno(ltstype,0);
		int act_count=GBchunkCount(output->model,typeno);
		int tau=-1;
		for(int i=0;i<act_count;i++){
			chunk c=GBchunkGet(output->model,typeno,i);
			DSwrite(ds,c.data,c.len);
			DSwrite(ds,"\n",1);
			if (!strncmp(c.data,"tau",c.len)) {
				tau=i;
			}
		}
		DSclose(&ds);
		Warning(debug,"TermDB contains %d actions",act_count);
		ds=arch_write(ctx->archive,"info",ctx->plain?ctx->plain_code:"",1);
		write_dir_info(ds,&(output->count),act_count,tau,output->root_seg,output->root_ofs);
		DSclose(&ds);
	}
	arch_close(&(ctx->archive));
}


static void dir_write_open_ops(lts_output_t output){
	output->ops.write_begin=dir_write_begin;
	output->ops.write_end=dir_write_end;
	output->ops.write_close=dir_write_close;
}

static void dir_read_part(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	(void)which_state;
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	int segment_count=input->segment_count;

	int i_from=(which_src==segment_count)?0:which_src;
	int i_to=(which_src==segment_count)?segment_count:(which_src+1);
	for(int i=i_from;i<i_to;i++){
		int j_from=(which_dst==segment_count)?0:which_dst;
		int j_to=(which_dst==segment_count)?segment_count:(which_dst+1);
		for(int j=j_from;j<j_to;j++){
			char name[1024];
			sprintf(name,"src-%d-%d",i,j);
			stream_t src_in=arch_read(ctx->archive,name,ctx->decode);
			sprintf(name,"label-%d-%d",i,j);
			stream_t lbl_in=arch_read(ctx->archive,name,ctx->decode);
			sprintf(name,"dest-%d-%d",i,j);
			stream_t dst_in=arch_read(ctx->archive,name,ctx->decode);
			for(;;){
				if (DSempty(src_in)) break;
				uint32_t s=DSreadU32(src_in);
				uint32_t l=DSreadU32(lbl_in);
				uint32_t d=DSreadU32(dst_in);
				enum_seg_seg(output,i,(int)s,j,(int)d,(int*)&l);
			}
			DSclose(&src_in);
			DSclose(&lbl_in);
			DSclose(&dst_in);
		}
	}
}

/*
 * Continues reading a DIR info file after the magic number has been stripped.
 */
static void load_dir_headers(lts_input_t input,stream_t ds){
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	Warning(info,"comment is %s",DSreadSA(ds));
	input->segment_count=DSreadU32(ds);
	lts_count_init(&(input->count),0,input->segment_count,input->segment_count); 
	input->root_seg=DSreadU32(ds);
	input->root_ofs=DSreadU32(ds);
	DSreadU32(ds); // skip label count;
	DSreadS32(ds); // ignore tau
	DSreadU32(ds); // skip top count;
	for(int i=0;i<input->segment_count;i++){
		input->count.state[i]=DSreadU32(ds);
		Warning(info,"input segment %d has %d states",i,(int)input->count.state[i]);
	}
	for(int i=0;i<input->segment_count;i++){
		for(int j=0;j<input->segment_count;j++){
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
	int s0[2];
	s0[0]=input->root_seg;
	s0[1]=input->root_ofs;
	GBsetInitialState(input->model,s0);

	ds=arch_read(ctx->archive,"TermDB",ctx->decode);
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
	input->ops.read_part=dir_read_part;
	input->ops.read_close=arch_read_close;
}



/*
 * Functions related to the DIR+ format.
 *
 */

struct vec_output_struct {
	lts_count_t *count_p;
	struct_stream_t state;
	struct_stream_t s_lbl;
	struct_stream_t src;
	struct_stream_t e_lbl;
	struct_stream_t dst;
};


static void idx_write_state(void*context,int seg,int ofs,int* labels){
	struct vec_output_struct *out=(struct vec_output_struct *)context;
	LTS_CHECK_STATE((*(out->count_p)),seg,(uint32_t)ofs);
	DSwriteStruct(out->s_lbl,labels);
}

static void vec_write_state(void* context,int* state,int* labels){
	struct vec_output_struct *out=(struct vec_output_struct *)context;
	// TODO State count might be wrong in case of deadlocks.
	DSwriteStruct(out->state,state);
	DSwriteStruct(out->s_lbl,labels);
}
static void iv_write_edge(void* context,int src_seg,int src_ofs,int* dst,int*labels){
	struct vec_output_struct *out=(struct vec_output_struct *)context;
	LTS_CHECK_STATE((*(out->count_p)),src_seg,(uint32_t)src_ofs);
	LTS_INCR_OUT((*(out->count_p)),(uint32_t)src_seg);
	DSwriteStruct(out->src,&src_ofs);
	DSwriteStruct(out->e_lbl,labels);
	DSwriteStruct(out->dst,dst);
}
static void si_write_edge(void*context,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	struct vec_output_struct *out=(struct vec_output_struct *)context;
	LTS_CHECK_STATE((*(out->count_p)),src_seg,(uint32_t)src_ofs);
	LTS_INCR_IN((*(out->count_p)),(uint32_t)src_seg);
	int src[2];
	src[0]=src_seg;
	src[1]=src_ofs;
	DSwriteStruct(out->src,src);
	DSwriteStruct(out->e_lbl,labels);
	(void)dst_seg;
	DSwriteStruct(out->dst,&dst_ofs);
}

static lts_enum_cb_t vec_write_begin(lts_output_t output,int which_state,int which_src,int which_dst){
	Warning(info,"begin write");
	struct vec_output_struct *out=RT_NEW(struct vec_output_struct);
	struct archive_io *ctx=(struct archive_io *)output->ops_context;
	lts_type_t ltstype=GBgetLTStype(output->model);
	int N=lts_type_get_state_length(ltstype);
	int sLbls=lts_type_get_state_label_count(ltstype);
	int eLbls=lts_type_get_edge_label_count(ltstype);
	int segment_count=output->segment_count;
	out->count_p=&(output->count);

	if (which_state==segment_count){
		Fatal(1,error,"cannot write more than one segment");
	}
	char base[128];
	char* segofs[2]={"seg","ofs"};
	char* ofs[1]={"ofs"};
	if (!strcmp(output->mode,"viv")){
		if (output->segment_count>1) {
			if (which_dst!=segment_count) Fatal(1,error,"dst selection assumption failed");
			if (which_state!=which_src) Fatal(1,error,"state == src selection assumption failed");
		}
		output->count.state[which_state]=0;
		output->count.out[which_state]=0;
		sprintf(base,"SV-%d-%%d",which_state);
		out->state=arch_write_vec_U32(ctx->archive,base,N,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"SL-%d-%%d",which_state);
		out->s_lbl=arch_write_vec_U32(ctx->archive,base,sLbls,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"ES-%d-%%s",which_state);
		out->src=arch_write_vec_U32_named(ctx->archive,base,1,ofs,ctx->plain?ctx->plain_code:"diff32|gzip",1);
		sprintf(base,"EL-%d-%%d",which_state);
		out->e_lbl=arch_write_vec_U32(ctx->archive,base,eLbls,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"ED-%d-%%d",which_state);
		out->dst=arch_write_vec_U32(ctx->archive,base,N,ctx->plain?ctx->plain_code:"gzip",1);
		Warning(info,"begin write done");
		return lts_enum_viv(N,out,vec_write_state,iv_write_edge);
	}
	if (!strcmp(output->mode,"vsi")){
		if (output->segment_count>1) {
			if (which_src!=segment_count) Fatal(1,error,"src selection assumption failed");
			if (which_state!=which_dst) Fatal(1,error,"state == dst selection assumption failed");
		}
		output->count.state[which_state]=0;
		output->count.in[which_state]=0;
		sprintf(base,"SV-%d-%%d",which_state);
		out->state=arch_write_vec_U32(ctx->archive,base,N,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"SL-%d-%%d",which_state);
		out->s_lbl=arch_write_vec_U32(ctx->archive,base,sLbls,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"ES-%d-%%s",which_state);
		out->src=arch_write_vec_U32_named(ctx->archive,base,2,segofs,ctx->plain?ctx->plain_code:"diff32|gzip",1);
		sprintf(base,"EL-%d-%%d",which_state);
		out->e_lbl=arch_write_vec_U32(ctx->archive,base,eLbls,ctx->plain?ctx->plain_code:"gzip",1);
		sprintf(base,"ED-%d-%%s",which_state);
		out->dst=arch_write_vec_U32_named(ctx->archive,base,1,ofs,ctx->plain?ctx->plain_code:"diff32|gzip",1);
		Warning(info,"begin write done");
		return lts_enum_vii(N,out,vec_write_state,si_write_edge);
	}
	(void)idx_write_state; //needed for -si and -is
	Fatal(1,error,"unknown mode %s",output->mode);
	return NULL;
}

static void vec_write_end(lts_output_t output,lts_enum_cb_t writer){
	(void)output;
	struct vec_output_struct *out=(struct vec_output_struct *)enum_get_context(writer);
	if (out->state) DSstructClose(&(out->state));
	if (out->s_lbl) DSstructClose(&(out->s_lbl));
	if (out->src) DSstructClose(&(out->src));
	if (out->e_lbl) DSstructClose(&(out->e_lbl));
	if (out->dst) DSstructClose(&(out->dst));
	free(out);
}

static void write_chunk_tables(lts_output_t output){
	lts_type_t ltstype=GBgetLTStype(output->model);
	struct archive_io *ctx=(struct archive_io *)output->ops_context;

	stream_t ds;
	int type_count=lts_type_get_type_count(ltstype);
	for(int i=0;i<type_count;i++){
		char stream_name[1024];
		sprintf(stream_name,"CT-%d",i);
		ds=arch_write(ctx->archive,stream_name,ctx->plain?ctx->plain_code:"gzip",1);
		int element_count=GBchunkCount(output->model,i);
		Warning(debug,"type %d has %d elements",i,element_count);
		for(int j=0;j<element_count;j++){
			chunk c=GBchunkGet(output->model,i,j);
			DSwriteVL(ds,c.len);
			DSwrite(ds,c.data,c.len);
		}
		DSclose(&ds);
	}
}

static void vec_write_header(lts_output_t output){
	lts_type_t ltstype=GBgetLTStype(output->model);
	struct archive_io *ctx=(struct archive_io *)output->ops_context;
	stream_t ds=arch_write(ctx->archive,"info",ctx->plain?ctx->plain_code:"",1);
	int N;
	fifo_t fifo=FIFOcreate(4096);
	stream_t fs=FIFOstream(fifo);
	// Identify format
	{
		char id[128];
		sprintf(id,"%s 1.0",output->mode);
		DSwriteS(ds,id);
	}
	// Comment
	char*comment="archive I/O";
	Warning(info,"comment is %s",comment);
	DSwriteS(ds,comment);
	// segment count
	N=output->segment_count;
	DSwriteU32(ds,N);
	Warning(info,"segment count is %d",N);
	// initial state
	// write to FIFO
	N=lts_type_get_state_length(ltstype);
	DSwriteU32(fs,output->root_seg);
	DSwriteU32(fs,output->root_ofs);
	if (output->root_vec){	
		DSwriteU32(fs,N);
		for(int i=0;i<N;i++){
			DSwriteU32(fs,output->root_vec[i]);
			//Warning(info,"root[%d]=%d",i,output->root_vec[i]);
		}
	} else {
		DSwriteU32(fs,0); // root vector undefined.
	}
	// copy FIFO to stream.
	DSwriteVL(ds,FIFOsize(fifo));
	for(;;){
		char data[1024];
		int len=stream_read_max(FIFOstream(fifo),data,1024);
		if (len) DSwrite(ds,data,len);
		if (len<1024) break;
	}
	// the LTS type
	lts_type_serialize(ltstype,FIFOstream(fifo));
	Warning(info,"ltstype is %d bytes",FIFOsize(fifo));
	DSwriteVL(ds,FIFOsize(fifo));
	for(;;){
		char data[1024];
		int len=stream_read_max(FIFOstream(fifo),data,1024);
		if (len) DSwrite(ds,data,len);
		if (len<1024) break;
	}
	// The state and transition counts.
	N=output->segment_count;
	for(int i=0;i<N;i++){
		DSwriteU32(fs,output->count.state[i]);
	}
	if (!strcmp(output->mode,"viv")){
		for(int i=0;i<N;i++){
			DSwriteU32(fs,output->count.out[i]);
		}
	}
	if (!strcmp(output->mode,"vsi")){
		for(int i=0;i<N;i++){
			DSwriteU32(fs,output->count.in[i]);
		}
	}
	DSwriteVL(ds,FIFOsize(fifo));
	for(;;){
		char data[1024];
		int len=stream_read_max(FIFOstream(fifo),data,1024);
		if (len) DSwrite(ds,data,len);
		if (len<1024) break;
	}
	// The tree compression used. (none)
	DSwriteVL(ds,0);
	DSclose(&ds);
	FIFOdestroy(&fifo);
}

static void vec_write_close(lts_output_t output){
	struct archive_io *ctx=(struct archive_io *)output->ops_context;
	if (output->share==0) write_chunk_tables(output);
	if (output->share==0) vec_write_header(output);
	arch_close(&(ctx->archive));
}

static void vec_write_open(lts_output_t output){
	if (!strcmp(output->mode,"viv") || !strcmp(output->mode,"vsi")){
		output->ops.write_begin=vec_write_begin;
		output->ops.write_end=vec_write_end;
		output->ops.write_close=vec_write_close;
		return;
	}
	Fatal(1,error,"Write mode %s unsupported in vector mode",output->mode);
}

static void vec_read_part(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	(void)which_state;(void)which_dst;
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	int segment_count=input->segment_count;
	lts_type_t ltstype=GBgetLTStype(input->model);
	int N=lts_type_get_state_length(ltstype);
	int sLbls=lts_type_get_state_label_count(ltstype);
	int eLbls=lts_type_get_edge_label_count(ltstype);

	int i_from=(which_src==segment_count)?0:which_src;
	int i_to=(which_src==segment_count)?segment_count:(which_src+1);
	char base[128];		
	char* ofs[1]={"ofs"};
	char* segofs[2]={"seg","ofs"};
	// Pass 1: states and labels.
	for(int i=i_from;i<i_to;i++){
		Warning(info,"enumerating states of part %d",i);
		sprintf(base,"SV-%d-%%d",i);
		struct_stream_t vec=arch_read_vec_U32(ctx->archive,base,N,ctx->decode);
		sprintf(base,"SL-%d-%%d",i);
		struct_stream_t map=arch_read_vec_U32(ctx->archive,base,sLbls,ctx->decode);
		int src_vec[N];
		int map_vec[sLbls];
		for (uint32_t j=0;j<input->count.state[i];j++){
			DSreadStruct(vec,src_vec);
			DSreadStruct(map,map_vec);
			enum_vec(output,src_vec,map_vec);
			//Warning(info,"state %d",j);
		}
		DSstructClose(&vec);
		DSstructClose(&map);
	}
	// Pass 2: edges and labels.	
	for(int i=i_from;i<i_to;i++){
		Warning(info,"enumerating edges of part %d",i);
		if(!strcmp(input->mode,"viv")){
			int src_ofs[1];
			int lbl_vec[eLbls];
			int dst_vec[N];
			sprintf(base,"ES-%d-%%s",i);
			struct_stream_t src=arch_read_vec_U32_named(ctx->archive,base,1,ofs,ctx->decode);
			sprintf(base,"EL-%d-%%d",i);
			struct_stream_t lbl=arch_read_vec_U32(ctx->archive,base,eLbls,ctx->decode);
			sprintf(base,"ED-%d-%%d",i);
			struct_stream_t dst=arch_read_vec_U32(ctx->archive,base,N,ctx->decode);
			for (uint32_t j=0;j<input->count.out[i];j++){
				DSreadStruct(src,src_ofs);
				DSreadStruct(dst,dst_vec);
				DSreadStruct(lbl,lbl_vec);
				enum_seg_vec(output,i,src_ofs[0],dst_vec,lbl_vec);
				//Warning(info,"edge %d",j);
			}
			// Note that:
			// 1. We should test what was requested.
			// 2. We should return the requested info in one pass. (ltsmin-convert will break)
			DSstructClose(&src);
			DSstructClose(&lbl);
			DSstructClose(&dst);
		}
		if(!strcmp(input->mode,"vsi")){
			int src_vec[2];
			int lbl_vec[eLbls];
			int dst_ofs[1];
			sprintf(base,"ES-%d-%%s",i);
			struct_stream_t src=arch_read_vec_U32_named(ctx->archive,base,2,segofs,ctx->decode);
			sprintf(base,"EL-%d-%%d",i);
			struct_stream_t lbl=arch_read_vec_U32(ctx->archive,base,eLbls,ctx->decode);
			sprintf(base,"ED-%d-%%s",i);
			struct_stream_t dst=arch_read_vec_U32_named(ctx->archive,base,1,ofs,ctx->decode);
			for (uint32_t j=0;j<input->count.in[i];j++){
				DSreadStruct(src,src_vec);
				DSreadStruct(dst,dst_ofs);
				DSreadStruct(lbl,lbl_vec);
				enum_seg_seg(output,src_vec[0],src_vec[1],i,dst_ofs[0],lbl_vec);
				//Warning(info,"edge %d (seg %d) of %d",j,i,input->count.in[i]);
			}
			// Note that:
			// 1. We should test what was requested.
			// 2. We should return the requested info in one pass. (ltsmin-convert will break)
			DSstructClose(&src);
			DSstructClose(&lbl);
			DSstructClose(&dst);
		}
	}
}

static void load_chunk_tables(lts_input_t input){
	lts_type_t ltstype=GBgetLTStype(input->model);
	struct archive_io *ctx=(struct archive_io *)input->ops_context;

	stream_t ds;
	int type_count=lts_type_get_type_count(ltstype);
	for(int i=0;i<type_count;i++){
		Warning(info,"loading type %s",lts_type_get_type(ltstype,i));
		char stream_name[1024];
		sprintf(stream_name,"CT-%d",i);
		ds=arch_read(ctx->archive,stream_name,ctx->decode);
		int L;
		for(L=0;;L++){
			if (DSempty(ds)) break;
			int len=DSreadVL(ds);
			char data[len];
			DSread(ds,data,len);
			if (GBchunkPut(input->model,i,chunk_ld(len,data))!=L){
				Fatal(1,error,"position of chunk %d was not %d",L,L);
			}
		}
		Warning(info,"%d elements",L);
		DSclose(&ds);
	}
}

/** Continue loading vec headers. The description has been read and copied to input->mode;
 */
static void load_vec_headers(lts_input_t input,stream_t ds){
	//struct archive_io *ctx=(struct archive_io *)input->ops_context;
	Warning(info,"reading header (%s)",input->mode);
	int N;
	fifo_t fifo=FIFOcreate(4096);

	if (input->mode[3]!=' '){
		Fatal(1,error,"unknown format: %s",input->mode);
	}
	input->mode[3]=0;// extract mode and check
	if (strcmp(input->mode,"viv") && strcmp(input->mode,"vsi")){
		Fatal(1,error,"unknown mode: %s",input->mode);
	}
	if (strcmp(input->mode+4,"1.0")){ // check version
		Fatal(1,error,"unknown version %s",input->mode+4);
	}
	input->comment=DSreadSA(ds);
	Warning(info,"comment is %s",input->comment);
	N=DSreadU32(ds);
	Warning(info,"segment count is %d",N);
	input->segment_count=N;
	N=DSreadVL(ds);
	{
		stream_t fs=FIFOstream(fifo);
		char data[N];
		DSread(ds,data,N);
		DSwrite(fs,data,N);
		input->root_seg=DSreadU32(fs);
		input->root_ofs=DSreadU32(fs);
		N=DSreadU32(fs);
		if (N) {
			Warning(info,"state length is %d",N);
			input->root_vec=(uint32_t*)RTmalloc(N*sizeof(uint32_t));
			for(int i=0;i<N;i++){
				input->root_vec[i]=DSreadU32(fs);
				//Warning(info,"root[%d]=%d",i,input->root_vec[i]);
			}
		} else {
			Warning(info,"no initial state");
		}
		if (FIFOsize(fifo)) Fatal(1,error,"Too much data in initial state (%d bytes)",FIFOsize(fifo));
	}
	N=DSreadVL(ds);
	{
		stream_t fs=FIFOstream(fifo);
		char data[N];
		DSread(ds,data,N);
		DSwrite(fs,data,N);
		lts_type_t ltstype=lts_type_deserialize(fs);
		if (FIFOsize(fifo)) Fatal(1,error,"Too much data in lts type (%d bytes)",FIFOsize(fifo));
		GBsetLTStype(input->model,ltstype);
		if (input->root_vec) GBsetInitialState(input->model,(int*)input->root_vec);
	}
	Warning(debug,"getting counts");
	N=DSreadVL(ds);
	{
		stream_t fs=FIFOstream(fifo);
		char data[N];
		Warning(debug,"getting %d bytes",N);
		DSread(ds,data,N);
		DSwrite(fs,data,N);
		N=input->segment_count;
		Warning(debug,"fifo now %d bytes for %d segments",FIFOsize(fifo),N);
		lts_count_init(&(input->count),0,input->segment_count,input->segment_count);
		for(int i=0;i<N;i++){
			input->count.state[i]=DSreadU32(fs);
		}
		if (!strcmp(input->mode,"viv")){
			for(int i=0;i<N;i++){
				input->count.out[i]=DSreadU32(fs);
			}
		}
		if (!strcmp(input->mode,"vsi")){
			for(int i=0;i<N;i++){
				input->count.in[i]=DSreadU32(fs);
			}
		}
		if (FIFOsize(fifo)) Fatal(1,error,"Too much data in state and transition counts (%d bytes)",FIFOsize(fifo));
	}
	Warning(debug,"getting compression tree");
	N=DSreadVL(ds);
	if (N) {
		Fatal(1,error,"Tree compression is unsupported in this version");
	}
	DSclose(&ds);
	Warning(info,"vec header read");
	input->ops.read_part=vec_read_part;
	input->ops.read_close=arch_read_close;
	load_chunk_tables(input);
}


/* shared functions for archive based file formats. */



static void dir_or_vec_write(lts_output_t output){
	lts_type_t ltstype=GBgetLTStype(output->model);
	if (	   (lts_type_get_state_label_count(ltstype)!=0)
		|| (lts_type_get_edge_label_count(ltstype)!=1)
		|| (output->mode[0]=='v')
		|| (output->mode[1]=='v')
		|| (output->mode[2]=='v')
	){
		Warning(debug,"writing extended DIR");
		vec_write_open(output);	
	} else {
		Warning(debug,"writing legacy DIR");
		dir_write_open_ops(output);
	}
}

/* Open a directory for writing. (detect format) */
static void dir_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	if (output->share==0){
		if(create_empty_dir(output->name,DELETE_ALL)){
			FatalCall(1,error,"could not create or clear directory %s",output->name);
		}
	} else {
		for(int i=0;;i++){
			if (i==1000) Fatal(1,error,"timeout during creation of %s",output->name);
			if (is_a_dir(output->name)) break;
			usleep(10000);
		}
	}
	ctx->archive=arch_dir_open(output->name,blocksize);
	ctx->plain=1;
	ctx->plain_code=NULL;
	output->ops_context=ctx;
	dir_or_vec_write(output);
}

static void dz_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	if (output->share==0){
		if(create_empty_dir(output->name,DELETE_ALL)){
			FatalCall(1,error,"could not create or clear directory %s",output->name);
		}
	} else {
		for(int i=0;;i++){
			if (i==1000) Fatal(1,error,"timeout during creation of %s",output->name);
			if (is_a_dir(output->name)) break;
			usleep(10000);
		}
	}
	ctx->archive=arch_dir_open(output->name,blocksize);
	ctx->plain=0;
	output->ops_context=ctx;
	dir_or_vec_write(output);
}



/* open a GCF (vector format) */
static void gcf_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	Warning(info,"opening share %d of %d",output->share,output->share_count);
	ctx->archive=arch_gcf_create(raf_unistd(output->name),blocksize,blocksize*blockcount,output->share,output->share_count);
	ctx->plain=plain;
	ctx->plain_code="";
	output->ops_context=ctx;
	dir_or_vec_write(output);
}


/*
 * Open the info stream in the archive and detect what format it is
 */
static void load_headers(lts_input_t input){
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	stream_t ds=arch_read(ctx->archive,"info",ctx->decode);
	char description[1024];
	DSreadS(ds,description,1024);
	if (strlen(description)==0) {
		if (DSreadU16(ds)==31){
			Log(info,"legacy DIR format");
			load_dir_headers(input,ds);
			return;
		} else {
			Fatal(1,error,"cannot identify input format");
			return;
		}
	} else {
		if (!strncmp(description,"viv",3) || !strncmp(description,"vsi",3)){
			input->mode=strdup(description);
			load_vec_headers(input,ds);
			return;
		}
		Fatal(1,error,"info format %s unknown",description);
	}
}


/* open an uncompressed directory for eading */
static void dir_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_dir_open(input->name,blocksize);
	ctx->decode=NULL;
	input->ops_context=ctx;
	load_headers(input);
}

/* open a compressed directory for eading */
static void dz_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_dir_open(input->name,blocksize);
	ctx->decode="auto";
	input->ops_context=ctx;
	load_headers(input);
}

/* open a GCF archive for reading */
static void gcf_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_gcf_read(raf_unistd(input->name));
	ctx->decode="auto";
	input->ops_context=ctx;
	load_headers(input);
}

/* initialisation callback */
static void archive_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		Fatal(1,error,"unexpected call to archive_popt");
	case POPT_CALLBACK_REASON_POST: {
		lts_write_register("dir",dir_write_open);
		lts_read_register("dir",dir_read_open);
		lts_write_register("dz",dz_write_open);
		lts_read_register("dz",dz_read_open);
		lts_write_register("gcf",gcf_write_open);
		lts_read_register("gcf",gcf_read_open);
		return;
		}
	case POPT_CALLBACK_REASON_OPTION:
		Fatal(1,error,"unexpected call to bcg_popt");
	}
}

/* Options for archive based file formats. */
struct poptOption archive_io_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , archive_popt , 0 , NULL , NULL },
	{ "block-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blocksize , 0 , "set the size of a block in bytes" , "<bytes>" },
	{ "cluster-size" , 0 , POPT_ARG_INT|POPT_ARGFLAG_SHOW_DEFAULT , &blockcount , 0 , "set the number of blocks in a GCF cluster" , "<blocks>"},
	{ "plain" , 0 , POPT_ARG_VAL , &plain , 1 , "disable compression of GCF containers" , NULL },
	POPT_TABLEEND
};


