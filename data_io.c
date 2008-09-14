#include "config.h"
#include "data_io.h"
#include <malloc.h>
#include <byteswap.h>
#include <string.h>
#include "runtime.h"

struct DataStream{
	stream_t stream;
	int swap;
};


int DSreadable(data_stream_t ds){
	return stream_readable(ds->stream);
}


int DSwritable(data_stream_t ds){
	return stream_writable(ds->stream);
}

void DSread(data_stream_t ds,void*buf,size_t count){
	stream_read(ds->stream,buf,count);
}

void DSwrite(data_stream_t ds,void*buf,size_t count){
	stream_write(ds->stream,buf,count);
}

void DSsetSwap(data_stream_t ds,int swap){
	ds->swap=swap;
}

int DSgetSwap(data_stream_t ds){
	return ds->swap;
}

data_stream_t DScreate(stream_t stream,int swap){
	data_stream_t s=(data_stream_t)malloc(sizeof(struct DataStream));
	if (s==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	s->stream=stream;
	s->swap=swap;
	return s;
}

int64_t DSreadS64(data_stream_t ds){
	int64_t i;
	DSread(ds,&i,8);
	if(ds->swap & SWAP_READ) i=bswap_64(i);
	return i;
}

uint64_t DSreadU64(data_stream_t ds){
	uint64_t i;
	DSread(ds,&i,8);
	if(ds->swap & SWAP_READ) i=bswap_64(i);
	return i;
}

double DSreadD(data_stream_t ds){
	double d;
	DSread(ds,&d,8);
	if(ds->swap & SWAP_READ) d=bswap_64(((uint64_t)d));
	return d;
}

int32_t DSreadS32(data_stream_t ds){
	int32_t i;
	DSread(ds,&i,4);
	if(ds->swap & SWAP_READ) i=bswap_32(i);
	return i;
}

uint32_t DSreadU32(data_stream_t ds){
	uint32_t i;
	DSread(ds,&i,4);
	if(ds->swap & SWAP_READ) i=bswap_32(i);
	return i;
}

float DSreadF(data_stream_t ds){
	float f;
	DSread(ds,&f,4);
	if(ds->swap & SWAP_READ) f=bswap_32(f);
	return f;
}


int16_t DSreadS16(data_stream_t ds){
	int16_t i;
	DSread(ds,&i,2);
	if(ds->swap & SWAP_READ) i=bswap_16(i);
	return i;
}

uint16_t DSreadU16(data_stream_t ds){
	uint16_t i;
	DSread(ds,&i,2);
	if(ds->swap & SWAP_READ) i=bswap_16(i);
	return i;
}


int8_t DSreadS8(data_stream_t ds){
	int8_t i;
	DSread(ds,&i,1);
	return i;
}

uint8_t DSreadU8(data_stream_t ds){
	uint8_t i;
	DSread(ds,&i,1);
	return i;
}

void DSreadS(data_stream_t ds,char *s,int maxlen){
	uint16_t len=DSreadU16(ds);
	if (len>=maxlen) {
		Fatal(0,error,"string overflow");
	}
	DSread(ds,s,len);
	s[len]=0;
}

char* DSreadSA(data_stream_t ds){
	uint16_t len=DSreadU16(ds);
	char*s=RTmalloc(len+1);
	DSread(ds,s,len);
	s[len]=0;
	return s;
}

char* DSreadLN(data_stream_t ds){
	char tmp[4096];
	for(int i=0;i<4096;i++){
		tmp[i]=DSreadU8(ds);
		if (tmp[i]=='\n') {
			char *s=RTmalloc(i+1);
			memcpy(s,tmp,i);
			s[i]=0;
			return s;
		}
	}
	Fatal(0,error,"assumption that line is shorter than 4096 failed");
	return NULL;
}

void DSwriteS64(data_stream_t ds,int64_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_64(i);
	DSwrite(ds,&i,8);
}

void DSwriteU64(data_stream_t ds,uint64_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_64(i);
	DSwrite(ds,&i,8);
}

void DSwriteD(data_stream_t ds,double d){
	if (ds->swap & SWAP_WRITE) d=bswap_64(((uint64_t)d));
	DSwrite(ds,&d,8);
}


void DSwriteS32(data_stream_t ds,int32_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_32(i);
	DSwrite(ds,&i,4);
}

void DSwriteU32(data_stream_t ds,uint32_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_32(i);
	DSwrite(ds,&i,4);
}

void DSwriteF(data_stream_t ds,float f){
	if (ds->swap & SWAP_WRITE) f=bswap_32(f);
	DSwrite(ds,&f,4);
}

void DSwriteS16(data_stream_t ds,int16_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_16(i);
	DSwrite(ds,&i,2);
}

void DSwriteU16(data_stream_t ds,uint16_t i){
	if (ds->swap & SWAP_WRITE) i=bswap_16(i);
	DSwrite(ds,&i,2);
}

void DSwriteS8(data_stream_t ds,int8_t i){
	DSwrite(ds,&i,1);
}

void DSwriteU8(data_stream_t ds,uint8_t i){
	DSwrite(ds,&i,1);
}


void DSflush(data_stream_t ds){
	stream_flush(ds->stream);
}

void DSdestroy(data_stream_t *ds){
	free(*ds);
	*ds=NULL;
}

void DSclose(data_stream_t *ds){
	stream_close(&((*ds)->stream));
	free(*ds);
	*ds=NULL;
}

void DSwriteS(data_stream_t ds,char *s){
	int len=strlen(s);
	if (len>=65536) {
		Fatal(1,error,"string too long for DSwriteS");
	}
	DSwriteU16(ds,len);
	DSwrite(ds,s,len);
}

stream_t DSgetStream(data_stream_t ds){
	return ds->stream;
}

void DSwriteVL(data_stream_t ds,uint64_t i){
	uint8_t tmp;
	do {
		tmp=i&0x7f;
		i=i>>7;
		if (i) tmp|=0x80;
		DSwriteU8(ds,tmp);
	} while(i);
}
uint64_t DSreadVL(data_stream_t ds){
	uint64_t i=0;
	uint8_t tmp;
	int ofs=0;
	do {
		tmp=DSreadU8(ds);
		i=i+((tmp&0x7f)<<ofs);
		ofs+=7;
	} while (tmp&0x80);
	return i;
}





