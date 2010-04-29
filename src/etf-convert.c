#include <config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <runtime.h>
#include <hre-main.h>
#include <etf-util.h>
#include <popt.h>

static int pv_count;
static char* pvars=NULL;
static char** pvar_name=NULL;
static int*   pvar_idx=NULL;

static  struct poptOption options[] = {
    { "pvars" , 0 , POPT_ARG_STRING , &pvars , 0 , "list of independent variables" , NULL },
	POPT_TABLEEND
};

static void pvar_slice(lts_type_t ltstype){
    if (pvars==NULL) {
        pv_count=0;
        return;
    }
    int N=lts_type_get_state_length(ltstype);
    char *tmp=pvars;
    pv_count=0;
    while(tmp){
        pv_count++;
        int i=0;
        while(tmp[i]&&tmp[i]!=' ') {
            i++;
        }
        if (tmp[i]) {
            tmp[i]=0;
            tmp=&tmp[i+1];
        } else {
            tmp=NULL;
        }
    }
    pvar_name=RTmalloc(pv_count*sizeof(char*));
    pvar_idx=RTmalloc(pv_count*sizeof(int));
    tmp=pvars;
    Warning(info,"state length is %d",N);
    for(int j=0;j<N;j++) Warning(info,"state %d is %s",j,
        lts_type_get_state_name(ltstype,j));
    for(int i=0;i<pv_count;i++){
        pvar_name[i]=tmp;
        pvar_idx[i]=-1;
        for(int j=0;j<N;j++){
            if (strcmp(tmp,lts_type_get_state_name(ltstype,j))!=0) continue;
            pvar_idx[i]=j;
            break;
        }
        tmp=tmp+strlen(tmp)+1;
    }
    for(int i=0;i<pv_count;i++){
        Warning(info,"variable %d is %s, idx %d",i,pvar_name[i],pvar_idx[i]);
    }
}

static int analyze_rel(etf_rel_t trans,int N,int K,int*status,int*max){
    int transitions=0;
    int src[N];
    int dst[N];
    int lbl[K];
    for(int i=0;i<N;i++) status[i]=0;
    ETFrelIterate(trans);
    while(ETFrelNext(trans,src,dst,lbl)){
        transitions++;
        for(int j=0;j<N;j++){
            if (max) {
                if (src[j]>max[j]) max[j]=src[j]-1;
                if (dst[j]>max[j]) max[j]=dst[j]-1;
            }
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
    pvar_slice(etf_type(model));
    int g_count[pv_count+1];
    for(int i=0;i<=pv_count;i++){
        g_count[i]=0;
    }
    int max[N];
    etf_get_initial(model,max);
    if (pv_count) {
        for(int i=0;i<G;i++) {
            etf_rel_t trans=etf_trans_section(model,i);
            int status[N];
            int count=analyze_rel(trans,N,K,status,max);
            owner[i]=-1;
            if(count==0) continue;
            for(int j=0;j<N;j++){
                if(status[j]&0x2){
                    for(int k=0;k<pv_count;k++){
                        if (pvar_idx[k]==j){
                            if (owner[i]==-1){
                                owner[i]=k;
                            } else {
                                Abort("group %d belongs to two processes",i);
                            }
                        }
                    }
                }
            }
            if (owner[i]==-1) owner[i]=pv_count;
            g_count[owner[i]]++;
            Warning(info,"group %d belongs to proc %d",i,owner[i]);
        }
    } else {
        for(int i=0;i<G;i++) {
            owner[i]=0;
            g_count[owner[i]]++;
        }
    }
    
    FILE* dve=fopen(name,"w");
    int initial_state[N];
    etf_get_initial(model,initial_state);
    fprintf(dve,"int x[%d] = {",N);
    for(int i=0;i<N;i++){
        fprintf(dve,"%s%d",i?",":"",initial_state[i]);
    }
    fprintf(dve,"};\n");
    for(int p=0;p<=pv_count;p++){
        if(g_count[p]==0) {
            continue;
        }
        Warning(info,"generating process %d",p);
        fprintf(dve,"process P%d {\n",p);
        fprintf(dve,"  state s0");
        if (p<pv_count){
            for(int i=1;i<=max[pvar_idx[p]];i++){
                fprintf(dve,",s%d",i);
            }
        }
        fprintf(dve,";\n");
        fprintf(dve,"  init s%d;\n",p<pv_count?initial_state[p]:0);
        fprintf(dve,"  trans\n");
        int transitions=0;
        for(int section=0;section<etf_trans_section_count(model);section++){
            if (owner[section]!=p) continue;
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
                if (p<pv_count){
                    fprintf(dve,"    s%d -> s%d { guard ",
                            src[pvar_idx[p]]-1,dst[pvar_idx[p]]-1);
                } else {
                    fprintf(dve,"    s0 -> s0 { guard ");
                }
                for(int j=0;j<N;j++){
                    if (src[j] && (p==pv_count || pvar_idx[p]!=j)) {
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
            Warning(info,"process %d has %d transitions",p,transitions);
        } else {
            Fatal(1,error,"model without transitions");
        }
        fprintf(dve,"}\n");
    }
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
        if (0==analyze_rel(trans,N,K,status,NULL)){
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

