
#include "config.h"
#include <string.h>
#include "ltsman.h"
#include "runtime.h"

#define UNDEF_SEG ((lts_seg_t)0xFFFFFFFF)
#define UNDEF_OFS ((lts_ofs_t)0xFFFFFFFF)
#define UNDEF_COUNT ((lts_count_t)0xFFFFFFFF)
#define MAX_COMMENT_COUNT 4

struct lts_s {
	lts_seg_t root_seg;
	lts_ofs_t root_ofs;
	lts_seg_t segments;
	lts_count_t*states;
	lts_count_t**trans;
	char *comment[MAX_COMMENT_COUNT];
	int enum_comment;
	int next_comment;
	string_index_t string_index;
	stream_t pkt_stream;
};

lts_t lts_new(){
	lts_t lts=(lts_t)RTmalloc(sizeof(struct lts_s));
	lts->root_seg=UNDEF_SEG;
	lts->root_ofs=UNDEF_OFS;
	lts->segments=UNDEF_SEG;
	lts->states=NULL;
	lts->trans=NULL;
	for(int i=0;i<MAX_COMMENT_COUNT;i++) lts->comment[i]=NULL;
	lts->enum_comment=0;
	lts->next_comment=0;
	lts->string_index=SIcreate();
	lts->pkt_stream=NULL;
	return lts;
}

void lts_set_packet_stream(lts_t lts,stream_t stream){
	lts->pkt_stream=stream;
}

/// Fake the LTS info xxx header as a packet.
#define TAG_HEADER 'L'  
#define TAG_SEGMENTS 'W'
#define TAG_ROOT 'R'
#define TAG_COMMENT 'C'
#define TAG_STATES 'S'
#define TAG_TRANS 'T'

static void pkt_write_segments(stream_t ds,lts_seg_t count){
	DSwriteU16(ds,5);
	DSwriteU8(ds,TAG_SEGMENTS);
	DSwriteU32(ds,count);
}
static void pkt_write_root(stream_t ds,lts_seg_t seg,lts_ofs_t ofs){
	DSwriteU16(ds,9);
	DSwriteU8(ds,TAG_ROOT);
	DSwriteU32(ds,seg);
	DSwriteU32(ds,ofs);
}
static void pkt_write_comment(stream_t ds,char*comment){
	int len=strlen(comment);
	DSwriteU16(ds,1+len);
	DSwriteU8(ds,TAG_COMMENT);
	DSwrite(ds,comment,len);
}
static void pkt_write_states(stream_t ds,lts_seg_t seg,lts_ofs_t val){
	DSwriteU16(ds,9);
	DSwriteU8(ds,TAG_STATES);
	DSwriteU32(ds,seg);
	DSwriteU32(ds,val);
}
static void pkt_write_trans(stream_t ds,lts_seg_t src,lts_seg_t dst,lts_count_t count){
	DSwriteU16(ds,13);
	DSwriteU8(ds,TAG_TRANS);
	DSwriteU32(ds,src);
	DSwriteU32(ds,dst);
	DSwriteU32(ds,count);	
}
void lts_info_add(lts_t lts,uint16_t len,uint8_t*data){
	if (len==0) Fatal(1,error,"illegal zero-length packet");
	switch(data[0]){
	case TAG_SEGMENTS:{
		if (len!=5) Fatal(1,error,"bad length");
		lts_seg_t segments=PKT_U32(data,1);
		lts_set_segments(lts,segments);
		return;
	}
	case TAG_ROOT:{
		if (len!=9) Fatal(1,error,"bad length");
		lts_seg_t seg=PKT_U32(data,1);
		lts_ofs_t ofs=PKT_U32(data,5);
		lts_set_root(lts,seg,ofs);
		return;
	}
	case TAG_COMMENT:{
		data[len]=0;
		lts_add_comment(lts,(char*)(data+1));
		return;
	}
	case TAG_STATES:{
		if (len!=9) Fatal(1,error,"bad length");
		lts_seg_t seg=PKT_U32(data,1);
		lts_seg_t count=PKT_U32(data,5);
		lts_set_states(lts,seg,count);
		return;
	}
	case TAG_TRANS:{
		if (len!=13) Fatal(1,error,"bad length");
		lts_seg_t src=PKT_U32(data,1);
		lts_seg_t dst=PKT_U32(data,5);
		lts_seg_t count=PKT_U32(data,9);
		lts_set_trans(lts,src,dst,count);
		return;
	}
	case TAG_HEADER:{
		data[len]=0;
		Warning(info,"processing a %s stream",(char*)(data));
		return;
	}
	default:
		Fatal(1,error,"unknown tag %x",data[0]);
	}
}

