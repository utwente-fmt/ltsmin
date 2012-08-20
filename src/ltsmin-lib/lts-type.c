// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <string.h>

#include <hre/user.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>

struct lts_type_s {
int state_length;
char** state_name;
int* state_type;
int state_label_count;
char** state_label_name;
int* state_label_type;
int edge_label_count;
char** edge_label_name;
int* edge_label_type;
string_index_t type_db;
data_format_t *type_format;
int *type_min;
int *type_max;
};

int lts_type_get_max(lts_type_t  t,int typeno){
    HREassert(t->type_format[typeno]==LTStypeRange, "Wrong type");
    return t->type_max[typeno];
}

int lts_type_get_min(lts_type_t  t,int typeno){
    HREassert(t->type_format[typeno]==LTStypeRange, "Wrong type");
    return t->type_min[typeno];
}

void lts_type_set_range(lts_type_t  t,int typeno,int min,int max){
    HREassert(t->type_format[typeno]==LTStypeRange, "Wrong type");
    t->type_min[typeno]=min;
    t->type_max[typeno]=max;
}

static const char* data_format_string(lts_type_t  t,int typeno){
    switch(t->type_format[typeno]){
    case LTStypeDirect: return "direct";
    case LTStypeRange: {
        char tmp[256];
        sprintf(tmp,"[%d,%d]",t->type_min[typeno],t->type_max[typeno]);
        return strdup(tmp);
    }
    case LTStypeChunk: return "chunk";
    case LTStypeEnum: return "enum";
    }
    Abort("illegal format value: %d",t->type_format[typeno]);
}

static const char* data_format_string_generic(data_format_t format){
    switch(format){
    case LTStypeDirect: return "direct";
    case LTStypeRange: return "[.,.]";
    case LTStypeChunk: return "chunk";
    case LTStypeEnum: return "enum";
    }
    Abort("illegal format value: %d",format);
}

lts_type_t lts_type_create(){
	lts_type_t ltstype=(lts_type_t)RTmalloc(sizeof(struct lts_type_s));
	ltstype->state_length=0;
	ltstype->state_name=NULL;
	ltstype->state_type=NULL;
	ltstype->state_label_count=0;
	ltstype->state_label_name=NULL;
	ltstype->state_label_type=NULL;
	ltstype->edge_label_count=0;
	ltstype->edge_label_name=NULL;
	ltstype->edge_label_type=NULL;
	ltstype->type_db=SIcreate();
	ltstype->type_format=NULL;
    ADD_ARRAY(SImanager(ltstype->type_db),ltstype->type_format,data_format_t);
	ltstype->type_min=NULL;
    ADD_ARRAY(SImanager(ltstype->type_db),ltstype->type_min,int);
	ltstype->type_max=NULL;
    ADD_ARRAY(SImanager(ltstype->type_db),ltstype->type_max,int);
	return ltstype;
}

#define STRDUP(s) (s==NULL)?NULL:strdup(s);

lts_type_t lts_type_clone(lts_type_t t0)
{
    int pi[t0->state_length];
    // build permutation
    for(int i=0; i < t0->state_length; i++)
        pi[i] = i;
    return lts_type_permute(t0, pi);
}

lts_type_t lts_type_permute(lts_type_t t0,int *pi){
    lts_type_t t=lts_type_create();
    
	// clone type numbering
	int N=SIgetCount(t0->type_db);
	ensure_access(SImanager(t0->type_db),N-1);
	for(int i=0;i<N;i++){
	    SIputAt(t->type_db,SIget(t0->type_db,i),i);
	    t->type_format[i]=t0->type_format[i];
	    t->type_min[i]=t0->type_min[i];
	    t->type_max[i]=t0->type_max[i];
	}
    // clone state signature
    lts_type_set_state_length(t,t0->state_length);
	for(int i=0;i<t0->state_length;i++){
	    t->state_name[i]=STRDUP(t0->state_name[pi[i]]);
		t->state_type[i]=t0->state_type[pi[i]];
	}
	// clone state label signature
	lts_type_set_state_label_count(t,t0->state_label_count);
	for(int i=0;i<t0->state_label_count;i++){
	    t->state_label_name[i]=STRDUP(t0->state_label_name[i]);
	    t->state_label_type[i]=t0->state_label_type[i];
	}
	// clone edge label signature
	t->edge_label_count=t0->edge_label_count;
	t->edge_label_name=(char**)RTmalloc(t0->edge_label_count*sizeof(char*));
	t->edge_label_type=(int*)RTmalloc(t0->edge_label_count*sizeof(int));
	for(int i=0;i<t0->edge_label_count;i++){
	    t->edge_label_name[i]=STRDUP(t0->edge_label_name[i]);
	    t->edge_label_type[i]=t0->edge_label_type[i];
	}
    return t;
}

