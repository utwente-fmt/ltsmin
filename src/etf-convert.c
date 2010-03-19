#include <config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <runtime.h>
#include <hre-main.h>
#include <etf-util.h>
#include <popt.h>

static char* pvars=NULL;

static  struct poptOption options[] = {
    { "pvars" , 0 , POPT_ARG_STRING , &pvars , 0 , "list of independent variables" , NULL },
	POPT_TABLEEND
};

static int analyze_rel(etf_rel_t trans,int N,int K,int*status){
    int transitions=0;
    int src[N];
    int dst[N];
    int lbl[K];
    for(int i=0;i<N;i++) status[i]=0;
    ETFrelIterate(trans);
    while(ETFrelNext(trans,src,dst,lbl)){
        transitions++;
        for(int j=0;j<N;j++){
            if (src[j]) {
                if (!dst[j]) Abort("inconsistent ETF");
                if (src[j]==dst[j]) {
                    status[j]|=1; // read
                } else {
                    status[j]|=2; // change
                }
            } else {
                if (dst[j]) Abort("inconsistent ETF");
            }
        }
    }
    return transitions;
}

#define ETF_BUF 4096

void dve_write(const char*name,etf_model_t model){
    int N=lts_type_get_state_length(etf_type(model));
    int K=lts_type_get_edge_label_count(etf_type(model));
    int G=etf_trans_section_count(model);
    int owner[G];
    int p_count;
    if (pvars) {
        
    } else {
        p_count=1;
        for(int i=0;i<G;i++) owner[i]=0;
    }
    
    FILE* dve=fopen(name,"w");
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

void dep_write(const char*name,etf_model_t model){
    FILE* dep=fopen(name,"w");

    lts_type_t ltstype=etf_type(model);
    int N=lts_type_get_state_length(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    
    for(int i=0;i<N;i++){
        fprintf(dep,"%s%s",lts_type_get_state_name(ltstype,i),(i==N-1)?"\n":" ");
    }
    for(int section=0;section<etf_trans_section_count(model);section++){
        etf_rel_t trans=etf_trans_section(model,section);
        int status[N];
        if (0==analyze_rel(trans,N,K,status)){
            continue;
        }       
        for (int i=0;i<N;i++){
            if (status[i]==1) { // read only
                fprintf(dep,"%s ",lts_type_get_state_name(ltstype,i));
            }
        }
        fprintf(dep,"--");
        for (int i=0;i<N;i++){
            if (status[i]>1) { // changed in at least one case
                fprintf(dep," %s",lts_type_get_state_name(ltstype,i));
            }
        }
        fprintf(dep,"\n");
    }
    fclose(dep);
}


int main(int argc,char *argv[]){
	char* files[2];
	RTinitPopt(&argc,&argv,options,2,2,files,NULL,"<input> <output>",
	"Convert ETF models.\n\n"
	"This tool knows about the following formats:\n"
	"etf Enumerated Table Format (read)\n"
	"dve DiVinE input language (write)\n"
	"dep DEPendencies of the model (write)\n");

	etf_model_t (*read_model)(const char *name)=NULL;
	void (*write_model)(const char *name,etf_model_t model)=NULL;
	int len;
	len=strlen(files[0]);
	if (len>4) {
		if (strcmp(files[0]+(len-4),".etf")==0) read_model=etf_parse_file;
	}
	if (read_model==NULL) Abort("extension of input file not recognized");
	len=strlen(files[1]);
	if (len>4) {
		if (strcmp(files[1]+(len-4),".dep")==0) write_model=dep_write;
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

