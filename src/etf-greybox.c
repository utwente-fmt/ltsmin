
#include "runtime.h"
#include "etf-util.h"
#include "etf-greybox.h"
#include "lts.h"

static void etf_popt(poptContext con,
 		enum poptCallbackReason reason,
                            const struct poptOption * opt,
                             const char * arg, void * data){
	(void)con;(void)opt;(void)arg;(void)data;
	switch(reason){
	case POPT_CALLBACK_REASON_PRE:
		break;
	case POPT_CALLBACK_REASON_POST:
		GBregisterLoader("etf",ETFloadGreyboxModel);
		Warning(info,"ETF language module initialized");
		return;
	case POPT_CALLBACK_REASON_OPTION:
		break;
	}
	Fatal(1,error,"unexpected call to etf_popt");
}
struct poptOption etf_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , etf_popt , 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
	treedbs_t label_db;
	int edge_labels;
	treedbs_t* trans_db;
	lts_t* trans_table;
	int* trans_len;
	treedbs_t* label_key;
	int** label_data;
} *gb_context_t;

static int etf_short(model_t self,int group,int*src,TransitionCB cb,void*user_context){
	gb_context_t ctx=(gb_context_t)GBgetContext(self);
	int src_no=TreeFold(ctx->trans_db[group],src);
	int dst[ctx->trans_len[group]];
	//Warning(info,"group %d, state %d",group,src_no);
	lts_t lts=ctx->trans_table[group];
	//for(int j=0;j<=lts->states;j++){
	//	Warning(info,"begin[%d]=%d",j,lts->begin[j]);
	//}
	if (((uint32_t)src_no)>=lts->states) return 0;
	for(uint32_t i=lts->begin[src_no];i<lts->begin[src_no+1];i++){
		TreeUnfold(ctx->trans_db[group],lts->dest[i],dst);
		switch(ctx->edge_labels){
			case 0:
				cb(user_context,NULL,dst);
				break;
			case 1: {
				int lbl=lts->label[i];
				cb(user_context,&lbl,dst);
				break;
			}
			default: {
				int lbl[ctx->edge_labels];
				TreeUnfold(ctx->label_db,lts->label[i],lbl);
				cb(user_context,lbl,dst);
				break;
			}
		}
	}
	return (lts->begin[src_no+1]-lts->begin[src_no]);
}

static int etf_state_short(model_t self,int label,int *state){
	gb_context_t ctx=(gb_context_t)GBgetContext(self);
	return ctx->label_data[label][TreeFold(ctx->label_key[label],state)];
}

