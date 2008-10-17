#include "ltsmeta.h"
#include "runtime.h"
#include "stream.h"


#define ROOT 0x01
#define STATES 0x02
#define TRANS 0x04
#define LABELS 0x08
#define TAU 0x10

struct lts_meta_s {
	uint64_t known;
	lts_state_t root;
	uint64_t states;
	uint64_t trans;
	uint64_t labels;
	uint32_t tau;
	uint32_t segment_count;
	uint32_t root_seg;
	uint32_t root_ofs;
	uint32_t label_count;
	uint32_t *state_count;
	uint32_t **transition_count;
	char *comment;
};

void lts_set_comment(lts_t lts,char *comment){
	lts->comment=comment;
}

char* lts_get_comment(lts_t lts){
	return lts->comment;
}

void lts_set_root(lts_t lts,lts_state_t state){
	lts->root=state;
	lts->known|=ROOT;
}

lts_state_t lts_get_root(lts_t lts){
	if(!(lts->known & ROOT)) Fatal(0,error,"root is not known");
	return lts->root;
}

void lts_set_trans(lts_t lts,uint64_t count){
	lts->trans=count;
	lts->known|=TRANS;
}

uint64_t lts_get_trans(lts_t lts){
	if(!(lts->known & TRANS)) Fatal(0,error,"transition count is not known");
	return lts->trans;
}

int lts_has_tau(lts_t lts){
	return (lts->known & TAU);
}

void lts_set_tau(lts_t lts,uint32_t tau){
	lts->tau=tau;
	lts->known|=TAU;
}

uint32_t lts_get_tau(lts_t lts){
	if(!(lts->known & TAU)) Fatal(0,error,"tau label is not known");
	return lts->tau;
}

void lts_set_states(lts_t lts,uint64_t count){
	lts->states=count;
	lts->known|=STATES;
}
uint64_t lts_get_states(lts_t lts){
	if(!(lts->known & STATES)) Fatal(0,error,"state count is not known");
	return lts->states;
}

void lts_set_labels(lts_t lts,uint64_t count){
	lts->labels=count;
	lts->known|=LABELS;
}
uint64_t lts_get_labels(lts_t lts){
	if(!(lts->known & LABELS)) Fatal(0,error,"label count is not known");
	return lts->labels;
}

void lts_set_segments(lts_t lts,int N){
	if (lts->segment_count!=-1) Fatal(1,error,"cannot set segment count more than once");
	lts->segment_count=N;
}

int lts_get_segments(lts_t lts){
	if (lts->segment_count==-1) {
		lts->segment_count=1;
	}
	return lts->segment_count;
}

lts_t lts_create(){
	lts_t lts=(lts_t)RTmalloc(sizeof(struct lts_meta_s));
	lts->known=0;
	lts->comment=NULL;
	return lts;
}

void lts_write_info(lts_t lts,stream_t ds,info_fmt_t format){
	switch(format){
	case DIR_INFO: {
		int i,j;
		DSwriteU32(ds,31);
		if (lts->comment) {
			DSwriteS(ds,lts->comment);
		} else {
			DSwriteS(ds,"no comment");
		}
		DSwriteU32(ds,lts->segment_count);
		DSwriteU32(ds,lts->root_seg);
		DSwriteU32(ds,lts->root_ofs);
		DSwriteU32(ds,lts_get_labels(lts));
		if (lts_has_tau(lts)) {
			DSwriteS32(ds,lts_get_tau(lts));
		} else {
			DSwriteS32(ds,-1);
		}
		DSwriteU32(ds,0);
		for(i=0;i<lts->segment_count;i++){
			DSwriteU32(ds,lts->state_count[i]);
		}
		for(i=0;i<lts->segment_count;i++){
			for(j=0;j<lts->segment_count;j++){
				DSwriteU32(ds,lts->transition_count[i][j]);
			}
		}
		return;
		}
	}
	Fatal(0,error,"unimplemented format");
}

static lts_t create_info_v31(stream_t ds) {
	int i,j;
	lts_t lts=lts_create();
	lts_set_comment(lts,DSreadSA(ds));

	lts->segment_count=DSreadU32(ds);
	lts->root_seg=DSreadU32(ds);
	lts->root_ofs=DSreadU32(ds);
	lts_set_labels(lts,DSreadU32(ds));

	lts_set_tau(lts,DSreadU32(ds));
	DSreadU32(ds); // skip top count;
	lts->state_count=(uint32_t*)RTmalloc(lts->segment_count*sizeof(uint32_t));
	uint64_t total=0;
	for(i=0;i<lts->segment_count;i++){
		lts->state_count[i]=DSreadU32(ds);
		total+=lts->state_count[i];
	}
	lts_set_states(lts,total);
	lts->transition_count=(uint32_t**)RTmalloc(lts->segment_count*sizeof(uint32_t*));
	total=0;
	for(i=0;i<lts->segment_count;i++){
		lts->transition_count[i]=(uint32_t*)RTmalloc(lts->segment_count*sizeof(uint32_t));
		for(j=0;j<lts->segment_count;j++){
			lts->transition_count[i][j]=DSreadU32(ds);
			total+=lts->transition_count[i][j];
			//Warning(info,"%d transitions from %d to %d",lts->transition_count[i][j],i,j);
		}
	}
	lts_set_trans(lts,total);
	if(lts->segment_count==1){
		lts_set_root(lts,lts->root_ofs);
	}
	return lts;
}

lts_t lts_read(stream_t ds){
	char description[1024];
	DSreadS(ds,description,1024);
	if (strlen(description)!=0) {
		Fatal(0,error,"cannot read %s format",description);
	}
	if (DSreadU16(ds)==31){
		return create_info_v31(ds);
	}
	Fatal(0,error,"cannot identify input format");
	return NULL;
}



