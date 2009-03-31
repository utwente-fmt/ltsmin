#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define ETF_BUF 4096

#include "runtime.h"
#include "etf-util.h"
#include "dynamic-array.h"
#include "stringindex.h"

struct etf_model_s {
	lts_type_t ltstype;
	int* initial_state;
	treedbs_t pattern_db;
	int map_count;
	treedbs_t* map;
	char** map_names;
	char** map_types;
	array_manager_t map_manager;
	int trans_count;
	treedbs_t* trans;
	array_manager_t trans_manager;
	array_manager_t type_manager;
	char** type_names;
	string_index_t* type_values;
};

static int ensure_type(etf_model_t model,const char*sort){
	int is_new;
	int type_no=lts_type_add_type(model->ltstype,sort,&is_new);
	Warning(debug,"type %s %s (%d)",sort,is_new?"created":"present",type_no);
	if (is_new) {
		ensure_access(model->type_manager,type_no);
		model->type_names[type_no]=strdup(sort);
		model->type_values[type_no]=SIcreate();
	}
	return type_no;
}

static inline int is_a_number(const char* v){
	int len=strlen(v);
	for(int i=0;i<len;i++) if (!isdigit(v[i])) return 0;
	return 1;
}

etf_model_t etf_parse(const char *file){
	etf_model_t model=(etf_model_t)RTmalloc(sizeof(struct etf_model_s));
	model->ltstype=lts_type_create();
	model->initial_state=NULL;

	FILE* etf=fopen(file,"r");
	char*line;
	char buf[ETF_BUF];


	do line=fgets(buf,ETF_BUF,etf); while (line[0]=='%');
	if (!strcmp("begin state",line)) { Fatal(1,error,"etf model must start with a state section"); }
	line=fgets(buf,ETF_BUF,etf);
	int N=0;
	for(int i=0;i<(int)strlen(line);i++){
		if (line[i]==':') N++;
		if (line[i]=='\n') {
			line[i]=0;
			break;
		}
	}
	Warning(debug,"state vector has length %d",N);
	lts_type_set_state_length(model->ltstype,N);
	model->pattern_db=TreeDBScreate(N);
	model->map_count=0;
	model->map_manager=create_manager(8);
	model->map=NULL;
	ADD_ARRAY(model->map_manager,model->map,treedbs_t);
	model->map_names=NULL;
	ADD_ARRAY(model->map_manager,model->map_names,char*);
	model->map_types=NULL;
	ADD_ARRAY(model->map_manager,model->map_types,char*);
	model->trans_count=0;
	model->trans=NULL;
	model->trans_manager=create_manager(8);
	ADD_ARRAY(model->trans_manager,model->trans,treedbs_t);
	model->type_manager=create_manager(8);
	model->type_names=NULL;
	ADD_ARRAY(model->type_manager,model->type_names,char*);
	model->type_values=NULL;
	ADD_ARRAY(model->type_manager,model->type_values,string_index_t);

	for(int i=0;i<N;i++){
		int j;
		while(isblank(line[0])) line+=1;
		char var[1024];
		for(j=0;line[j]!=':';j++){
			var[j]=line[j];
		}
		var[j]=0;
		line+=j+1;
		char sort[1024];
		for(j=0;line[j]!=0&&!isblank(line[j]);j++){
			sort[j]=line[j];
		}
		sort[j]=0;
		line+=j;
		Warning(debug,"position %d: var \"%s\" sort \"%s\"",i,var,sort);
		if (strcmp(var,"_")) lts_type_set_state_name(model->ltstype,i,var);
		if (strcmp(sort,"_")) {
			int typeno=ensure_type(model,sort);
			lts_type_set_state_typeno(model->ltstype,i,typeno);
		}
	}

	line=fgets(buf,ETF_BUF,etf);
	if (!strcmp("end state",line)) { Fatal(1,error,"expected end state"); }

	do line=fgets(buf,ETF_BUF,etf); while (line[0]=='%');
	if (!strcmp("begin edge",line)) { Fatal(1,error,"expected begin edge"); }
	line=fgets(buf,ETF_BUF,etf);
	int K=0;
	for(int i=0;i<(int)strlen(line);i++){
		if (line[i]==':') K++;
		if (line[i]=='\n') {
			line[i]=0;
			break;
		}
	}
	Warning(debug,"The model has %d edge labels",K);
	int edge_type_no[K];
	lts_type_set_edge_label_count(model->ltstype,K);
	for(int i=0;i<K;i++){
		int j;
		while(isblank(line[0])) line+=1;
		char var[1024];
		for(j=0;line[j]!=':';j++){
			var[j]=line[j];
		}
		var[j]=0;
		line+=j+1;
		char sort[1024];
		for(j=0;line[j]!=0&&!isblank(line[j]);j++){
			sort[j]=line[j];
		}
		sort[j]=0;
		line+=j;
		Warning(debug,"edge label %d: name \"%s\" sort \"%s\"",i,var,sort);
		lts_type_set_edge_label_name(model->ltstype,i,var);
		edge_type_no[i]=ensure_type(model,sort);
		lts_type_set_edge_label_type(model->ltstype,i,sort);
	}
	line=fgets(buf,ETF_BUF,etf);
	if (!strcmp("end edge",line)) { Fatal(1,error,"expected end state"); }

	for(;;){
		line=fgets(buf,ETF_BUF,etf);
		if (line==NULL && feof(etf)) break;
		int len=strlen(line);
		if (line[len-1]=='\n') {
			line[len-1]=0;
			len--;
		}
		if (len==0 || line[0]=='%') continue;
		if (!strcmp(line,"begin init")){
			if(model->initial_state){
				Fatal(1,error,"more than one init section");
			}
			model->initial_state=(int*)RTmalloc(N*sizeof(int));
			for(int i=0;i<N;i++){
				if (!fscanf(etf,"%s",buf)){
					Fatal(1,error,"unexpected end of file");
				}
				model->initial_state[i]=atoi(buf);
			}
			line=fgets(buf,ETF_BUF,etf);
			line=fgets(buf,ETF_BUF,etf);
			if (strcmp(line,"end init\n")) {
				Fatal(1,error,"expected end init");
			}
			//Warning(debug,"got initial state");
			continue;
		}
		if (!strcmp(line,"begin trans")) {
			treedbs_t trans=TreeDBScreate(2+K);
			for(;;){
				line=fgets(buf,ETF_BUF,etf);
				int len=strlen(line);
				if (line[len-1]=='\n') {
					line[len-1]=0;
					len--;
				}
				if (!strcmp(line,"end trans")) {
					//Warning(debug,"transition section ended");
					break;
				}
				int ptr=0;
				int src[N];
				int dst[N];
				for(int i=0;i<N;i++){
					while(isblank(line[ptr])) ptr++;
					if (line[ptr]=='*') {
						src[i]=0;
						dst[i]=0;
						ptr++;
						continue;
					}
					src[i]=atoi(line+ptr)+1;
					while(line[ptr]!='/') ptr++;
					ptr++;
					while(isblank(line[ptr])) ptr++;
					dst[i]=atoi(line+ptr)+1;
					while(!isblank(line[ptr])) ptr++;
				}
				int edge[2+K];
				edge[0]=TreeFold(model->pattern_db,src);
				edge[1]=TreeFold(model->pattern_db,dst);
				for(int i=0;i<K;i++){
					while(isblank(line[ptr])) ptr++;
					char*v=line+ptr;
					if (v[0]=='"') {
						for(ptr++;line[ptr]&&line[ptr]!='"';) ptr++;
						if(line[ptr]!='"') Fatal(1,error,"line ended in the middle of a string");
						ptr++;
					} else {
						while(line[ptr] && !isblank(line[ptr])) ptr++;
					}
					if (line[ptr]){
						line[ptr]=0;
						ptr++;
					}
					if (strlen(v)==0) Fatal(1,error,"missing value");
					if (is_a_number(v)){
						edge[2+i]=atoi(v);
					} else {
						if(line[ptr]){
							line[ptr]=0;
							ptr++;
						}
						char tmp_data[ETF_BUF];
						chunk tmp_chunk=chunk_ld(ETF_BUF,tmp_data);
						string2chunk(v,&tmp_chunk);
						edge[2+i]=SIputC(model->type_values[edge_type_no[i]],tmp_chunk.data ,tmp_chunk.len);
					}
				}
				TreeFold(trans,edge);
			}
			if (TreeCount(trans)){
				ensure_access(model->trans_manager,model->trans_count);
				model->trans[model->trans_count]=trans;
				model->trans_count++;
			} else {
				Warning(debug,"skipping empty trans section");
			}
			continue;
		}
		if (!strncmp(line,"begin map",9)){
			char name[1024];
			char sort[1024];
			char *tmp=strchr(line+9,':');
			if (!tmp) Fatal(1,error,"cannot get name and type from %s",line+9);
			tmp[0]=' ';
			if (sscanf(line+9,"%s %s",name,sort)!=2){
				Fatal(1,error,"cannot get name and type from %s",line+9);
			}
			ensure_access(model->map_manager,model->map_count);
			model->map_names[model->map_count]=strdup(name);
			model->map_types[model->map_count]=strdup(sort);
			int type_no=ensure_type(model,model->map_types[model->map_count]);
			Warning(debug,"map %s, type %s",name,model->map_types[model->map_count]);
			treedbs_t current_map=TreeDBScreate(N+1);
			for(;;){
				line=fgets(buf,ETF_BUF,etf);
				int len=strlen(line);
				if (line[len-1]=='\n') {
					line[len-1]=0;
					len--;
				}
				if (!strcmp(line,"end map")) {
					//Warning(debug,"map section ended");
					break;
				}
				int entry[N+1];
				int ptr=0;
				for(int i=0;i<N;i++){
					while(isblank(line[ptr])) ptr++;
					if (line[ptr]=='*') {
						entry[i]=0;
					} else {
						entry[i]=atoi(line+ptr)+1;
					}
					while(line[ptr] && !isblank(line[ptr])) ptr++;
				}

				while(isblank(line[ptr])) ptr++;
				char*v=line+ptr;
				if (v[0]=='"') {
					for(ptr++;line[ptr]&&line[ptr]!='"';) ptr++;
					if(line[ptr]!='"') Fatal(1,error,"line ended in the middle of a string");
					ptr++;
				} else {
					while(line[ptr] && !isblank(line[ptr])) ptr++;
				}
				if (line[ptr]){
					line[ptr]=0;
					ptr++;
				}
				if (strlen(v)==0) Fatal(1,error,"missing value");
				if (is_a_number(v)){
					entry[N]=atoi(v);
				} else {
					char tmp_data[ETF_BUF];
					chunk tmp_chunk=chunk_ld(ETF_BUF,tmp_data);
					string2chunk(v,&tmp_chunk);
					entry[N]=SIputC(model->type_values[type_no],tmp_chunk.data ,tmp_chunk.len);
				}
				TreeFold(current_map,entry);
			}
			model->map[model->map_count]=current_map;
			model->map_count++;
			continue;
		}
		if (!strncmp(line,"begin sort",10)){
			char sort[10];
			if (sscanf(line+10,"%s",sort)!=1){
				Fatal(1,error,"cannot get type from %s",line+10);
			}
			int type_no=ensure_type(model,sort);
			Warning(debug,"scanning values for sort %s",sort);
			int count=0;
			for(;;count++){
				line=fgets(buf,ETF_BUF,etf);
				int len=strlen(line);
				if (line[len-1]=='\n') {
					line[len-1]=0;
					len--;
				}
				if (!strcmp(line,"end sort")) {
					//Warning(debug,"sort section ended");
					break;
				}
				char tmp_data[ETF_BUF];
				chunk tmp_chunk=chunk_ld(ETF_BUF,tmp_data);
				string2chunk(line,&tmp_chunk);
				if (count!=SIputC(model->type_values[type_no],tmp_chunk.data ,tmp_chunk.len)){
					Warning(error,"inconsistent value indexing");
					Fatal(1,error,"put sort section(s) before the first direct use of the type");
				}
			}
			continue;
		}
		Fatal(1,error,"cannot understand %s",line);
	}
	fclose(etf);
	lts_type_set_state_label_count(model->ltstype,model->map_count);
	for(int i=0;i<model->map_count;i++){
		lts_type_set_state_label_name(model->ltstype,i,model->map_names[i]);
		lts_type_set_state_label_type(model->ltstype,i,model->map_types[i]);
	}
	if (model->trans_count==0){
		Warning(info,"ETF model has no transition sections. Assuming input is an ODE.");
		etf_ode_add(model);
	}
	Warning(debug,"ETF model has %d transition sections",model->trans_count);
	Warning(debug,"ETF model has %d map sections",model->map_count);
	Warning(debug,"ETF model has %d types",lts_type_get_type_count(model->ltstype));
	return model;	
}

