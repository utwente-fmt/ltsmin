#include <config.h>
#include <unistd.h>
#include <runtime.h>
#include <lts_io_internal.h>
#include <archive.h>
#include <dir_ops.h>

/* these three have to become static after moving to popt: */
int plain=0;
int blocksize=32768;
int blockcount=32;

struct archive_io {
	archive_t archive;
	int plain;
	char*plain_code;
	char*decode;
};

static void dir_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	if (output->share==0){
		if(create_empty_dir(output->name,DELETE_ALL)){
			FatalCall(1,error,"could not create or clear directory %s",output->name);
		}
	} else {
		for(;;){
			if (is_a_dir(output->name)) break;
			usleep(10000);
		}
	}
	ctx->archive=arch_dir_open(output->name,blocksize);
	ctx->plain=1;
	ctx->plain_code=NULL;
	output->ops_context=ctx;
}

static void gcf_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_gcf_create(raf_unistd(output->name),blocksize,blocksize*blockcount,output->share,output->share_count);
	ctx->plain=plain;
	ctx->plain_code="";
	output->ops_context=ctx;
}
static void fmt_write_open(lts_output_t output){
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_fmt(output->name,file_input,file_output,blocksize);
	ctx->plain=plain;
	ctx->plain_code=NULL;
	output->ops_context=ctx;
}

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

/*
static void vec_write_close(lts_output_t output){
	lts_type_t ltstype=GBgetLTStype(output->model);
	struct archive_io *ctx=(struct archive_io *)output->ops_context;

	stream_t ds;
	int type_count=lts_type_get_type_count(ltstype);
	for(int i=0;i<type_count;i++){
		char stream_name[1024];
		sprintf(stream_name,"CT-%s",lts_type_get_type(ltstype,i));
		ds=arch_write(ctx->archive,stream_name,plain?"":"gzip",1);
		int element_count=GBchunkCount(output->model,i);
		Warning(debug,"type %d has %d elements",i,element_count);
		for(int j=0;j<element_count;j++){
			chunk c=GBchunkGet(output->model,i,j);
			DSwriteVL(ds,c.len);
			DSwrite(ds,c.data,c.len);
		}
		DSclose(&ds);
	}
	ds=arch_write(ctx->archive,"info","",1);
	DSwriteS(ds,"no info format for vec has been defined");
	DSclose(&ds);
	arch_close(&(ctx->archive));
}
*/

static void load_headers(lts_input_t input){
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	stream_t ds=arch_read(ctx->archive,"info",NULL);
	char description[1024];
	DSreadS(ds,description,1024);
	if (strlen(description)==0) {
		switch(DSreadU16(ds)){
		case 0:
			if (DSreadU16(ds)!=31) {
				Fatal(1,error,"cannot identify input format");
				return;
			}
			Log(debug,"input uses headers");
			ctx->decode="auto";
			break;
		case 31:
			Log(debug,"input has no headers");
			break;
		default:
			Fatal(1,error,"cannot identify input format");
			return;
		}
	} else {
		Log(debug,"input uses headers");
		ctx->decode="auto";
		ds=stream_setup(ds,description);
		if (DSreadU32(ds)!=31){
			Fatal(1,error,"cannot identify input format");
		}
	}
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
		Warning(debug,"input segment %d has %d states",i,(int)input->count.state[i]);
	}
	for(int i=0;i<input->segment_count;i++){
		for(int j=0;j<input->segment_count;j++){
			input->count.cross[i][j]=DSreadU32(ds);
			Warning(debug,"input edge count from %d to %d is %d",i,j,(int)input->count.cross[i][j]);
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
}


static void dir_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_dir_open(input->name,blocksize);
	input->ops_context=ctx;
	load_headers(input);
}
static void gcf_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_gcf_read(raf_unistd(input->name));
	input->ops_context=ctx;
	load_headers(input);
}
static void fmt_read_open(lts_input_t input){
	Warning(debug,"opening %s",input->name);
	struct archive_io *ctx=RT_NEW(struct archive_io);
	ctx->archive=arch_fmt(input->name,file_input,file_output,blocksize);
	input->ops_context=ctx;
	load_headers(input);
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
				enum_seg_seg(output,i,s,j,d,(int*)&l);
			}
			DSclose(&src_in);
			DSclose(&lbl_in);
			DSclose(&dst_in);
		}
	}
}

static void arch_read_close(lts_input_t input){
	struct archive_io *ctx=(struct archive_io *)input->ops_context;
	arch_close(&(ctx->archive));
}


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

struct lts_io_ops dir_io_ops={
	dir_write_open,
	dir_write_begin,
	dir_write_end,
	dir_write_close,
	dir_read_open,
	dir_read_part,
	arch_read_close
};
struct lts_io_ops gcf_io_ops={
	gcf_write_open,
	dir_write_begin,
	dir_write_end,
	dir_write_close,
	gcf_read_open,
	dir_read_part,
	arch_read_close
};
struct lts_io_ops fmt_io_ops={
	fmt_write_open,
	dir_write_begin,
	dir_write_end,
	dir_write_close,
	fmt_read_open,
	dir_read_part,
	arch_read_close
};




