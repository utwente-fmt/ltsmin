// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <ctype.h>
#include <popt.h>
#include <stdio.h>
#include <string.h>

#include <hre/user.h>
#include <ltsmin-lib/etf-util.h>
#include <ltsmin-lib/ltsmin-syntax.h>

static int pv_count;
static char* pvars=NULL;
static char** pvar_name=NULL;
static int*   pvar_idx=NULL;
static int output_bytes=0;
static int constelm=0;

static  struct poptOption options[] = {
    { "pvars" , 0 , POPT_ARG_STRING , &pvars , 0 , "list of independent variables" , NULL },
    { "byte" , 0 , POPT_ARG_NONE , &output_bytes , 0 , "output bytes instead of ints" , NULL },
    { "ce" , 0 , POPT_ARG_NONE , &constelm , 1 , "eliminate constants while writing ETF " , NULL },
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

static int analyze_rel_add(etf_rel_t trans,int N,int K,int*status,int*max){
    int transitions=0;
    int src[N];
    int dst[N];
    int lbl[K];
    ETFrelIterate(trans);
    while(ETFrelNext(trans,src,dst,lbl)){
        transitions++;
        for(int j=0;j<N;j++){
            if (max) {
                if (src[j]-1>max[j]) max[j]=src[j]-1;
                if (dst[j]-1>max[j]) max[j]=dst[j]-1;
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

static int analyze_rel(etf_rel_t trans,int N,int K,int*status,int*max){
    for(int i=0;i<N;i++) status[i]=0;
    return analyze_rel_add(trans,N,K,status,max);
}

/**
 * Turns a string into a valid identifier.
 * When the string is "-delimited, it strips the " delimiters
 * Replaces all characters not in [_A-Za-z0-9] with '_'
 * If the string starts with a number, prepends a _
 */
char * sanitize_ID(const char *src, size_t dst_size, char *dst) {
    unsigned int out=0;
    int quote_mode=0;
    for (int i=0; src[i] && out<dst_size-1; i++) {
	if (i==0 && src[i] == '"') {
	    quote_mode=1;
	    continue;
	}
	if (src[i] == '"' && quote_mode) break;
	// FIXME: there's probably a better way
	if (src[i] == '_' || (src[i] >= '0' && src[i] <= '9') || (src[i] >= 'A' && src[i] <= 'Z') || (src[i] >= 'a' && src[i] <= 'z')) {
	    if (out==0 && src[i] >= '0' && src[i] <= '9') {
		dst[out++] = '_';
	    }
	    dst[out++] = src[i];
	} else {
	    dst[out++] = '_';
	}
    }
    dst[out++] = 0;
    return dst;
}

char ***sanitized_types=NULL;
char **sanitized_variables=NULL;

/*
 * Sanitizes and caches all type symbols of an etf_model_t. Should only be
 * called once.
 */
void sanitize_types(etf_model_t model) {
    lts_type_t ltstype = etf_type(model);
    int n_types = lts_type_get_type_count(ltstype);
    sanitized_types = (char***)RTmallocZero(n_types*sizeof(char**));
    for (int i=0;i<n_types;i++) {
	int v_count = etf_get_value_count(model, i);
	if (v_count) {
	    sanitized_types[i] = (char**)RTmalloc(v_count*sizeof(char*));
	    for (int j=0;j<v_count;j++) {
		chunk ch = etf_get_value(model, i, j);
		char tmp[ch.len+1];
		strncpy(tmp,ch.data,ch.len);
		tmp[ch.len] = '\0';
		int len = ch.len+2;
		sanitized_types[i][j] = (char*)RTmalloc(len);
		sanitize_ID(tmp,len,sanitized_types[i][j]);
		Debug("Sanitizing type %s to %s", tmp, sanitized_types[i][j]);
	    }
	}
    }
}

/*
 * Sanitizes and caches all the variable names of an lts_type_t. Should only be
 * called once.
 */
void sanitize_variables(lts_type_t ltstype) {
    int n_vars = lts_type_get_state_length(ltstype);
    sanitized_variables = (char**)RTmalloc(n_vars*sizeof(char*));
    for (int i=0;i<n_vars;i++) {
	char *name = lts_type_get_state_name(ltstype,i);
	int len = strlen(name)+2;
	sanitized_variables[i] = (char*)RTmalloc(len);
	sanitize_ID(name,len,sanitized_variables[i]);
	Debug("Sanitizing variable %s to %s", name, sanitized_variables[i]);
    }
}

char **state_names=NULL;
int n_state_names=0;

/*
 * Returns a cached version of a state number (e.g.: s20). Used internally by
 * etf_state_value().
 */
char * get_state_name(int idx) {
    while (idx >= n_state_names) {
	state_names = (char**)RTrealloc(state_names,(n_state_names+10)*sizeof(char*));
	char tmp[20];
	for (int i=n_state_names;i<n_state_names+10;i++) {
	    snprintf(tmp,20,"s%d",i);
	    state_names[i]=HREstrdup(tmp);
	}
	n_state_names += 10;
    }
    return state_names[idx];
}

/*
 * Returns a state name identifier. Uses the symbolic name if it exists,
 * otherwise it will return the value with an "s" in front.
 */
char * etf_state_value(etf_model_t model, int state, int value) {
    if (sanitized_types==NULL)
	sanitize_types(model);
    int type_no = lts_type_get_state_typeno(etf_type(model), state);
    int v_count = etf_get_value_count(model, type_no);
    if (v_count > value) {
	return sanitized_types[type_no][value];
    } else {
	return get_state_name(value);
    }
}

/*
 * Returns a sanitized version of the variable name.
 */
char * lts_variable_name(lts_type_t ltstype, int idx) {
    if (sanitized_variables==NULL)
	sanitize_variables(ltstype);
    return sanitized_variables[idx];
}

#define VAR_TYPE_UNKNOWN 0
#define VAR_TYPE_STATE 1
#define VAR_TYPE_LOCAL 2
#define VAR_TYPE_GLOBAL 3

void dve_write(const char*name,etf_model_t model){
    lts_type_t ltstype=etf_type(model);
    int N=lts_type_get_state_length(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    int G=etf_trans_section_count(model);
    // mapping section => pvar
    int owner[G];
    // default ownership if no state variable is written to
    // this is so that self-transitions on state variables go in their own process
    int defowner[G];
    // mapping variable => type
    int types[N];
    pvar_slice(ltstype);
    // count of sections per pvar
    int g_count[pv_count+1];
    for(int i=0;i<=pv_count;i++){
        g_count[i]=0;
    }

    /* Analyze sections: find out which sections belong to which pvar */
    // maximum value per variable
    int max[N];
    etf_get_initial(model,max);
    if (pv_count) {
        for(int i=0;i<G;i++) {
            etf_rel_t trans=etf_trans_section(model,i);
            int status[N];
            int count=analyze_rel(trans,N,K,status,max);
            owner[i]=-1;
            defowner[i]=-1;
            if(count==0) continue;
            for(int j=0;j<N;j++){
		for(int k=0;k<pv_count;k++){
		    if (pvar_idx[k]==j){
			if(status[j]&0x2){
			    if (owner[i]==-1){
				owner[i]=k;
			    } else {
				Abort("group %d belongs to two processes",i);
			    }
			} else if (status[j]&0x1) {
			    if (defowner[i]==-1) {
				defowner[i]=k;
			    } else {
				defowner[i]=pv_count;
			    }
			}
		    }
		}
            }
	    if (owner[i]==-1) owner[i]=defowner[i];
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

    /* Analyze variables: find out what type each variable is */
    // pvar with at least one nonempty section: state variable
    // always same owner: local variable
    // otherwise: global
    int var_owner[N];
    for (int i=0;i<N;i++) {
    	types[i]=VAR_TYPE_UNKNOWN;
	var_owner[i]=-1;
    }
    for (int i=0;i<pv_count;i++) {
    	types[pvar_idx[i]] = g_count[i]?VAR_TYPE_STATE:VAR_TYPE_GLOBAL;
    }
    if (pv_count) {
	for (int i=0;i<G;i++) {
            etf_rel_t trans=etf_trans_section(model,i);
            int status[N];
            int count=analyze_rel(trans,N,K,status,NULL);
	    if (!count) continue;
	    for (int j=0;j<N;j++) {
		if (types[j]==VAR_TYPE_STATE) continue;
		if (status[j]) {
		    if (var_owner[j]==-1) {
			var_owner[j]=owner[i];
			types[j]=VAR_TYPE_LOCAL;
		    } else {
			if (var_owner[j]!=owner[i]) {
			    types[j]=VAR_TYPE_GLOBAL;
			}
		    }
		}
	    }
	}
	for (int i=0;i<N;i++) {
	    char *typename;
	    switch(types[i]) {
		case VAR_TYPE_STATE: typename="state"; break;
		case VAR_TYPE_LOCAL: typename="local"; break;
		case VAR_TYPE_GLOBAL: typename="global"; break;
		default: typename="unknown"; break;
	    }
	    Warning(info,"variable %s is %s",lts_variable_name(ltstype,i),typename);
	}
    } else {
	for(int i=0;i<N;i++) {
	    types[i] = VAR_TYPE_GLOBAL;
	}
    }

    char *outtype;
    if (output_bytes)
    	outtype = "byte";
    else
	outtype = "int";

    FILE* dve=fopen(name,"w");
    int initial_state[N];
    etf_get_initial(model,initial_state);
    // Global variables
    for(int i=0;i<N;i++) {
	if (types[i] == VAR_TYPE_GLOBAL) {
	    fprintf(dve,"%s %s = %d;\n",outtype,lts_variable_name(ltstype,i),initial_state[i]);
	}
    }

    // Processes
    for(int p=0;p<=pv_count;p++){
	int idx=-1;
	if (p<pv_count) {
	    idx=pvar_idx[p];
	}
        if(g_count[p]==0) {
            continue;
        }
	// process
        Warning(info,"generating process %d",p);
	if (p<pv_count) {
	    fprintf(dve,"process %s {\n",lts_variable_name(ltstype,pvar_idx[p]));
	} else {
	    fprintf(dve,"process __bucket {\n");
	}
	// local variables
	for(int i=0;i<N;i++) {
	    if (types[i] == VAR_TYPE_LOCAL && var_owner[i] == p) {
		fprintf(dve,"  %s %s = %d;\n",outtype,lts_variable_name(ltstype,i),initial_state[i]);
	    }
	}
	// states
	if (p<pv_count) {
	    fprintf(dve,"  state ");
            for(int i=0;i<=max[idx];i++){
                fprintf(dve,"%s%s",i==0?"":",",etf_state_value(model,idx,i));
            }
	} else {
	    fprintf(dve,"  state s0");
	}
        fprintf(dve,";\n");
        fprintf(dve,"  init %s;\n",p<pv_count?etf_state_value(model,idx,initial_state[pvar_idx[p]]):"s0");
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
		    fprintf(dve,"    %s",
			    etf_state_value(model,idx,src[idx]-1));
		    fprintf(dve," -> %s { ",
			    etf_state_value(model,idx,dst[idx]-1));
                } else {
                    fprintf(dve,"    s0 -> s0 { ");
                }
		int first;
		first=1;
                for(int j=0;j<N;j++){
		    if (src[j] && (p==pv_count || j!=idx)) {
			if (!first) {
			    fprintf(dve," && ");   
			} else {
			    fprintf(dve,"guard ");   
			    first=0;
			}
			if (types[j] == VAR_TYPE_STATE) {
			    fprintf(dve,"%s.%s",
				    lts_variable_name(ltstype,j),
				    etf_state_value(model,j,src[j]-1));
			} else {
			    fprintf(dve,"%s==%d",lts_variable_name(ltstype,j),src[j]-1);
			}
		    }
                }
                if (!first) {
                    fprintf(dve,"; ");
                }
                first=1;
                for(int j=0;j<N;j++){
                    if (dst[j] && dst[j]!=src[j] && (p==pv_count || j!=idx)) {
                        if (!first) {
                            fprintf(dve,", ");
                        } else {
                            fprintf(dve,"effect ");
                            first=0;
                        }
                        fprintf(dve,"%s=%d",lts_variable_name(ltstype,j),dst[j]-1);
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
    	int transitions=analyze_rel(trans,N,K,status,NULL);
        if (transitions==0){
            continue;
        }
	    fprintf(dep,"%d ", transitions);
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

void etf_write(const char*name,etf_model_t model){
    FILE* out=fopen(name,"w");

    lts_type_t ltstype=etf_type(model);
    int N=lts_type_get_state_length(ltstype);
    int K=lts_type_get_edge_label_count(ltstype);
    int init[N];
    int global[N];
    int keep[N];
    if (constelm){
        int count;
        for(int i=0;i<N;i++){
            global[i]=0;
        }
        for(int section=0;section<etf_trans_section_count(model);section++){
            etf_rel_t trans=etf_trans_section(model,section);
        	analyze_rel_add(trans,N,K,global,NULL);
        }
        count=0;
        for(int i=0;i<N;i++){
            if(global[i]<=1){
                count++;
                keep[i]=0;
            } else {
                keep[i]=1;
            }
        }
        Print(info,"eliminating %d constant(s)",count);
    } else {
        for(int i=0;i<N;i++){
            keep[i]=1;
        }
    }
    fprintf(out,"begin state\n");
    for(int i=0;i<N;i++){
        if (keep[i]) {
            fprintf(out," ");
            fprint_ltsmin_ident(out,lts_type_get_state_name(ltstype,i));
            fprintf(out,":");
            fprint_ltsmin_ident(out,lts_type_get_state_type(ltstype,i));
        }
    }
    fprintf(out,"\n");
    fprintf(out,"end state\n");
    fprintf(out,"begin edge\n");
    for(int i=0;i<K;i++){
        fprintf(out," ");
        fprint_ltsmin_ident(out,lts_type_get_edge_label_name(ltstype,i));
        fprintf(out,":");
        fprint_ltsmin_ident(out,lts_type_get_edge_label_type(ltstype,i));
    }
    fprintf(out,"\n");
    fprintf(out,"end edge\n");
    
    etf_get_initial(model,init);
    fprintf(out,"begin init\n");
    for(int i=0;i<N;i++){
        if (keep[i]) fprintf(out," %d",init[i]);
    }
    fprintf(out,"\n");
    fprintf(out,"end init\n");
    for(int section=0;section<etf_trans_section_count(model);section++){
        etf_rel_t trans=etf_trans_section(model,section);
        int src[N];
        int dst[N];
        int lbl[K];
		fprintf(out,"begin trans\n");
        ETFrelIterate(trans);
        while(ETFrelNext(trans,src,dst,lbl)){
            int valid=1;
            for(int j=0;j<N && valid;j++){
                if (keep[j] || src[j]==0) continue;
                if((src[j]-1)!=init[j]) valid=0;
            }
            if (!valid) continue;
            for(int j=0;j<N;j++){
                if (!keep[j]) continue;
                if (src[j]) {
                    fprintf(out," %d/%d",src[j]-1,dst[j]-1);
                } else {
                    fprintf(out," *");
                }
            }
            for(int j=0;j<K;j++){
                fprintf(out," %d",lbl[j]);
            }
            fprintf(out,"\n");
        }
		fprintf(out,"end trans\n");
    }
    fclose(out);
}


int main(int argc,char *argv[]){
	char* files[2];
    HREinitBegin(argv[0]);
    HREaddOptions(options,"Convert ETF models.\n\n"
                          "This tool knows about the following formats:\n"
                          "etf Enumerated Table Format (read)\n"
                          "dve DiVinE input language (write)\n"
                          "dep DEPendencies of the model (write)\n");
    HREinitStart(&argc,&argv,2,2,files,"<input> <output>");
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
		if (strcmp(files[1]+(len-4),".etf")==0) write_model=etf_write;
	}
	if (write_model==NULL) Abort("extension of input file not recognized");

	Warning(info,"reading %s",files[0]);
	etf_model_t model=read_model(files[0]);
	Warning(info,"writing %s",files[1]);
	write_model(files[1],model);
	Warning(info,"done");
	return 0;
}

