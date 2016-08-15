#include <hre/config.h>

#include <dm/dm.h>
#include <hre/user.h>
#include <ltsmin-lib/etf-util.h>
#include <pins-lib/pins-util.h>
#include <pins-lib/modules/etf-pins.h>
#include <hre/stringindex.h>
#include <util-lib/tables.h>


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
	Abort("unexpected call to etf_popt");
}
struct poptOption etf_options[]= {
	{ NULL, 0 , POPT_ARG_CALLBACK|POPT_CBFLAG_POST|POPT_CBFLAG_SKIPOPTION , etf_popt , 0 , NULL , NULL },
	POPT_TABLEEND
};

typedef struct grey_box_context {
	string_index_t label_idx;
	int edge_labels;
    string_index_t* trans_key_idx;
	matrix_table_t* trans_table;
	string_index_t* label_key_idx;
	int** label_data;
} *gb_context_t;

static int etf_short(model_t self,int group,int*src,TransitionCB cb,void*user_context){
    gb_context_t ctx=(gb_context_t)GBgetContext(self);
    int len=dm_ones_in_row(GBgetDMInfo(self), group);
    uint32_t src_no=(uint32_t)SIlookupC(ctx->trans_key_idx[group],(char*)src,len<<2);
    matrix_table_t mt=ctx->trans_table[group];
    if ((src_no)>=MTclusterCount(mt)) return 0;
    int K=MTclusterSize(mt,src_no);
    for(int i=0;i<K;i++){
        uint32_t row[3];
        MTclusterGetRow(mt,src_no,i,row);
        int *dst=(int*)SIgetC(ctx->trans_key_idx[group],(int)row[1],NULL);
        switch(ctx->edge_labels){
            case 0: {
                transition_info_t ti = GB_TI(NULL, group);
                cb(user_context,&ti,dst,NULL);
                break;
            }
            case 1: {
                int lbl=(int)row[2];
                transition_info_t ti = GB_TI(&lbl, group);
                cb(user_context,&ti,dst,NULL);
                break;
            }
            default: {
                transition_info_t ti = GB_TI((int*)SIgetC(ctx->label_idx,(int)row[2],NULL), group);
                cb(user_context,&ti,dst,NULL);
                break;
            }
        }
    }
    return K;
}

static int etf_state_short(model_t self,int label,int *state){
    gb_context_t ctx=(gb_context_t)GBgetContext(self);
    matrix_t *sl_info = GBgetStateLabelInfo(self);
    int len=dm_ones_in_row(sl_info,label);
    return ctx->label_data[label][SIlookupC(ctx->label_key_idx[label],(char*)state,len<<2)];
}

static int
etf_groups_of_edge(model_t model, int edge_no, int index, int** groups)
{
    gb_context_t ctx  = (gb_context_t)GBgetContext(model);
    int n_groups = dm_nrows(GBgetDMInfo(model));

    int count = 0;
    *groups = (int*) RTmalloc(sizeof(int) * n_groups);

    for (int i = 0; i < n_groups; i++) {
        matrix_table_t mt = ctx->trans_table[i];
        int K = MTgetCount(mt);
        for(int j = 0; j < K; j++){
            uint32_t row[3];
            MTgetRow(mt, j, row);
            switch(ctx->edge_labels){
                case 0: {
                    // JM: I think it should be so that if there are no edge labels,
                    // then no group can produce the edge
                    // and this method should return 0
                    // the caller can then produce a nice error message
                    // groups[count++] = i;
                    break;
                } case 1: {
                    if (index == (int) row[2]) *groups[count++] = i;
                    break;
                } default: {
                    int* tl = (int*) SIgetC(ctx->label_idx, (int) row[2], NULL);
                    if (tl[edge_no] == index) (*groups)[count++] = i;
                    break;
                }
            }
        }
    }
    RTrealloc(*groups, sizeof(int) * count);
    return count;
}

