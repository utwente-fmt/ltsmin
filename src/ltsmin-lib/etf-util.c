#include <hre/config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define ETF_BUF 4096

#include <hre/user.h>
#include <ltsmin-lib/etf-internal.h>
#include <ltsmin-lib/etf-util.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>


etf_model_t ETFmodelCreate(){
	etf_model_t model=RT_NEW(struct etf_model_s);
	model->ltstype=lts_type_create();
	model->map_count=0;
	model->map_manager=create_manager(8);
	model->map=NULL;
	ADD_ARRAY(model->map_manager,model->map,etf_map_t);
	model->map_names=NULL;
	ADD_ARRAY(model->map_manager,model->map_names,char*);
	model->map_types=NULL;
	ADD_ARRAY(model->map_manager,model->map_types,char*);
	model->trans_count=0;
	model->trans=NULL;
	model->trans_manager=create_manager(8);
	ADD_ARRAY(model->trans_manager,model->trans,etf_rel_t);
	model->type_manager=create_manager(8);
	model->type_names=NULL;
	ADD_ARRAY(model->type_manager,model->type_names,char*);
	model->type_values=NULL;
	ADD_ARRAY(model->type_manager,model->type_values,string_index_t);
	return model;
}


int ETFgetType(etf_model_t model,const char*sort)
{
    int is_new;
    int type_no = lts_type_put_type(model->ltstype, sort, LTStypeEnum, &is_new);

    Warning(debug, "type %s %s (%d)", sort, is_new?"created":"present",type_no);

    if (is_new) {
        ensure_access(model->type_manager, type_no);
        model->type_names[type_no]  = strdup(sort);
        model->type_values[type_no] = SIcreate();
    }

    return type_no;
}

etf_map_t etf_get_map(etf_model_t model,int map){
	return model->map[map];
}

lts_type_t etf_type(etf_model_t model){
	return model->ltstype;
}

void etf_get_initial(etf_model_t model,int* state){
	int N=lts_type_get_state_length(model->ltstype);
	if (!model->initial_state){
		Abort("model has no initial state");
	}
	for(int i=0;i<N;i++) state[i]=model->initial_state[i];
}

int etf_trans_section_count(etf_model_t model){
	return model->trans_count;
}

int etf_map_section_count(etf_model_t model){
	return model->map_count;
}

etf_rel_t etf_trans_section(etf_model_t model,int section){
	return model->trans[section];
}

chunk etf_get_value(etf_model_t model,int type_no,int idx){
	int len;
	char *v=SIgetC(model->type_values[type_no],idx,&len);
	return chunk_ld(len,v);
}

int etf_get_value_count(etf_model_t model,int type_no){
	return SIgetCount(model->type_values[type_no]);
}

