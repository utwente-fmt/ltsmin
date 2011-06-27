#include <config.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#include <lts_io_internal.h>
#include <runtime.h>
#include <treedbs.h>

struct pg_output {
	model_t model;
	treedbs_t dbs;
	int current_state_idx;
	int previous_src_idx;
	FILE *outputfile;
};

static void pg_write_state_vec(void* context, int* state, int* labels){
    struct pg_output* ctx=(struct pg_output*)context;
    int idx=TreeFold(ctx->dbs,state);
    if (ctx->current_state_idx!=INT_MIN)
    {
        fprintf(ctx->outputfile,";\n");
    }
    ctx->current_state_idx = idx;
    fprintf(ctx->outputfile,"%d %d %d ", idx, labels[0] /* priority*/, labels[1] /* player */);
}

static void pg_write_edge_seg_seg(void* context, int src_seg, int src_ofs, int dst_seg, int dst_ofs, int*labels){
    (void)src_seg;
    (void)dst_seg;
    (void)labels;
    struct pg_output* ctx=(struct pg_output*)context;
    assert(src_ofs==ctx->current_state_idx);
    if (src_ofs==ctx->previous_src_idx)
    {
        fprintf(ctx->outputfile,",%d", dst_ofs);
    }
    else
    {
        fprintf(ctx->outputfile,"%d", dst_ofs);
        ctx->previous_src_idx = src_ofs;
    }
}

static lts_enum_cb_t pg_write_begin(lts_output_t output,int which_state,int which_src,int which_dst){
	(void)which_state;
	(void)which_src;
	(void)which_dst;
	lts_type_t ltstype = GBgetLTStype(output->model);
	int N = lts_type_get_state_length(ltstype);
	struct pg_output* ctx = RT_NEW(struct pg_output);
	ctx->model = output->model;
	ctx->dbs = TreeDBScreate(N);
	ctx->current_state_idx = INT_MIN;
	ctx->previous_src_idx = INT_MIN;
	ctx->outputfile = fopen(output->name, "w");
	Warning(info,"opened %s",output->name);
	//fprintf(ctx->outputfile,"parity %d;\n", INT_MAX);
	lts_enum_cb_t enum_cb = lts_enum_vii(N, ctx, pg_write_state_vec, pg_write_edge_seg_seg);
	return enum_cb;
}

static void pg_write_end(lts_output_t output,lts_enum_cb_t writer){
    struct pg_output* ctx=(struct pg_output*)enum_get_context(writer);
    if (ctx->current_state_idx != INT_MIN)
    {
        fprintf(ctx->outputfile,";\n");
    }
    fclose(ctx->outputfile);
	free(ctx);
}

static void pg_write_close(lts_output_t output){
    (void)output;
}

static void pg_write_open(lts_output_t output){
	output->ops.write_begin = pg_write_begin;
	output->ops.write_end = pg_write_end;
	output->ops.write_close = pg_write_close;
}

static void pg_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;
	(void)opt;
	(void)arg;
	(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		Fatal(1,error,"unexpected call to pg_popt");
	case POPT_CALLBACK_REASON_POST:
		lts_write_register("pg",pg_write_open);
		return;
	case POPT_CALLBACK_REASON_OPTION:
		Fatal(1,error,"unexpected call to pg_popt");
	}
}

struct poptOption pg_io_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , pg_popt , 0 , NULL , NULL},
	POPT_TABLEEND
};