void ETFloadGreyboxModel(model_t model,const char*name){
	gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
	GBsetContext(model,ctx);
	etf_model_t etf=etf_parse(name);
	lts_type_t ltstype=etf_type(etf);
	int state_length=lts_type_get_state_length(ltstype);
	ctx->edge_labels=lts_type_get_edge_label_count(ltstype);
	if (ctx->edge_labels>1) {
		ctx->label_db=TreeDBScreate(ctx->edge_labels);
	} else {
		ctx->label_db=NULL;
	}
	GBsetLTStype(model,ltstype);

	treedbs_t pattern_db=etf_patterns(etf);
	edge_info_t e_info=(edge_info_t)RTmalloc(sizeof(struct edge_info));
	e_info->groups=etf_trans_section_count(etf);
	e_info->length=(int*)RTmalloc(e_info->groups*sizeof(int));
	e_info->indices=(int**)RTmalloc(e_info->groups*sizeof(int*));
	ctx->trans_len=e_info->length;
	ctx->trans_db=(treedbs_t*)RTmalloc(e_info->groups*sizeof(treedbs_t));
	ctx->trans_table=(lts_t*)RTmalloc(e_info->groups*sizeof(lts_t));
	for(int i=0;i<e_info->groups;i++){
		Warning(info,"parsing table %d",i);
		treedbs_t trans=etf_trans_section(etf,i);
		int used[state_length];
		int step[2+ctx->edge_labels];
		TreeUnfold(trans,0,step);
		TreeUnfold(pattern_db,step[0],used);
		int len=0;
		for(int j=0;j<state_length;j++){
			if (used[j]) {
				used[len]=j;
				len++;
			}
		}
		int*proj=(int*)RTmalloc(len*sizeof(int));
		for(int j=0;j<len;j++) proj[j]=used[j];
		e_info->length[i]=len;
		e_info->indices[i]=proj;
		TreeUnfold(pattern_db,step[0],used);

		ctx->trans_db[i]=TreeDBScreate(len);
		ctx->trans_table[i]=lts_create();
		lts_set_type(ctx->trans_table[i],LTS_LIST);
		lts_set_size(ctx->trans_table[i],TreeCount(trans),TreeCount(trans));

		int state[state_length];
		int src[len];
		int dst[len];

		for(int j=TreeCount(trans)-1;j>=0;j--){
			TreeUnfold(trans,j,step);
			TreeUnfold(pattern_db,step[0],state);
			for(int k=0;k<state_length;k++) {
				if(used[k]?(state[k]==0):(state[k]!=0)){
					Fatal(1,error,"inconsistent section");
				}
			}
			for(int k=0;k<len;k++) src[k]=state[proj[k]]-1;
			TreeUnfold(pattern_db,step[1],state);
			for(int k=0;k<state_length;k++) {
				if(used[k]?(state[k]==0):(state[k]!=0)){
					Fatal(1,error,"inconsistent section");
				}
			}
			for(int k=0;k<len;k++) dst[k]=state[proj[k]]-1;
			ctx->trans_table[i]->src[j]=TreeFold(ctx->trans_db[i],src);
			switch(ctx->edge_labels){
				case 0:
					ctx->trans_table[i]->label[j]=0;
					break;
				case 1:
					ctx->trans_table[i]->label[j]=step[2];
					break;
				default:
					ctx->trans_table[i]->label[j]=TreeFold(ctx->label_db,step+2);
					break;
			}
			ctx->trans_table[i]->dest[j]=TreeFold(ctx->trans_db[i],dst);
		}
		Warning(info,"table %d has %d states and %d transitions",i,TreeCount(ctx->trans_db[i]),TreeCount(trans));
		lts_set_size(ctx->trans_table[i],TreeCount(ctx->trans_db[i]),TreeCount(trans));
		lts_set_type(ctx->trans_table[i],LTS_BLOCK);
		//for(int j=0;j<=ctx->trans_table[i]->states;j++){
		//	Warning(info,"begin[%d]=%d",j,ctx->trans_table[i]->begin[j]);
		//}
	}
	GBsetEdgeInfo(model,e_info);
	GBsetNextStateShort(model,etf_short);

	state_info_t s_info=(state_info_t)RTmalloc(sizeof(struct state_info));
	s_info->labels=etf_map_section_count(etf);
	s_info->length=(int*)RTmalloc(s_info->labels*sizeof(int));
	s_info->indices=(int**)RTmalloc(s_info->labels*sizeof(int*));
	ctx->label_key=(treedbs_t*)RTmalloc(s_info->labels*sizeof(treedbs_t));
	ctx->label_data=(int**)RTmalloc(s_info->labels*sizeof(int*));
	for(int i=0;i<s_info->labels;i++){
		Warning(info,"parsing map %d",i);
		treedbs_t map=etf_get_map(etf,i);
		int used[state_length+1];
		TreeUnfold(map,0,used);
		int len=0;
		for(int j=0;j<state_length;j++){
			if (used[j]) {
				used[len]=j;
				len++;
			}
		}
		int*proj=(int*)RTmalloc(len*sizeof(int));
		for(int j=0;j<len;j++) proj[j]=used[j];
		s_info->length[i]=len;
		s_info->indices[i]=proj;
		TreeUnfold(map,0,used);
		treedbs_t key_db=TreeDBScreate(len);
		int *data=(int*)RTmalloc(TreeCount(map)*sizeof(int));
		int entry[state_length+1];
		int key[len];
		for(int j=TreeCount(map)-1;j>=0;j--){
			TreeUnfold(map,j,entry);
			for(int k=0;k<state_length;k++) {
				if(used[k]?(entry[k]==0):(entry[k]!=0)){
					Fatal(1,error,"inconsistent map section");
				}
			}
			for(int k=0;k<len;k++) key[k]=entry[proj[k]]-1;
			data[TreeFold(key_db,key)]=entry[state_length];
		}
		ctx->label_key[i]=key_db;
		ctx->label_data[i]=data;
	}
	GBsetStateInfo(model,s_info);
	GBsetStateLabelShort(model,etf_state_short);

	int type_count=lts_type_get_type_count(ltstype);
	for(int i=0;i<type_count;i++){
		Warning(info,"Setting values for type %d (%s)",i,lts_type_get_type(ltstype,i));
		int count=etf_get_value_count(etf,i);
		for(int j=0;j<count;j++){
			if (j!=GBchunkPut(model,i,etf_get_value(etf,i,j))){
				Fatal(1,error,"etf-greybox does not support remapping of values");
			}
		}
	}

	int state[state_length];
	etf_get_initial(etf,state);
	GBsetInitialState(model,state);
}

