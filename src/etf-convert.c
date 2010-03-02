#include <config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <runtime.h>
#include <hre-main.h>
#include <etf-util.h>
#include <popt.h>


static  struct poptOption options[] = {
	POPT_TABLEEND
};


#define ETF_BUF 4096

void dve_write(const char*name,etf_model_t model){
	FILE* dve=fopen(name,"w");

	int N=lts_type_get_state_length(etf_type(model));
	int K=lts_type_get_edge_label_count(etf_type(model));
	int initial_state[N];
	etf_get_initial(model,initial_state);

	fprintf(dve,"int x[%d] = {",N);
	for(int i=0;i<N;i++){
		fprintf(dve,"%s%d",i?",":"",initial_state[i]);
	}
	fprintf(dve,"};\n");
	fprintf(dve,"process P {\n");
	fprintf(dve,"  state s0;\n");
	fprintf(dve,"  init s0;\n");
	fprintf(dve,"  trans\n");
	int transitions=0;
	for(int section=0;section<etf_trans_section_count(model);section++){
		etf_rel_t trans=etf_trans_section(model,section);
		int src[N];
		int dst[N];
		int lbl[K];
		ETFrelIterate(trans);
		while(ETFrelNext(trans,src,dst,lbl)){
			if (transitions) {
				fprintf(dve,",\n");	
			}
			transitions++;
			fprintf(dve,"    s0 -> s0 { guard ");
			for(int j=0;j<N;j++){
				if (src[j]) {
					fprintf(dve,"x[%d]==%d && ",j,src[j]-1);
				}
			}
			fprintf(dve," 1 ; ");
			int first=1;
			for(int j=0;j<N;j++){
				if (dst[j]) {
					if (!first) {
						fprintf(dve,", ");
					} else {
						fprintf(dve,"effect ");
						first=0;
					}
					fprintf(dve,"x[%d]=%d",j,dst[j]-1);
				}
			}
			if(!first) {
				fprintf(dve,"; ");
			}
			fprintf(dve,"}");
		}
	}
	if (transitions) {
		fprintf(dve,";\n");
		Warning(info,"model has %d transitions",transitions);
	} else {
		Fatal(1,error,"model without transitions");
	}
	fprintf(dve,"}\n");
	fprintf(dve,"system async;\n");
	fclose(dve);	
}

void btf_write(const char*name,etf_model_t model){
	Warning(info,"faking write to %s",name);
	lts_type_t ltstype=etf_type(model);
	int N=lts_type_get_state_length(ltstype);
	int K=lts_type_get_edge_label_count(ltstype);
	int L=lts_type_get_state_label_count(ltstype);
	
}

int main(int argc,char *argv[]){
	char* files[2];
	RTinitPopt(&argc,&argv,options,2,2,files,NULL,"<input> <output>",
	"Convert ETF models from ETF/BTF to ETF/BTF/DVE");

	etf_model_t (*read_model)(const char *name)=NULL;
	void (*write_model)(const char *name,etf_model_t model)=NULL;
	int len;
	len=strlen(files[0]);
	if (len>4) {
		if (strcmp(files[0]+(len-4),".etf")==0) read_model=etf_parse_file;
		if (strcmp(files[0]+(len-4),".btf")==0) Abort("BTF reader TODO");
	}
	if (read_model==NULL) Abort("extension of input file not recognized");
	len=strlen(files[1]);
	if (len>4) {
		if (strcmp(files[1]+(len-4),".etf")==0) Abort("ETF writer TODO");
		if (strcmp(files[1]+(len-4),".btf")==0) write_model=btf_write;
		if (strcmp(files[1]+(len-4),".dve")==0) write_model=dve_write;
	}
	if (write_model==NULL) Abort("extension of input file not recognized");

	Warning(info,"reading %s",files[0]);
	etf_model_t model=read_model(files[0]);
	Warning(info,"writing %s",files[1]);
	write_model(files[1],model);
	Warning(info,"done");
	return 0;
}