void
ETFloadGreyboxModel(model_t model, const char *name)
{
    gb_context_t ctx=(gb_context_t)RTmalloc(sizeof(struct grey_box_context));
    GBsetContext(model,ctx);
    etf_model_t etf=etf_parse_file(name);
    lts_type_t ltstype=etf_type(etf);
    int state_length=lts_type_get_state_length(ltstype);
    ctx->edge_labels=lts_type_get_edge_label_count(ltstype);
    if (ctx->edge_labels>1) {
        ctx->label_idx=SIcreate();
    } else {
        ctx->label_idx=NULL;
    }
    GBsetLTStype(model,ltstype);
    matrix_t* p_dm_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    matrix_t* p_dm_read_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    matrix_t* p_dm_write_info = (matrix_t*)RTmalloc(sizeof(matrix_t));
    dm_create(p_dm_info, etf_trans_section_count(etf), state_length);
    dm_create(p_dm_read_info, etf_trans_section_count(etf), state_length);
    dm_create(p_dm_write_info, etf_trans_section_count(etf), state_length);
    ctx->trans_key_idx=(string_index_t*)RTmalloc(dm_nrows(p_dm_info)*sizeof(string_index_t));
    ctx->trans_table=(matrix_table_t*)RTmalloc(dm_nrows(p_dm_info)*sizeof(matrix_table_t));
    for(int i=0; i < dm_nrows(p_dm_info); i++) {
        Warning(infoLong,"parsing table %d",i);
        etf_rel_t trans=etf_trans_section(etf,i);
        int used[state_length];
        int src[state_length];
        int dst[state_length];
        int lbl[ctx->edge_labels];
        int proj[state_length];
        ETFrelIterate(trans);
        if (!ETFrelNext(trans,src,dst,lbl)){
            Abort("unexpected empty transition section");
        }
        int len=0;
        for(int j=0;j<state_length;j++){
            if (src[j]) {
                proj[len]=j;
                Warning(debug,"pi[%d]=%d",len,proj[len]);
                len++;
                dm_set(p_dm_info, i, j);
                used[j]=1;
            } else {
                used[j]=0;
            }
        }
        Warning(infoLong,"length is %d",len);
        ctx->trans_key_idx[i]=SIcreate();
        ctx->trans_table[i]=MTcreate(3);
        int src_short[len];
        int dst_short[len];
        uint32_t row[3];
        do {
            /*
             * If an element is non-zero, we always consider it a read. If the
             * value is changed for at least one transition in a group then
             * we also consider it a write. Note that this could be slightly
             * optimized by omitting those elements from read where the value
             * varies over all possible inputs.
             */
            for(int k=0;k<state_length;k++) {
                if (src[k] != 0) {
                    dm_set(p_dm_read_info, i, k);
                    if (src[k] != dst[k]) dm_set(p_dm_write_info, i, k);
                }
            }
            for(int k=0;k<state_length;k++) {
                if(used[k]?(src[k]==0):(src[k]!=0)){
                    Abort("inconsistent section in src vector");
                }
            }
            for(int k=0;k<len;k++) src_short[k]=src[proj[k]]-1;
            for(int k=0;k<state_length;k++) {
                if(used[k]?(dst[k]==0):(dst[k]!=0)){
                    Abort("inconsistent section in dst vector");
                }
            }
            for(int k=0;k<len;k++) dst_short[k]=dst[proj[k]]-1;
            row[0]=(uint32_t)SIputC(ctx->trans_key_idx[i],(char*)src_short,len<<2);
            switch(ctx->edge_labels){
            case 0:
                row[2]=0;
                break;
            case 1:
                row[2]=(uint32_t)lbl[0];
                break;
            default:
                row[2]=(uint32_t)SIputC(ctx->label_idx,(char*)lbl,(ctx->edge_labels)<<2);
                break;
            }
            row[1]=(int32_t)SIputC(ctx->trans_key_idx[i],(char*)dst_short,len<<2);
            MTaddRow(ctx->trans_table[i],row);
        } while(ETFrelNext(trans,src,dst,lbl));
        Warning(infoLong,"table %d has %d states and %d transitions",
                i,SIgetCount(ctx->trans_key_idx[i]),ETFrelCount(trans));
        ETFrelDestroy(&trans);
        MTclusterBuild(ctx->trans_table[i],0,SIgetCount(ctx->trans_key_idx[i]));
    }
    GBsetDMInfo(model, p_dm_info);

    /*
     * Set these again when ETF supports read, write and copy.
       GBsetDMInfoRead(model, p_dm_read_info);
       GBsetDMInfoMustWrite(model, p_dm_write_info);
     */
    GBsetNextStateShort(model,etf_short);

    matrix_t *p_sl_info = RTmalloc(sizeof *p_sl_info);
    dm_create(p_sl_info, etf_map_section_count(etf), state_length);
    ctx->label_key_idx=(string_index_t*)RTmalloc(dm_nrows(p_sl_info)*sizeof(string_index_t));
    ctx->label_data=(int**)RTmalloc(dm_nrows(p_sl_info)*sizeof(int*));
    for(int i=0;i<dm_nrows(p_sl_info);i++){
        Warning(infoLong,"parsing map %d",i);
        etf_map_t map=etf_get_map(etf,i);
        int used[state_length];
        int state[state_length];
        int value;
        ETFmapIterate(map);
        if (!ETFmapNext(map,state,&value)){
            Abort("Unexpected empty map");
        }
        int len=0;
        for(int j=0;j<state_length;j++){
            if (state[j]) {
                used[len]=j;
                len++;
                dm_set(p_sl_info, i, j);
            }
        }
        int*proj=(int*)RTmalloc(len*sizeof(int));
        for(int j=0;j<len;j++) proj[j]=used[j];
        for(int j=0;j<state_length;j++) used[j]=state[j];
        string_index_t key_idx=SIcreate();
        int *data=(int*)RTmalloc(ETFmapCount(map)*sizeof(int));
        int key[len];
        do {
            for(int k=0;k<state_length;k++) {
                if(used[k]?(state[k]==0):(state[k]!=0)){
                    Abort("inconsistent map section");
                }
            }
            for(int k=0;k<len;k++) key[k]=state[proj[k]]-1;
            data[SIputC(key_idx,(char*)key,len<<2)]=value;
        } while(ETFmapNext(map,state,&value));
        ctx->label_key_idx[i]=key_idx;
        ctx->label_data[i]=data;
    }
    GBsetStateLabelInfo(model, p_sl_info);
    GBsetStateLabelShort(model,etf_state_short);
    GBsetGroupsOfEdge(model,etf_groups_of_edge);

    int type_count=lts_type_get_type_count(ltstype);
    for(int i=0;i<type_count;i++){
        Warning(infoLong,"Setting values for type %d (%s)",i,lts_type_get_type(ltstype,i));
        int count=etf_get_value_count(etf,i);
        for(int j=0;j<count;j++){
            pins_chunk_put_at (model,i,etf_get_value(etf,i,j),j);
        }
    }

    int state[state_length];
    etf_get_initial(etf,state);
    GBsetInitialState(model,state);
}

