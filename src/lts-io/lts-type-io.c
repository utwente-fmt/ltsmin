// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre-io/user.h>

#include <lts-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <hre/stringindex.h>

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
	Warning(debug,"%d state labels",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		char*x=lts_type_get_state_label_name(t,i);
		if (x) DSwriteS(ds,x); else DSwriteS(ds,"");
		DSwriteU32(ds,lts_type_get_state_label_typeno(t,i));
	}
	N=lts_type_get_edge_label_count(t);
	Warning(debug,"%d edge labels",N);
	DSwriteU32(ds,N);
	for(uint32_t i=0;i<N;i++){
		char*x=lts_type_get_edge_label_name(t,i);
		if (x) DSwriteS(ds,x); else DSwriteS(ds,"");
		DSwriteU32(ds,lts_type_get_edge_label_typeno(t,i));
		Warning(debug,"edge label %d is %s : %s",i,x,lts_type_get_edge_label_type(t,i));
	}
	N=lts_type_get_type_count(t);
	Warning(debug,"%d types",N);
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
	Warning(debug,"state length is %d",N);
	lts_type_set_state_length(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_state_name(t,i,x);
		RTfree(x);
		lts_type_set_state_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(debug,"%d state labels",N);
	lts_type_set_state_label_count(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_state_label_name(t,i,x);
		RTfree(x);
		lts_type_set_state_label_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(debug,"%d edge labels",N);
	lts_type_set_edge_label_count(t,N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		if (strlen(x)) lts_type_set_edge_label_name(t,i,x);
		RTfree(x);
		lts_type_set_edge_label_typeno(t,i,DSreadU32(ds));
	}
	N=DSreadU32(ds);
	Warning(debug,"%d types",N);
	for(uint32_t i=0;i<N;i++){
		char*x=DSreadSA(ds);
		int tmp=lts_type_add_type(t,x,NULL);
		if (tmp!=(int)i) Abort("bad type add");
		RTfree(x);
		if (has_format_info) {
			x=DSreadSA(ds);
			if (strcmp(x,"direct")==0){
			    lts_type_set_format(t,i,LTStypeDirect);
			} else if (strcmp(x,"chunk")==0){
				lts_type_set_format(t,i,LTStypeChunk);
			} else if (strcmp(x,"enum")==0){
				lts_type_set_format(t,i,LTStypeEnum);
			} else if (strcmp(x,"Boolean")==0){
				lts_type_set_format(t,i,LTStypeBool);
			} else if (strcmp(x,"trilean")==0){
				lts_type_set_format(t,i,LTStypeTrilean);
			} else if (strcmp(x,"Signed 32-bit integer")==0){
				lts_type_set_format(t,i,LTStypeSInt32);
			} else {
			    int n=strlen(x);
			    if (x[0]=='[' && x[n-1]==']') {
			        int k=0;
			        while(k<n && x[k]!=',') k++;
			        if (k<n) {
			            lts_type_set_format(t,i,LTStypeRange);
			            lts_type_set_range(t,i,atoi(x+1),atoi(x+k+1));
			        }
			    }
				Abort("unsupported data format %s",x);
			}
			RTfree(x);
		} else {
		    lts_type_set_format(t,i,LTStypeChunk);
		}
	}
	return t;
}