static void pkt_write_all(stream_t ds,lts_t lts){
	DSwriteS(ds,"LTS info 0.1");
	if (!lts_has_segments(lts)) return;
	pkt_write_segments(ds,lts->segments);
	if (lts_has_root(lts)) pkt_write_root(ds,lts->root_seg,lts->root_ofs);
	lts_reset_comment(lts);
	char* comment;
	while((comment=lts_next_comment(lts))) pkt_write_comment(ds,comment);
	for(lts_seg_t i=0;i<lts->segments;i++){
		if (lts->states[i]!=UNDEF_SEG) pkt_write_states(ds,i,lts->states[i]);
		for(lts_seg_t j=0;j<lts->segments;j++){
			if (lts->trans[i][j]!=UNDEF_COUNT) pkt_write_trans(ds,i,j,lts->trans[i][j]);
		}
	}
}

int lts_has_root(lts_t lts){
	return (lts->root_seg != UNDEF_SEG && lts->root_ofs != UNDEF_OFS);
}
lts_seg_t lts_get_root_seg(lts_t lts){
	if (lts->root_seg == UNDEF_SEG) Fatal(1,error,"root segment is not known");
	return lts->root_seg;
}
lts_ofs_t lts_get_root_ofs(lts_t lts){
	if (lts->root_ofs == UNDEF_OFS) Fatal(1,error,"root offset is not known");
	return lts->root_ofs;
}
void lts_set_root(lts_t lts,lts_seg_t seg,lts_ofs_t ofs){
	if (seg==UNDEF_SEG) Fatal(1,error,"illegal root segment");
	if (ofs==UNDEF_OFS) Fatal(1,error,"illegal root offset");
	if (lts->root_seg == UNDEF_SEG) {
		lts->root_seg=seg;
		lts->root_ofs=ofs;
		if(lts->pkt_stream) pkt_write_root(lts->pkt_stream,seg,ofs);
	} else {
		if (lts->root_seg != seg) Fatal(1,error,"inconsistent root segment %d != %d",lts->root_seg,seg);
		if (lts->root_ofs != ofs) Fatal(1,error,"inconsistent root offset %d != %d",lts->root_ofs,ofs);
	}
}


int lts_has_segments(lts_t lts){
	return (lts->segments != UNDEF_SEG);
}
lts_seg_t lts_get_segments(lts_t lts){
	if (lts->segments == UNDEF_SEG) Fatal(1,error,"segment count is not known");
	return lts->segments;
}
void lts_set_segments(lts_t lts,lts_seg_t segments){
	if (segments==UNDEF_SEG) Fatal(1,error,"illegal segment count");
	if (lts->segments == UNDEF_SEG) {
		lts->segments=segments;
		lts->states=(lts_ofs_t*)RTmalloc(segments*sizeof(lts_count_t));
		lts->trans=(lts_count_t**)RTmalloc(segments*sizeof(lts_count_t*));
		for(lts_seg_t i=0;i<segments;i++) {
			lts->states[i]=UNDEF_OFS;
			lts->trans[i]=RTmalloc(segments*sizeof(lts_count_t));
			for(lts_seg_t j=0;j<segments;j++) lts->trans[i][j]=UNDEF_COUNT;
		}
		if(lts->pkt_stream) pkt_write_segments(lts->pkt_stream,segments);
	} else {
		if(lts->segments!=segments) Fatal(1,error,"inconsistent segment count");
	}
}

int lts_has_states(lts_t lts,lts_seg_t seg){
	return (lts->states!=NULL && lts->states[seg] != UNDEF_SEG);
}
lts_ofs_t lts_get_states(lts_t lts,lts_ofs_t seg){
	if (lts->states==NULL || lts->states[seg] == UNDEF_OFS) Fatal(1,error,"state count is not known");
	return lts->states[seg];
}
void lts_set_states(lts_t lts,lts_seg_t seg,lts_ofs_t val){
	if (lts->states==NULL) Fatal(1,error,"cannot set state count before setting segments count");
	if (val==UNDEF_OFS) Fatal(1,error,"illegal state count");
	if (lts->states[seg] == UNDEF_OFS) {
		lts->states[seg]=val;
		if(lts->pkt_stream) pkt_write_states(lts->pkt_stream,seg,val);
	} else {
		if(lts->states[seg]!=val) Fatal(1,error,"inconsistent " "state count");
	}
}

