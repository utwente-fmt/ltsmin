
#include "dlts.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "runtime.h"

#define BUFFER_SIZE (1024 * 1024)

dlts_t dlts_create(){
	dlts_t lts;

	lts=(dlts_t)RTmalloc(sizeof(struct dlts));
	lts->arch=NULL;
	lts->segment_count=0;
	return lts;
}

void dlts_free(dlts_t lts){
	int i,j;
	for(i=0;i<lts->segment_count;i++){
		free(lts->transition_count[i]);
		free(lts->label_string[i]);
		for(j=0;j<lts->segment_count;j++){
			if (lts->src[i][j]) free(lts->src[i][j]);
			if (lts->label[i][j]) free(lts->label[i][j]);
			if (lts->dest[i][j]) free(lts->dest[i][j]);
		}
		free(lts->src[i]);
		free(lts->label[i]);
		free(lts->dest[i]);
	}
	free(lts->transition_count);
	free(lts->label_string);
	free(lts->src);
	free(lts->label);
	free(lts->dest);
	free(lts);
}

void dlts_getinfo(dlts_t lts){
	stream_t ds;
	int i,j,version,dummy;

	ds=arch_read(lts->arch,"info",NULL);
	version=DSreadU32(ds);

/* If info is compressed with "" then the file starts with 00 00 00 00 00 1f
   If info is uncompressed       then the file starts with 00 00 00 1f
 */
	if (version==0) {
		Warning(info,"detected compressed input",lts->decode);
		lts->decode="auto";
		version=DSreadU16(ds);
	} else {
		Warning(debug,"detected uncompressed input");
		lts->decode=NULL;
	}
	if (version!=31) Fatal(1,error,"wrong file version: %d",version);
	lts->info=DSreadSA(ds);
	Warning(info,"info field is %s",lts->info);
	lts->segment_count=DSreadU32(ds);
	lts->root_seg=DSreadU32(ds);
	lts->root_ofs=DSreadU32(ds);
	lts->label_count=DSreadU32(ds);
	lts->tau=DSreadU32(ds);
	dummy=DSreadU32(ds);
	lts->state_count=(uint32_t*)RTmalloc(lts->segment_count*sizeof(uint32_t));
	for(i=0;i<lts->segment_count;i++){
		lts->state_count[i]=DSreadU32(ds);
	}
	lts->transition_count=(uint32_t**)RTmalloc(lts->segment_count*sizeof(uint32_t*));
	for(i=0;i<lts->segment_count;i++){
		lts->transition_count[i]=(uint32_t*)RTmalloc(lts->segment_count*sizeof(uint32_t));
		for(j=0;j<lts->segment_count;j++){
			lts->transition_count[i][j]=DSreadU32(ds);
			Warning(debug,"%d transitions from %d to %d",lts->transition_count[i][j],i,j);
		}
	}
	lts->src=(uint32_t***)malloc(lts->segment_count*sizeof(uint32_t**));
	lts->label=(uint32_t***)malloc(lts->segment_count*sizeof(uint32_t**));
	lts->dest=(uint32_t***)malloc(lts->segment_count*sizeof(uint32_t**));
	for(i=0;i<lts->segment_count;i++){
		lts->src[i]=(uint32_t**)malloc(lts->segment_count*sizeof(uint32_t*));
		lts->label[i]=(uint32_t**)malloc(lts->segment_count*sizeof(uint32_t*));
		lts->dest[i]=(uint32_t**)malloc(lts->segment_count*sizeof(uint32_t*));
		for(j=0;j<lts->segment_count;j++){
			lts->src[i][j]=NULL;
			lts->label[i][j]=NULL;
			lts->dest[i][j]=NULL;
		}
	}
	DSclose(&ds);
}

void dlts_getTermDB(dlts_t lts){
	int N=lts->label_count;
	stream_t ds=arch_read(lts->arch,"TermDB",lts->decode);
	char **label=RTmalloc(N*sizeof(char*));
	for(int i=0;i<N;i++){
		//Warning(info,"reading label %d",i);
		label[i]=DSreadLN(ds);
	}
	lts->label_string=label;
	DSclose(&ds);
}

void dlts_load_src(dlts_t lts,int from,int to){
	uint32_t i;
	char name[1024];
	uint32_t *data;

	sprintf(name,"src-%d-%d",from,to);
	stream_t ds=arch_read(lts->arch,name,lts->decode);
	data=(uint32_t*)RTmalloc((lts->transition_count[from][to])*sizeof(uint32_t));
	for(i=0;i<lts->transition_count[from][to];i++){
		data[i]=DSreadU32(ds);
	}
	lts->src[from][to]=data;
	DSclose(&ds);
}
void dlts_load_label(dlts_t lts,int from,int to){
	uint32_t i;
	char name[1024];
	uint32_t *data;

	sprintf(name,"label-%d-%d",from,to);
	stream_t ds=arch_read(lts->arch,name,lts->decode);
	data=(uint32_t*)RTmalloc((lts->transition_count[from][to])*sizeof(uint32_t));
	for(i=0;i<lts->transition_count[from][to];i++){
		data[i]=DSreadU32(ds);
	}
	lts->label[from][to]=data;
	DSclose(&ds);
}
void dlts_load_dest(dlts_t lts,int from,int to){
	uint32_t i;
	char name[1024];
	uint32_t *data;

	sprintf(name,"dest-%d-%d",from,to);
	stream_t ds=arch_read(lts->arch,name,lts->decode);
	data=(uint32_t*)RTmalloc((lts->transition_count[from][to])*sizeof(uint32_t));
	for(i=0;i<lts->transition_count[from][to];i++){
		data[i]=DSreadU32(ds);
	}
	lts->dest[from][to]=data;
	DSclose(&ds);
}

void dlts_free_dest(dlts_t lts,int from,int to){
	free(lts->dest[from][to]);
	lts->dest[from][to]=NULL;
}
void dlts_free_src(dlts_t lts,int from,int to){
	free(lts->src[from][to]);
	lts->src[from][to]=NULL;
}
void dlts_free_label(dlts_t lts,int from,int to){
	free(lts->label[from][to]);
	lts->label[from][to]=NULL;
}





