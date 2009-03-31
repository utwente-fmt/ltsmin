#include "config.h"
#include <stdlib.h>
#include <zlib.h>

#include "unix.h"
#include "stream_object.h"
#include "runtime.h"

struct stream_s {
	struct stream_obj procs;
	FILE *f;
};


/*************************************************************************/
/* Wrapper functions.                                                    */
/*************************************************************************/


void stream_read(stream_t stream,void* buf,size_t count){
	stream->procs.read(stream,buf,count);
}

size_t stream_read_max(stream_t stream,void* buf,size_t count){
	return stream->procs.read_max(stream,buf,count);
}

int stream_empty(stream_t stream){
	return stream->procs.empty(stream);
}

void stream_write(stream_t stream,void* buf,size_t count){
	stream->procs.write(stream,buf,count);
}

void stream_flush(stream_t stream){
	stream->procs.flush(stream);
}

void stream_close(stream_t *stream){
	(*stream)->procs.close(stream);
}

int stream_readable(stream_t stream){
	return (stream->procs.read_max!=stream_illegal_read_max);
}

int stream_writable(stream_t stream){
	return (stream->procs.write!=stream_illegal_write);
}


/*************************************************************************/
/* Initialize to illegal                                                 */
/*************************************************************************/

size_t stream_illegal_read_max(stream_t stream,void*buf,size_t count){
	(void)stream;(void)buf;(void)count;
	Fatal(0,error,"illegal read max on stream");
	return 0;
}
size_t stream_default_read_max(stream_t stream,void*buf,size_t count){
	stream->procs.read(stream,buf,count);
	return count;
}
void stream_illegal_read(stream_t stream,void*buf,size_t count){
	(void)stream;(void)buf;(void)count;
	Fatal(0,error,"illegal read on stream");
}
void stream_default_read(stream_t stream,void*buf,size_t count){
	size_t res=stream->procs.read_max(stream,buf,count);
	if (res<count) {
		Fatal(1,error,"short read");
	}
}
int stream_illegal_empty(stream_t stream){
	(void)stream;
	Fatal(0,error,"illegal empty on stream");
	return 0;
}
void stream_illegal_write(stream_t stream,void*buf,size_t count){
	(void)stream;(void)buf;(void)count;
	Fatal(0,error,"illegal write on stream");
}
void stream_illegal_flush(stream_t stream){
	(void)stream;
	Fatal(0,error,"illegal flush on stream");
}
void stream_illegal_close(stream_t *stream){
	(void)stream;
	Fatal(0,error,"illegal close on stream");
}
void stream_init(stream_t s){
	s->procs.read_max=stream_illegal_read_max;
	s->procs.read=stream_illegal_read;
	s->procs.empty=stream_illegal_empty;
	s->procs.write=stream_illegal_write;
	s->procs.flush=stream_illegal_flush;
	s->procs.close=stream_illegal_close;
	s->procs.swap=SWAP_NETWORK;
}

/*************************************************************************/
/* FILE IO functions.                                                    */
/*************************************************************************/

static void file_read(stream_t stream,void*buf,size_t count){
	size_t res=fread(buf,1,count,stream->f);
	if (res<count) Fatal(0,error,"short read");
}

static size_t file_read_max(stream_t stream,void*buf,size_t count){
	size_t res=fread(buf,1,count,stream->f);
	return res;
}

static int file_empty(stream_t stream){
	int c=fgetc(stream->f);
	if (c==EOF) {
		return 1;
	} else {
		if (ungetc(c,stream->f)==EOF){
			Fatal(0,error,"unexpected failure");
		};
		return 0;
	}
}

static void file_write(stream_t stream,void*buf,size_t count){
	size_t res=fwrite(buf,1,count,stream->f);
	if (res<count) Fatal(0,error,"short write");
}

static void file_close(stream_t *stream){
	fclose((*stream)->f);
	free(*stream);
	*stream=NULL;
}

static void file_flush(stream_t stream){
	fflush(stream->f);
}