int lts_has_trans(lts_t lts,lts_seg_t src,lts_seg_t dst){
	return (lts->trans!=NULL && lts->trans[src][dst] != UNDEF_COUNT);
}
lts_count_t lts_get_trans(lts_t lts,lts_seg_t src,lts_seg_t dst){
	if (lts->trans==NULL || lts->trans[src][dst] == UNDEF_COUNT)
		Fatal(1,error,"transition count is not known");
	return lts->trans[src][dst];
}
void lts_set_trans(lts_t lts,lts_seg_t src,lts_seg_t dst,lts_count_t count){
	if (lts->trans==NULL) Fatal(1,error,"cannot set transition count before setting segments count");
	if (count==UNDEF_COUNT)  Fatal(1,error,"illegal transition count");
	if (lts->trans[src][dst] == UNDEF_COUNT){
		lts->trans[src][dst] = count;
		if(lts->pkt_stream) pkt_write_trans(lts->pkt_stream,src,dst,count);
	} else {
		if (lts->trans[src][dst] != count) Fatal(1,error,"inconsistent transition count");
	}
}


void lts_add_comment(lts_t lts,char*comment){
	if (comment==NULL) Fatal(1,error,"illegal comment");
	if(lts->pkt_stream) pkt_write_comment(lts->pkt_stream,comment);
	if (lts->next_comment == MAX_COMMENT_COUNT) {
		Warning(info,"ignoring extra comment");
	} else {
		lts->comment[lts->next_comment]=strdup(comment);
		lts->next_comment++;
	}
}
void lts_reset_comment(lts_t lts){
	lts->next_comment=0;
}
char* lts_next_comment(lts_t lts){
	if (lts->enum_comment<lts->next_comment) {
		lts->enum_comment++;
		return lts->comment[lts->enum_comment-1];
	} else {
		return NULL;
	}
}

string_index_t lts_get_string_index(lts_t lts){
	return lts->string_index;
}

void lts_write_info(lts_t lts,stream_t ds,info_fmt_t format){
	switch(format){
	case LTS_INFO_DIR: {
		DSwriteU32(ds,31);
		if (lts->comment[0]) {
			DSwriteS(ds,lts->comment[0]);
		} else {
			DSwriteS(ds,"no comment");
		}
		DSwriteU32(ds,lts_get_segments(lts));
		DSwriteU32(ds,lts_get_root_seg(lts));
		DSwriteU32(ds,lts_get_root_ofs(lts));
		DSwriteU32(ds,SIgetCount(lts->string_index));
		int tau=SIlookup(lts->string_index,"tau");
		if (tau==SI_INDEX_FAILED) {
			DSwriteS32(ds,-1);
		} else {
			DSwriteS32(ds,tau);
		}
		DSwriteU32(ds,0); // This field was either top count or vector length. CHECK!
		for(lts_seg_t i=0;i<lts->segments;i++){
			DSwriteU32(ds,lts_get_states(lts,i));
		}
		for(lts_seg_t i=0;i<lts->segments;i++){
			for(lts_seg_t j=0;j<lts->segments;j++){
				DSwriteU32(ds,lts_get_trans(lts,i,j));
			}
		}
		return;
	}
	case LTS_INFO_PACKET:
		pkt_write_all(ds,lts);
		return;
	}
}


static lts_t create_info_v31(stream_t ds) {
	lts_t lts=lts_new();
	lts_add_comment(lts,DSreadSA(ds));
	lts_set_segments(lts,DSreadU32(ds));
	lts_seg_t root_seg=DSreadU32(ds);
	lts_ofs_t root_ofs=DSreadU32(ds);
	lts_set_root(lts,root_seg,root_ofs);
	DSreadU32(ds); // skip label count;
	int tau=DSreadS32(ds);
	if (tau>=0) SIputAt(lts->string_index,"tau",tau);
	DSreadU32(ds); // skip top count;
	for(lts_seg_t i=0;i<lts->segments;i++){
		lts_set_states(lts,i,DSreadU32(ds));
	}
	for(lts_seg_t i=0;i<lts->segments;i++){
		for(lts_seg_t j=0;j<lts->segments;j++){
			lts_set_trans(lts,i,j,DSreadU32(ds));
		}
	}
	return lts;
}

lts_t lts_read_info(stream_t ds,int*header_found){
	char description[1024];
	DSreadS(ds,description,1024);
	if (strlen(description)==0) {
		switch(DSreadU16(ds)){
		case 0:
			if (DSreadU16(ds)!=31) {
				Fatal(1,error,"cannot identify input format");
				return NULL;
			}
			Log(info,"input uses headers");
			*header_found=1;
			return create_info_v31(ds);
		case 31:
			*header_found=0;
			Log(info,"input has no headers");
			return create_info_v31(ds);
		default:
			Fatal(1,error,"cannot identify input format");
			return NULL;
		}
	}
	Log(info,"input uses headers");
	*header_found=1;
	ds=stream_setup(ds,description);
	if (DSreadU32(ds)==31) return create_info_v31(ds);
	Fatal(0,error,"cannot identify input format");
	return NULL;
}



