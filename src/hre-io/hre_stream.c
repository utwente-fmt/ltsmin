// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <inttypes.h>
#include <stdlib.h>
#include <zlib.h>

#include <hre/unix.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>

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

void stream_close_z(stream_t *stream,uint64_t orig_size){
    (*stream)->procs.close_z(stream,orig_size);
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
    Abort("illegal read max on stream");
    return 0;
}
size_t stream_default_read_max(stream_t stream,void*buf,size_t count){
    stream->procs.read(stream,buf,count);
    return count;
}
void stream_illegal_read(stream_t stream,void*buf,size_t count){
    (void)stream;(void)buf;(void)count;
    Abort("illegal read on stream");
}
void stream_default_read(stream_t stream,void*buf,size_t count){
    size_t res=stream->procs.read_max(stream,buf,count);
    if (res<count) {
        Abort("short read");
    }
}
int stream_illegal_empty(stream_t stream){
    (void)stream;
    Abort("illegal empty on stream");
    return 0;
}
void stream_illegal_write(stream_t stream,void*buf,size_t count){
    (void)stream;(void)buf;(void)count;
    Abort("illegal write on stream");
}
void stream_illegal_flush(stream_t stream){
    (void)stream;
    Abort("illegal flush on stream");
}
void stream_illegal_close(stream_t *stream){
    (void)stream;
    Abort("illegal close on stream");
}
void stream_default_close_z(stream_t *stream,uint64_t orig_size){
    (void)orig_size;
    Debug("ignoring orig size %"PRIu64,orig_size);
    (*stream)->procs.close(stream);
}
void stream_init(stream_t s){
    s->procs.read_max=stream_illegal_read_max;
    s->procs.read=stream_illegal_read;
    s->procs.empty=stream_illegal_empty;
    s->procs.write=stream_illegal_write;
    s->procs.flush=stream_illegal_flush;
    s->procs.close=stream_illegal_close;
    s->procs.close_z=stream_default_close_z;
}

/*************************************************************************/
/* FILE IO functions.                                                    */
/*************************************************************************/

static void file_read(stream_t stream,void*buf,size_t count){
    size_t res=fread(buf,1,count,stream->f);
    if (res<count) Abort("short read");
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
            Abort("unexpected failure");
        };
        return 0;
    }
}

static void file_write(stream_t stream,void*buf,size_t count){
    size_t res=fwrite(buf,1,count,stream->f);
    if (res<count) Abort("short write");
}

static void file_close(stream_t *stream){
    fclose((*stream)->f);
    RTfree(*stream);
    *stream=NULL;
}

static void file_flush(stream_t stream){
    fflush(stream->f);
}

stream_t stream_input(FILE*f){
    stream_t s=(stream_t)RTmallocZero(sizeof(struct stream_s));
    stream_init(s);
    setbuf(f,NULL);
    s->f=f;
    s->procs.read_max=file_read_max;
    s->procs.read=file_read;
    s->procs.empty=file_empty;
    s->procs.close=file_close;
    return s;
}

stream_t stream_output(FILE*f){
    stream_t s=(stream_t)RTmallocZero(sizeof(struct stream_s));
    stream_init(s);
    setbuf(f,NULL);
    s->f=f;
    s->procs.write=file_write;
    s->procs.flush=file_flush;
    s->procs.close=file_close;
    return s;
}

stream_t file_input(char *name){
    FILE*f =fopen(name,"r");
    if (!f) {
        AbortCall("could not open %s for reading",name);
    }
    return stream_input(f);
}

stream_t file_output(char*name){
    FILE*f =fopen(name,"w");
    if (!f) {
        AbortCall("could not open %s for writing",name);
    }
    return stream_output(f);
}

/*************************************************************************/
/* Data Input/Output functions.                                          */
/*************************************************************************/

int64_t DSreadS64(stream_t ds){
    int64_t i;
    stream_read(ds,&i,8);
    return ntoh_64(i);
}

uint64_t DSreadU64(stream_t ds){
    uint64_t i;
    stream_read(ds,&i,8);
    return ntoh_64(i);
}

int32_t DSreadS32(stream_t ds){
    int32_t i;
    stream_read(ds,&i,4);
    return ntoh_32(i);
}

uint32_t DSreadU32(stream_t ds){
    uint32_t i;
    stream_read(ds,&i,4);
    return ntoh_32(i);
}

int16_t DSreadS16(stream_t ds){
    int16_t i;
    stream_read(ds,&i,2);
    return ntoh_16(i);
}

uint16_t DSreadU16(stream_t ds){
    uint16_t i;
    stream_read(ds,&i,2);
    return ntoh_16(i);
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
        Abort("string overflow (%d>=%d)",len,maxlen);
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
    Abort("assumption that line is shorter than 4096 failed");
    return NULL;
}

void DSwriteS64(stream_t ds,int64_t i){
    i=hton_64(i);
    stream_write(ds,&i,8);
}

void DSwriteU64(stream_t ds,uint64_t i){
    i=hton_64(i);
    stream_write(ds,&i,8);
}

void DSwriteS32(stream_t ds,int32_t i){
    i=hton_32(i);
    stream_write(ds,&i,4);
}

void DSwriteU32(stream_t ds,uint32_t i){
    i=hton_32(i);
    stream_write(ds,&i,4);
}

void DSwriteS16(stream_t ds,int16_t i){
    i=hton_16(i);
    stream_write(ds,&i,2);
}

void DSwriteU16(stream_t ds,uint16_t i){
    i=hton_16(i);
    stream_write(ds,&i,2);
}

void DSwriteS8(stream_t ds,int8_t i){
    stream_write(ds,&i,1);
}

void DSwriteU8(stream_t ds,uint8_t i){
    stream_write(ds,&i,1);
}

void DSwriteS(stream_t ds,char *s){
    int len=strlen(s);
    if (len>=65536) {
        Abort("string too long for DSwriteS");
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

stream_t stream_add_code(stream_t s,const char* code){
    if (code==NULL || strlen(code)==0) return s;
    char*tail=strchr(code,'|');
    if(tail){
        s=stream_add_code(s,tail+1);
    }
    if(!strncmp(code,"diff32",6)){
        return stream_diff32(s);
    }
    if(!strncmp(code,"rle32",5)){
        return stream_rle32(s);
    }
    if(!strncmp(code,"gzip",4)){
        int level=Z_DEFAULT_COMPRESSION;
        sscanf(code+4,"%d",&level);
        Debug("gzip level %d",level);
        return stream_gzip(s,level,8192);
    }
    Abort("unknown code prefix %s",code);
    return NULL;
}

stream_t stream_add_decode(stream_t s,const char* code){
    if (code==NULL || strlen(code)==0) return s;
    char*tail=strchr(code,'|');
    if(tail){
        s=stream_add_code(s,tail+1);
    }
    if(!strncmp(code,"diff32",6)){
        return stream_undiff32(s);
    }
    if(!strncmp(code,"rle32",5)){
        return stream_unrle32(s);
    }
    if(!strncmp(code,"gzip",4)){
        int level=Z_DEFAULT_COMPRESSION;
        sscanf(code+4,"%d",&level);
        Debug("gunzip level %d",level);
        return stream_gunzip(s,level,8192);
    }
    Abort("unknown code prefix %s",code);
    return NULL;
}