stream_t stream_input(FILE*f){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	setbuf(f,NULL);
	s->f=f;
	s->procs.read_max=file_read_max;
	s->procs.read=file_read;
	s->procs.empty=file_empty;
	s->procs.close=file_close;
	return s;
}

stream_t fs_read(char *name){
	FILE*f=fopen(name,"r");
	if (f==NULL) Fatal(0,error,"failed to open %s for reading",name);
	return stream_input(f);
}

stream_t stream_output(FILE*f){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	setbuf(f,NULL);
	s->f=f;
	s->procs.write=file_write;
	s->procs.flush=file_flush;
	s->procs.close=file_close;
	return s;
}

stream_t fs_write(char *name){
	FILE*f=fopen(name,"w");
	if (f==NULL) Fatal(0,error,"failed to open %s for writing",name);
	return stream_output(f);
}


stream_t file_input(char *name){
	FILE*f =fopen(name,"r");
	if (!f) {
		FatalCall(1,error,"could not open %s for reading",name);
	}
	return stream_input(f);
}


stream_t file_output(char*name){
	FILE*f =fopen(name,"w");
	if (!f) {
		FatalCall(1,error,"could not open %s for writing",name);
	}
	return stream_output(f);
}

/* Data Input/Output */


void DSsetSwap(stream_t ds,int swap){
	ds->procs.swap=swap;
}

int DSgetSwap(stream_t ds){
	return ds->procs.swap;
}

int64_t DSreadS64(stream_t ds){
	int64_t i;
	stream_read(ds,&i,8);
	if(ds->procs.swap & SWAP_READ) i=bswap_64(i);
	return i;
}

uint64_t DSreadU64(stream_t ds){
	uint64_t i;
	stream_read(ds,&i,8);
	if(ds->procs.swap & SWAP_READ) i=bswap_64(i);
	return i;
}

double DSreadD(stream_t ds){
	double d;
	stream_read(ds,&d,8);
	if(ds->procs.swap & SWAP_READ) d=bswap_64(((uint64_t)d));
	return d;
}

int32_t DSreadS32(stream_t ds){
	int32_t i;
	stream_read(ds,&i,4);
	if(ds->procs.swap & SWAP_READ) i=bswap_32(i);
	return i;
}

uint32_t DSreadU32(stream_t ds){
	uint32_t i;
	stream_read(ds,&i,4);
	if(ds->procs.swap & SWAP_READ) i=bswap_32(i);
	return i;
}

float DSreadF(stream_t ds){
	float f;
	stream_read(ds,&f,4);
	if(ds->procs.swap & SWAP_READ) f=bswap_32(f);
	return f;
}


int16_t DSreadS16(stream_t ds){
	int16_t i;
	stream_read(ds,&i,2);
	if(ds->procs.swap & SWAP_READ) i=bswap_16(i);
	return i;
}

uint16_t DSreadU16(stream_t ds){
	uint16_t i;
	stream_read(ds,&i,2);
	if(ds->procs.swap & SWAP_READ) i=bswap_16(i);
	return i;
}


int8_t DSreadS8(stream_t ds){
	int8_t i;
	stream_read(ds,&i,1);
	return i;
}

uint8_t DSreadU8(stream_t ds){
	uint8_t i;
	stream_read(ds,&i,1);
	return i;
}

void DSreadS(stream_t ds,char *s,int maxlen){
	uint16_t len=DSreadU16(ds);
	if (len>=maxlen) {
		Fatal(0,error,"string overflow (%d>=%d)",len,maxlen);
	}
	stream_read(ds,s,len);
	s[len]=0;
}

char* DSreadSA(stream_t ds){
	uint16_t len=DSreadU16(ds);
	char*s=RTmalloc(len+1);
	stream_read(ds,s,len);
	s[len]=0;
	return s;
}

char* DSreadLN(stream_t ds){
	char tmp[4096];
	for(int i=0;i<4096;i++){
		size_t res=stream_read_max(ds,tmp+i,1);
		if (res==0 || tmp[i]=='\n') {
			char *s=RTmalloc(i+1);
			memcpy(s,tmp,i);
			s[i]=0;
			return s;
		}
	}
	Fatal(0,error,"assumption that line is shorter than 4096 failed");
	return NULL;
}