void lts_type_print(log_t log, lts_type_t t){
	log_printf(log,"The state labels are:\n");
	for(int i=0;i<t->state_label_count;i++){
	    log_printf(log,"%4d: %s:%s\n",i,
                   t->state_label_name[i],
                   SIget(t->type_db,t->state_label_type[i]));
	}
	log_printf(log,"The edge labels are:\n");
	for(int i=0;i<t->edge_label_count;i++){
	    log_printf(log,"%4d: %s:%s\n",i,
                   t->edge_label_name[i],
                   SIget(t->type_db,t->edge_label_type[i]));
	}
    log_printf(log,"The registered types are:\n");
 	int N=SIgetCount(t->type_db);
	for(int i=0;i<N;i++){
	    log_printf(log,"%4d: %s (%s)\n",i,SIget(t->type_db,i),data_format_string(t,i));
	}
    log_printf(log,"The state vector is:\n");
    for(int i=0;i<t->state_length;i++){
        log_printf(log,"%4d: %s:%s\n",i,
                   t->state_name[i],
                   SIget(t->type_db,t->state_type[i]));
    }
}

void lts_type_destroy(lts_type_t *t){
	Warning(info,"Need to define ownership and implement destroy of LTS type.");
	RTfree(*t);
	*t=NULL;
}

void lts_type_set_state_length(lts_type_t  t,int length){
    int old_length = t->state_length;

    // allow state vector to be hidden
    if (length==0) {
        for(int i=0;i<t->state_length;i++){
            RTfree(t->state_name[i]);
        }
        RTfree(t->state_name);
        t->state_name=NULL;
        RTfree(t->state_type);
        t->state_type=NULL;
        t->state_length=0;
        return;
    }
    // allow ltstype to grow
    if (old_length < length) t->state_length=length;
    else Fatal(1, error, "lts-type isn't allowed to shrink");

    t->state_name=(char**)RTrealloc(t->state_name, length*sizeof(char*));
    t->state_type=(int*)RTrealloc(t->state_type, length*sizeof(int));

	for(int i=old_length;i<length;i++){
		t->state_name[i]=NULL;
		t->state_type[i]=SI_INDEX_FAILED;
	}
}

int lts_type_get_state_length(lts_type_t  t){
	return t->state_length;
}

void lts_type_set_state_name(lts_type_t  t,int idx,const char* name){
	t->state_name[idx]=strdup(name);
}

char* lts_type_get_state_name(lts_type_t  t,int idx){
	return t->state_name[idx];
}

void lts_type_set_state_type(lts_type_t  t,int idx,const char* name){
	t->state_type[idx]=SIput(t->type_db,name);
}

char* lts_type_get_state_type(lts_type_t  t,int idx){
	if(t->state_type[idx]==SI_INDEX_FAILED) {
		return NULL;
	} else {
		return SIget(t->type_db,t->state_type[idx]);
	}
}

void lts_type_set_state_typeno(lts_type_t  t,int idx,int typeno){
	t->state_type[idx]=typeno;
}

int lts_type_get_state_typeno(lts_type_t  t,int idx){
	return t->state_type[idx];
}