treedbs_t etf_get_map(etf_model_t model,int map){
	return model->map[map];
}

lts_type_t etf_type(etf_model_t model){
	return model->ltstype;
}

void etf_get_initial(etf_model_t model,int* state){
	int N=lts_type_get_state_length(model->ltstype);
	if (!model->initial_state){
		Fatal(1,error,"model has no initial state");
	}
	for(int i=0;i<N;i++) state[i]=model->initial_state[i];
}

int etf_trans_section_count(etf_model_t model){
	return model->trans_count;
}

int etf_map_section_count(etf_model_t model){
	return model->map_count;
}

treedbs_t etf_patterns(etf_model_t model){
	return model->pattern_db;
}

treedbs_t etf_trans_section(etf_model_t model,int section){
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
	if (model->trans_count) Fatal(1,error,"model already has transitions");
	int state_length=lts_type_get_state_length(model->ltstype);
	ensure_access(model->trans_manager,state_length);
	if (state_length != model->map_count){
		Fatal(1,error,"inconsistent map count");
	}
	int vcount[state_length];
	for(int i=0;i<state_length;i++){
		vcount[i]=SIgetCount(model->type_values[i]);
		Warning(debug,"var %d is %s with %d values",i,lts_type_get_state_name(model->ltstype,i),vcount[i]);
	}
	int is_new;
	int signtype=lts_type_add_type(model->ltstype,"sign",&is_new);
	if(is_new) Fatal(1,error,"model does not define sign type");
	int neg=SIlookup(model->type_values[signtype],"-");
	int zero=SIlookup(model->type_values[signtype],"0");
	int pos=SIlookup(model->type_values[signtype],"+");
	int actiontype=ensure_type(model,"action");
	lts_type_set_edge_label_count(model->ltstype,1);
	lts_type_set_edge_label_type(model->ltstype,0,"action");
	for(int i=0;i<state_length;i++){
		Warning(debug,"parsing map %d",i);
		treedbs_t map=model->map[i];
		int used[state_length+1];
		TreeUnfold(map,0,used);
		int entry[state_length+1];
		for(int j=TreeCount(map)-1;j>=0;j--){
			TreeUnfold(map,j,entry);
			for(int k=0;k<state_length;k++) {
				if(used[k]?(entry[k]==0):(entry[k]!=0)){
					Fatal(1,error,"inconsistent map section");
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
			Fatal(1,error,"variable %s is not a state variable",var);
		} else {
			Warning(debug,"variable %s has index %d",var,varidx);
		}
		treedbs_t transdb=TreeDBScreate(3);
		int trans[3];
		int src[state_length];
		int dst[state_length];
		int ofs[state_length];
		int ofs_used[state_length];
		for(int j=TreeCount(map)-1;j>=0;j--){
			TreeUnfold(map,j,entry);
			for(int k=0;k<state_length;k++){
				ofs[k]=0;
				ofs_used[k]=used[k]&&(k!=varidx);
			}
			do {
				int valid=1;
				for(int k=0;k<state_length;k++){
					src[k]=entry[k]-ofs[k];
					if (ofs_used[k]) valid=valid && src[k];
					dst[k]=entry[k]-ofs[k];			
				}
				if (!valid) continue;
				int first=used[varidx]?src[varidx]:1;
				int last=used[varidx]?src[varidx]:(vcount[varidx]-1);
				if (entry[state_length]==zero) continue;
				if (entry[state_length]!=neg && entry[state_length]!=pos) {
					Fatal(1,error,"unexpected sign: %d",entry[state_length]);
				}
				char label[1024];
				sprintf(label,"%s%s",var,(entry[state_length]==neg)?"-":"+");
				char tmp_data[ETF_BUF];
				chunk tmp_chunk=chunk_ld(ETF_BUF,tmp_data);
				string2chunk(label,&tmp_chunk);
				trans[2]=SIputC(model->type_values[actiontype],tmp_chunk.data ,tmp_chunk.len );
				for(int k=first;k<=last;k++){
					if (entry[state_length]==neg && k==1) continue;
					if (entry[state_length]==pos && k==(vcount[varidx]-1)) continue;
					src[varidx]=k;
					dst[varidx]=(entry[state_length]==neg)?(k-1):(k+1);
					// Debug code
					//for(int l=0;l<state_length;l++){
					//	if (src[l]) printf("%d/%d ",src[l]-1,dst[l]-1);
					//	else printf("* ");
					//}
					//printf("%s\n",label);
					trans[0]=TreeFold(model->pattern_db,src);
					trans[1]=TreeFold(model->pattern_db,dst);
					TreeFold(transdb,trans);
				}
			} while (!incr_ofs(state_length,ofs,ofs_used,2));
		}
		if (TreeCount(transdb)){
			Warning(debug,"adding trans section with %d entries",TreeCount(transdb));
			ensure_access(model->trans_manager,model->trans_count);
			model->trans[model->trans_count]=transdb;
			model->trans_count++;
		} else {
			Warning(debug,"skipping empty trans section");
		}
	}
}