static int incr_ofs(int N,int *ofs,int*used,int max){
	int carry=1;
	int i=0;
	for(;carry && i<N;i++){
		if(used[i]){
			ofs[i]++;
			if(ofs[i]==max){
				ofs[i]=0;
			} else {
				carry=0;
			}
		}
	}
	return carry;
}
void etf_ode_add(etf_model_t model){
	if (model->trans_count) Abort("model already has transitions");
	int state_length=lts_type_get_state_length(model->ltstype);
	ensure_access(model->trans_manager,state_length);
	if (state_length != model->map_count){
		Abort("inconsistent map count");
	}
	int vcount[state_length];
	for(int i=0;i<state_length;i++){
		vcount[i]=SIgetCount(model->type_values[i]);
		Warning(debug,"var %d is %s with %d values",i,lts_type_get_state_name(model->ltstype,i),vcount[i]);
	}
	int is_new;
	int signtype=lts_type_add_type(model->ltstype,"sign",&is_new);
	if(is_new) Abort("model does not define sign type");
	Warning(debug,"sign type has %d values",SIgetCount(model->type_values[signtype]));
	for(int i=0;i<SIgetCount(model->type_values[signtype]);i++){
		Warning(debug,"sign[%d]=%s",i,SIget(model->type_values[signtype],i));
	}
	int neg=SIlookupC(model->type_values[signtype],"-",1);
	int zero=SIlookupC(model->type_values[signtype],"0",1);
	int pos=SIlookupC(model->type_values[signtype],"+",1);
	Warning(debug,"signs (-,0,+) are %d %d %d",neg,zero,pos);
	int actiontype=ETFgetType(model,"action");
	lts_type_set_edge_label_count(model->ltstype,1);
	lts_type_set_edge_label_type(model->ltstype,0,"action");
	for(int i=0;i<state_length;i++){
		Warning(debug,"parsing map %d",i);
		etf_map_t map=model->map[i];
		int used[state_length];
		int state[state_length];
		int value;
		ETFmapIterate(map);
		if (!ETFmapNext(map,used,&value)){
			Abort("Unexpected empty map");
		}
		while(ETFmapNext(map,state,&value)){
			for(int k=0;k<state_length;k++) {
				if(used[k]?(state[k]==0):(state[k]!=0)){
					Abort("inconsistent map section");
				}
			}
		}
		Warning(debug,"map is consistent");
		char var[1024];
		{
			int len=strlen(model->map_names[i]);
			for(int j=1;j<len-2;j++) {
				var[j-1]=model->map_names[i][j];
				var[j]=0;
			}
		}
		int varidx=0;
		while(strcmp(lts_type_get_state_name(model->ltstype,varidx),var)&&varidx<state_length) varidx++;
		if(varidx>=state_length){
			Abort("variable %s is not a state variable",var);
		} else {
			Warning(debug,"variable %s has index %d",var,varidx);
		}
		etf_rel_t trans=ETFrelCreate(state_length,1);
		int src[state_length];
		int dst[state_length];
		int lbl[1];
		int ofs[state_length];
		int ofs_used[state_length];
		ETFmapIterate(map);
		while(ETFmapNext(map,state,&value)){
			for(int k=0;k<state_length;k++){
				ofs[k]=0;
				ofs_used[k]=used[k]&&(k!=varidx);
			}
			do {
				int valid=1;
				for(int k=0;k<state_length;k++){
					src[k]=state[k]-ofs[k];
					if (ofs_used[k]) valid=valid && src[k];
					dst[k]=state[k]-ofs[k];			
				}
				if (!valid) continue;
				int first=used[varidx]?src[varidx]:1;
				int last=used[varidx]?src[varidx]:(vcount[varidx]-1);
				if (value==zero) continue;
				if (value!=neg && value!=pos) {
					Abort("unexpected sign: %d",value);
				}
				char label[1024];
				sprintf(label,"%s%s",var,(value==neg)?"-":"+");
				char tmp_data[ETF_BUF];
				chunk tmp_chunk=chunk_ld(ETF_BUF,tmp_data);
				string2chunk(label,&tmp_chunk);
				lbl[0]=SIputC(model->type_values[actiontype],tmp_chunk.data ,tmp_chunk.len );
				for(int k=first;k<=last;k++){
					if (value==neg && k==1) continue;
					if (value==pos && k==(vcount[varidx]-1)) continue;
					src[varidx]=k;
					dst[varidx]=(value==neg)?(k-1):(k+1);
					// Debug code
					//for(int l=0;l<state_length;l++){
					//	if (src[l]) printf("%d/%d ",src[l]-1,dst[l]-1);
					//	else printf("* ");
					//}
					//printf("%s\n",label);
					ETFrelAdd(trans,src,dst,lbl);
				}
			} while (!incr_ofs(state_length,ofs,ofs_used,2));
		}
		if (ETFrelCount(trans)){
			Warning(debug,"adding trans section with %d entries",ETFrelCount(trans));
			ensure_access(model->trans_manager,model->trans_count);
			model->trans[model->trans_count]=trans;
			model->trans_count++;
		} else {
			Warning(debug,"skipping empty trans section");
		}
	}
}