void lts_type_set_state_label_count(lts_type_t  t,int count){
	t->state_label_count=count;
	t->state_label_name=RTrealloc(t->state_label_name, count*sizeof(char*));
	t->state_label_type=RTrealloc(t->state_label_type, count*sizeof(int));
}
int lts_type_get_state_label_count(lts_type_t  t){
	return t->state_label_count;
}
void lts_type_set_state_label_name(lts_type_t  t,int label,const char*name){
	t->state_label_name[label]=strdup(name);
}
void lts_type_set_state_label_type(lts_type_t  t,int label,const char*name){
	t->state_label_type[label]=SIput(t->type_db,name);
}
void lts_type_set_state_label_typeno(lts_type_t  t,int label,int typeno){
	t->state_label_type[label]=typeno;
}
char* lts_type_get_state_label_name(lts_type_t  t,int label){
	return t->state_label_name[label];
}
char* lts_type_get_state_label_type(lts_type_t  t,int label){
	if(t->state_label_type[label]==SI_INDEX_FAILED) {
		return NULL;
	} else {
		return SIget(t->type_db,t->state_label_type[label]);
	}
}
int lts_type_get_state_label_typeno(lts_type_t  t,int label){
	return t->state_label_type[label];
}


void lts_type_set_edge_label_count(lts_type_t  t,int count){
	t->edge_label_count=count;
	t->edge_label_name=RTrealloc(t->edge_label_name, count*sizeof(char*));
	t->edge_label_type=RTrealloc(t->edge_label_type, count*sizeof(int));
}
int lts_type_get_edge_label_count(lts_type_t  t){
	return t->edge_label_count;
}
void lts_type_set_edge_label_name(lts_type_t  t,int label,const char*name){
	t->edge_label_name[label]=strdup(name);
}
void lts_type_set_edge_label_type(lts_type_t  t,int label,const char*name){
	t->edge_label_type[label]=SIput(t->type_db,name);
}
void lts_type_set_edge_label_typeno(lts_type_t  t,int label,int typeno){
	t->edge_label_type[label]=typeno;
}
char* lts_type_get_edge_label_name(lts_type_t  t,int label){
	return t->edge_label_name[label];
}
char* lts_type_get_edge_label_type(lts_type_t  t,int label){
	if(t->edge_label_type[label]==SI_INDEX_FAILED) {
		return NULL;
	} else {
		return SIget(t->type_db,t->edge_label_type[label]);
	}
}
int lts_type_get_edge_label_typeno(lts_type_t  t,int label){
	return t->edge_label_type[label];
}


int lts_type_get_type_count(lts_type_t  t){
	return SIgetCount(t->type_db);
}

data_format_t lts_type_get_format(lts_type_t  t,int typeno){
    return t->type_format[typeno];
}

void lts_type_set_format(lts_type_t  t,int typeno,data_format_t format){
    t->type_format[typeno]=format;
}

int lts_type_has_type(lts_type_t  t,const char *name){
    int type_no=SIlookup(t->type_db,name);
    return (type_no!=SI_INDEX_FAILED);
}

int lts_type_put_type(lts_type_t  t,const char *name,data_format_t format,int* is_new){
    int type_no;
    if (is_new) {
		type_no=SIlookup(t->type_db,name);
		if (type_no!=SI_INDEX_FAILED) {
			*is_new=0;
			if (format==t->type_format[type_no]) return type_no;
			Abort("existing format %s and requested format %s for type %s do not match",
			    data_format_string(t,type_no),data_format_string_generic(format),name);
		}
		*is_new=1;
    }
    type_no=SIput(t->type_db,name);
    t->type_format[type_no]=format;
    return type_no;
}

int lts_type_add_type(lts_type_t  t,const char *name,int *is_new){
    return lts_type_put_type(t,name,LTStypeChunk,is_new);
}

char* lts_type_get_type(lts_type_t  t,int typeno){
	return SIget(t->type_db,typeno);
}

void lts_type_validate(lts_type_t t){
    if (t->state_length < 0)
        Abort("unsupported state vector of size %d", t->state_length);
    for(int i=0;i<t->state_length;i++){
        if (t->state_name[i]==NULL) Abort("name of state variable %d undefined",i);
        if (t->state_type[i]<0) Abort("type of state variable %d undefined",i);
        if (SIget(t->type_db,t->state_type[i])==NULL) {
            Abort("type %d used for state variable %d, but undefined",t->state_type[i],i);
        }
    }
    for(int i=0;i<t->state_label_count;i++){
        if (t->state_label_name[i]==NULL) Abort("name of defined variable %d undefined",i);
        if (t->state_label_type[i]<0) Abort("type of defined variable %d undefined",i);
        if (SIget(t->type_db,t->state_label_type[i])==NULL) {
            Abort("type %d used for state label %d, but undefined",t->state_label_type[i],i);
        }
    }
    for(int i=0;i<t->edge_label_count;i++){
        if (t->edge_label_name[i]==NULL) Abort("name of edge label %d undefined",i);
        if (t->edge_label_type[i]<0) Abort("type of edge label %d undefined",i);
        if (SIget(t->type_db,t->edge_label_type[i])==NULL) {
            Abort("type %d used for edge label %d, but undefined",t->edge_label_type[i],i);
        }
    }
    int N=SIgetCount(t->type_db);
    for(int i=0;i<N;i++){
        switch(t->type_format[i]){
        case LTStypeDirect: continue;
        case LTStypeRange: {
            if (t->type_min[i]>t->type_max[i]){
                Abort("illegal range [%d,%d]",t->type_min[i],t->type_max[i]);
            }
            continue;
        }
        case LTStypeChunk: continue;
        case LTStypeEnum: continue;
        }
        Abort("illegal format value: %d",t->type_format[i]);        
    }
}

