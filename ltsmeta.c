#include "ltsmeta.h"
#include "runtime.h"


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
	uint64_t tau;
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

void lts_set_tau(lts_t lts,uint64_t tau){
	lts->tau=tau;
	lts->known|=TAU;
}

uint64_t lts_get_tau(lts_t lts){
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

lts_t lts_create(){
	lts_t lts=(lts_t)RTmalloc(sizeof(struct lts_meta_s));
	lts->known=0;
	lts->comment=NULL;
	return lts;
}

void lts_write_info(lts_t lts,stream_t ds,info_fmt_t format){
	switch(format){
	case DIR_INFO: {
		DSwriteU32(ds,31);
		DSwriteS(ds,"dummy comment");
		DSwriteU32(ds,1); // fwrite32(f,info->segment_count);
		DSwriteU32(ds,0); // fwrite32(f,info->initial_seg);
		DSwriteU32(ds,lts_get_root(lts)); // fwrite32(f,info->initial_ofs);
		DSwriteU32(ds,lts_get_labels(lts)); // fwrite32(f,info->label_count);
		if (lts_has_tau(lts)) {
			DSwriteS32(ds,lts_get_tau(lts)); // fwrite32(f,info->label_tau);
		} else {
			DSwriteS32(ds,-1);
		}
		DSwriteU32(ds,0); // fwrite32(f,info->top_count);
		//for(i=0;i<info->segment_count;i++){
		DSwriteU32(ds,lts_get_states(lts)); //fwrite32(f,info->state_count[i]);
		//}
		//for(i=0;i<info->segment_count;i++){
		//	for(j=0;j<info->segment_count;j++){
		DSwriteU32(ds,lts_get_trans(lts)); // fwrite32(f,info->transition_count[i][j]);
		//	}
		//}
		return;
		}
	}
	Fatal(0,error,"unimplemented format");
}

static lts_t create_info_v31(stream_t ds) {
	lts_t lts=lts_create();
	lts_set_comment(lts,DSreadSA(ds));

	int segment_count;
	//int i,j;
	segment_count=DSreadU32(ds);
	if (segment_count!=1) Fatal(0,error,"unsupported segment count %d",segment_count);
	DSreadU32(ds); //fread32(f,&((*info)->initial_seg));
	lts_set_root(lts,DSreadU32(ds));
	lts_set_labels(lts,DSreadU32(ds));
	DSreadU32(ds); //fread32(f,&((*info)->label_tau));
	DSreadU32(ds); //fread32(f,&((*info)->top_count));
	//for(i=0;i<segment_count;i++){
	lts_set_states(lts,DSreadU32(ds)); //fread32(f,((*info)->state_count)+i);
	//}
	//for(i=0;i<segment_count;i++){
	//	for(j=0;j<segment_count;j++){
	lts_set_trans(lts,DSreadU32(ds)); //fread32(f,&((*info)->transition_count[i][j]));
	//	}
	//}
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



