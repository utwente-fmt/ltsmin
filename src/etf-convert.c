#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "runtime.h"
#include "etf-util.h"

#define ETF_BUF 4096

void dve_write(const char*name,etf_model_t model){
	FILE* dve=fopen(name,"w");

	int N=lts_type_get_state_length(etf_type(model));
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
	treedbs_t pattern_db=etf_patterns(model);
	int transitions=0;
	for(int section=0;section<etf_trans_section_count(model);section++){
		treedbs_t trans=etf_trans_section(model,section);
		for(int i=TreeCount(trans)-1;i>=0;i--){
			if (transitions) {
				fprintf(dve,",\n");	
			}
			transitions++;
			fprintf(dve,"    s0 -> s0 { guard ");
			int step[2];
			int src[N];
			int dst[N];
			TreeUnfold(trans,i,step);
			TreeUnfold(pattern_db,step[0],src);
			TreeUnfold(pattern_db,step[1],dst);
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

int main(int argc,char *argv[]){
	RTinit(&argc,&argv);
        //take_options(options,&argc,argv);

	Warning(info,"parsing %s",argv[1]);
	etf_model_t model=etf_parse(argv[1]);

	Warning(info,"writing %s",argv[2]);
	dve_write(argv[2],model);
	Warning(info,"done");
	return 0;
}