void DSwriteS64(stream_t ds,int64_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_64(i);
	stream_write(ds,&i,8);
}

void DSwriteU64(stream_t ds,uint64_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_64(i);
	stream_write(ds,&i,8);
}

void DSwriteD(stream_t ds,double d){
	if (ds->procs.swap & SWAP_WRITE) d=bswap_64(((uint64_t)d));
	stream_write(ds,&d,8);
}


void DSwriteS32(stream_t ds,int32_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_32(i);
	stream_write(ds,&i,4);
}

void DSwriteU32(stream_t ds,uint32_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_32(i);
	stream_write(ds,&i,4);
}

void DSwriteF(stream_t ds,float f){
	if (ds->procs.swap & SWAP_WRITE) f=bswap_32(f);
	stream_write(ds,&f,4);
}

void DSwriteS16(stream_t ds,int16_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_16(i);
	stream_write(ds,&i,2);
}

void DSwriteU16(stream_t ds,uint16_t i){
	if (ds->procs.swap & SWAP_WRITE) i=bswap_16(i);
	stream_write(ds,&i,2);
}

void DSwriteS8(stream_t ds,int8_t i){
	stream_write(ds,&i,1);
}

void DSwriteU8(stream_t ds,uint8_t i){
	stream_write(ds,&i,1);
}

void DSwriteC(stream_t ds,uint16_t len,char *c){
	DSwriteU16(ds,len);
	stream_write(ds,c,len);
}

void DSwriteS(stream_t ds,char *s){
	int len=strlen(s);
	if (len>=65536) {
		Fatal(1,error,"string too long for DSwriteS");
	}
	DSwriteU16(ds,len);
	stream_write(ds,s,len);
}

void DSwriteVL(stream_t ds,uint64_t i){
	uint8_t tmp;
	do {
		tmp=i&0x7f;
		i=i>>7;
		if (i) tmp|=0x80;
		DSwriteU8(ds,tmp);
	} while(i);
}
uint64_t DSreadVL(stream_t ds){
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

void DSautoSwap(stream_t ds){
	DSsetSwap(ds,SWAP_NONE);
	if(stream_writable(ds)) {
		DSwriteU16(ds,0x0102);
	}
	if(stream_readable(ds)){
		uint16_t test=DSreadU16(ds);
		switch(test){
		case 0x0201:
			DSsetSwap(ds,SWAP_READ);
			break;
		case 0x0102:
			break;
		default:
			Fatal(1,error,"byte order detection value missing");
		}
	}
}

stream_t stream_add_code(stream_t s,char* code){
	char*tail=strchr(code,'|');
	if(tail){
		s=stream_add_code(s,tail+1);
	}
	if(strlen(code)==0) return s;
	if(!strncmp(code,"diff32",6)){
		return stream_diff32(s);
	}
	if(!strncmp(code,"native",6)){
		DSsetSwap(s,SWAP_NONE);
		return s;
	}
	if(!strncmp(code,"gzip",4)){
		int level=Z_DEFAULT_COMPRESSION;
		sscanf(code+4,"%d",&level);
		return stream_gzip(s,Z_DEFAULT_COMPRESSION,8192);
	}
	if(!strncmp(code,"gunzip",6)){
		int level=Z_DEFAULT_COMPRESSION;
		sscanf(code+6,"%d",&level);
		return stream_gunzip(s,Z_DEFAULT_COMPRESSION,8192);
	}
	Fatal(1,error,"unknown code prefix %s",code);
	return NULL;
}

stream_t stream_setup(stream_t s,char* code){
	int detect=!strcmp(code,"auto");
	if(stream_writable(s)) {
		if(!detect) DSwriteS(s,code);
	}
	if (detect) {
		char code[1024];
		DSreadS(s,code,1024);
		return stream_buffer(stream_add_code(s,code),4096);
	} else {
		return stream_buffer(stream_add_code(s,code),4096);
	}
}

