#include <string.h>
#include <bcg_user.h>
#include <lts_io_internal.h>
#include <runtime.h>

static int bcg_write_used=0;

struct bcg_output {
	model_t model;
	int typeno;
	int segment_count;
};

static void bcg_write_state(void* lts_output,int seg,int ofs,int* labels){
	//Warning(debug,"state");
	(void)lts_output;
	(void)seg;
	(void)ofs;
	(void)labels;
}

static void bcg_write_edge(void* lts_output,int src_seg,int src_ofs,int dst_seg,int dst_ofs,int*labels){
	//Warning(debug,"edge");
	struct bcg_output* out=(struct bcg_output*)lts_output;
	char buffer[1024];
	chunk c=chunk_ld(1024,buffer);
	chunk_encode_copy(c,GBchunkGet(out->model,out->typeno,labels[0]),'\\');
	BCG_IO_WRITE_BCG_EDGE ((src_ofs*out->segment_count)+src_seg,strcmp(buffer,"tau")?buffer:"i",(dst_ofs*out->segment_count)+dst_seg);
}

static lts_enum_cb_t bcg_write_begin(lts_output_t output,int which_state,int which_src,int which_dst){
	(void)which_state;(void)which_src;(void)which_dst;
	if (bcg_write_used){
		Fatal(1,error,"cannot write more than one BCG file at the same time");
	}
	bcg_write_used=1;
	if (output->root_seg) Fatal(1,error,"illegal root segment %d",output->root_seg);
	BCG_IO_WRITE_BCG_BEGIN (output->name,output->root_ofs,1,get_label(),0);
	Warning(info,"opened %s",output->name);
	lts_type_t ltstype=GBgetLTStype(output->model);
	int N=lts_type_get_state_length(ltstype);
	struct bcg_output* ctx=RT_NEW(struct bcg_output);
	ctx->model=output->model;
	ctx->typeno=lts_type_get_edge_label_typeno(ltstype,0);
	ctx->segment_count=output->segment_count;
	return lts_enum_iii(N,ctx,bcg_write_state,bcg_write_edge);
}

static void bcg_write_end(lts_output_t output,lts_enum_cb_t writer){
	(void)output;
	BCG_IO_WRITE_BCG_END();
	free(enum_get_context(writer));
	bcg_write_used=0;
}

static void bcg_write_close(lts_output_t output){
	if (output->segment_count!=1) {
		Warning(info,"Writing of PBG file unimplemented");
		Fatal(1,error,"%d",output->segment_count);
	}
}


struct bcg_input {
	BCG_TYPE_OBJECT_TRANSITION bcg_graph;
};

static void bcg_read_part(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output){
	(void)which_state;(void)which_src;(void)which_dst;
	struct bcg_input*ctx=(struct bcg_input*)input->ops_context;
	bcg_type_state_number bcg_s1, bcg_s2;
	BCG_TYPE_LABEL_NUMBER bcg_label_number;
	BCG_OT_ITERATE_PLN (ctx->bcg_graph, bcg_s1, bcg_label_number, bcg_s2) {
		enum_seg_seg(output,0,bcg_s1,0,bcg_s2,(int*)&bcg_label_number);
	} BCG_OT_END_ITERATE;
}

static void bcg_read_close(lts_input_t input){
	struct bcg_input*ctx=(struct bcg_input*)input->ops_context;
	BCG_OT_READ_BCG_END (&(ctx->bcg_graph));
}

static void bcg_write_open(lts_output_t output){
	if (output->segment_count!=1) {
		Fatal(1,error,"this tool does not support partitioned BCG");
	}
	if (strcmp(output->mode,"iii")&&strcmp(output->mode,"-ii")) Fatal(1,error,"Write mode %s not supported when writing BCG",output->mode);
	output->ops.write_begin=bcg_write_begin;
	output->ops.write_end=bcg_write_end;
	output->ops.write_close=bcg_write_close;
}

static void bcg_read_open(lts_input_t input){
	if (input->share_count!=1) {
		Fatal(1,error,"parallel reading of BCG unsupported");
	}
	input->segment_count=1;
	lts_count_init(&(input->count),0,1,1); 
	if (input->share_count!=1) Fatal(1,error,"parallel reading not supported");
	lts_type_t ltstype=lts_type_create();
	lts_type_set_state_length(ltstype,2);
	int action_type=lts_type_add_type(ltstype,"action",NULL);
	lts_type_set_edge_label_count(ltstype,1);
	lts_type_set_edge_label_name(ltstype,0,"action");
	lts_type_set_edge_label_type(ltstype,0,"action");
	GBsetLTStype(input->model,ltstype);

	struct bcg_input*ctx=RT_NEW(struct bcg_input);
	BCG_TYPE_C_STRING bcg_comment;
	BCG_OT_READ_BCG_BEGIN (input->name, &(ctx->bcg_graph), 0);
	BCG_READ_COMMENT (BCG_OT_GET_FILE (ctx->bcg_graph), &bcg_comment);
	Warning(info,"comment is: %s",bcg_comment);
	input->root_seg=0;
	input->root_ofs=BCG_OT_INITIAL_STATE (ctx->bcg_graph);
	int s0[2];
	s0[0]=input->root_seg;
	s0[1]=input->root_ofs;
	GBsetInitialState(input->model,s0);

	int label_count=BCG_OT_NB_LABELS (ctx->bcg_graph);
	for(int i=0;i<label_count;i++){
		char *lbl=BCG_OT_LABEL_STRING (ctx->bcg_graph,i);
		if (!BCG_OT_LABEL_VISIBLE (ctx->bcg_graph,i)){
			lbl="tau";
		}
		if (GBchunkPut(input->model,action_type,chunk_str(lbl))!=i){
			Fatal(1,error,"position of label %d was not %d",i,i);
		}
	}
	input->count.state[0]=BCG_OT_NB_STATES (ctx->bcg_graph);
	input->count.in[0]=BCG_OT_NB_EDGES (ctx->bcg_graph);
	input->count.out[0]=input->count.in[0];
	input->count.cross[0][0]=input->count.in[0];
	input->ops_context=ctx;
	input->ops.read_part=bcg_read_part;
	input->ops.read_close=bcg_read_close;
}

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
		lts_write_register("bcg",bcg_write_open);
		lts_read_register("bcg",bcg_read_open);
		return;
	case POPT_CALLBACK_REASON_OPTION:
		Fatal(1,error,"unexpected call to bcg_popt");
	}
}

struct poptOption bcg_io_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , bcg_popt , 0 , NULL , NULL},
	POPT_TABLEEND
};