void lts_type_serialize(lts_type_t t,stream_t ds){
	DSwriteS(ds,"lts signature 1.1");
	uint32_t N=lts_type_get_state_length(t);
	Warning(debug,"state length is %d",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		char*x=lts_type_get_state_name(t,i);
		if (x) DSwriteS(ds,x); else DSwriteS(ds,"");
		DSwriteU32(ds,lts_type_get_state_typeno(t,i));
	}
	N=lts_type_get_state_label_count(t);
	Warning(info,"%d state labels",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		char*x=lts_type_get_state_label_name(t,i);
		if (x) DSwriteS(ds,x); else DSwriteS(ds,"");
		DSwriteU32(ds,lts_type_get_state_label_typeno(t,i));
	}
	N=lts_type_get_edge_label_count(t);
	Warning(info,"%d edge labels",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		char*x=lts_type_get_edge_label_name(t,i);
		if (x) DSwriteS(ds,x); else DSwriteS(ds,"");
		DSwriteU32(ds,lts_type_get_edge_label_typeno(t,i));
		Warning(debug,"edge label %d is %s : %s",i,x,lts_type_get_edge_label_type(t,i));
	}
	N=lts_type_get_type_count(t);
	Warning(info,"%d types",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		DSwriteS(ds,lts_type_get_type(t,i));
		DSwriteS(ds,(char*)data_format_string(t,i));
	}
}

lts_type_t lts_type_deserialize(stream_t ds){
	lts_type_t t=lts_type_create();
	char version[1024];
	DSreadS(ds,version,1024);
	int has_format_info;
	if (strcmp(version,"lts signature 1.1")==0){
		has_format_info=1;
	} else if (strcmp(version,"lts signature 1.0")==0){
		has_format_info=0;
	} else {
		Abort("cannot deserialize %s",version);
	}
	uint32_t N=DSreadU32(ds);
	Warning(info,"state length is %d",N);
	lts_type_set_state_length(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_state_name(t,i,x);
		RTfree(x);
		lts_type_set_state_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(info,"%d state labels",N);
	lts_type_set_state_label_count(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_state_label_name(t,i,x);
		RTfree(x);
		lts_type_set_state_label_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(info,"%d edge labels",N);
	lts_type_set_edge_label_count(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_edge_label_name(t,i,x);
		RTfree(x);
		lts_type_set_edge_label_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(info,"%d types",N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		SIputAt(t->type_db,x,i);
		RTfree(x);
		if (has_format_info) {
			x=DSreadSA(ds);
			if (strcmp(x,"direct")==0){
				t->type_format[i]=LTStypeDirect;
			} else if (strcmp(x,"chunk")==0){
				t->type_format[i]=LTStypeChunk;
			} else if (strcmp(x,"enum")==0){
				t->type_format[i]=LTStypeEnum;
			} else {
			    int n=strlen(x);
			    if (x[0]=='[' && x[n-1]==']') {
			        int k=0;
			        while(k<n && x[k]!=',') k++;
			        if (k<n) {
			            t->type_format[i]=LTStypeRange;
			            t->type_min[i]=atoi(x+1);
			            t->type_max[i]=atoi(x+k+1);
			        }
			    }
				Abort("unsupported data format %s",x);
			}
			RTfree(x);
		} else {
		    t->type_format[i]=LTStypeChunk;
		}
	}
	return t;
}

