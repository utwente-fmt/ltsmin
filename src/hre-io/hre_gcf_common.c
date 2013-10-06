// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <stdlib.h>

#include <hre-io/user.h>
#include <hre-io/gcf_common.h>

void ghf_write_new(stream_t ds,uint32_t id,char*name){
    DSwriteU8(ds,GHF_NEW);
    DSwriteVL(ds,id);
    DSwriteS(ds,name);
}
void ghf_read_new(stream_t ds,uint32_t *id,char**name){
    uint32_t i=DSreadVL(ds);
    char*n=DSreadSA(ds);
    if(id) *id=i;
    if(name) *name=n; else RTfree(n);
}

void ghf_write_code(stream_t ds,uint32_t id,char*name){
    DSwriteU8(ds,GHF_CODE);
    DSwriteVL(ds,id);
    DSwriteS(ds,name);
}
void ghf_read_code(stream_t ds,uint32_t *id,char**name){
    uint32_t i=DSreadVL(ds);
    char*n=DSreadSA(ds);
    if(id) *id=i;
    if(name) *name=n; else RTfree(n);
}


void ghf_write_end(stream_t ds,uint32_t id){
    DSwriteU8(ds,GHF_END);
    DSwriteVL(ds,id);
}
void ghf_read_end(stream_t ds,uint32_t *id){
    uint32_t i=DSreadVL(ds);
    if(id) *id=i;
}

void ghf_write_eof(stream_t ds){
    DSwriteU8(ds,GHF_EOF);
}

void ghf_write_data(stream_t ds,uint32_t id,void*buf,size_t count){
    if (count==0) return;
    DSwriteU8(ds,GHF_DAT);
    DSwriteVL(ds,id);
    DSwriteVL(ds,count-1);
    DSwrite(ds,buf,count);
}
void ghf_read_data(stream_t ds,uint32_t *id,size_t*count){
    uint32_t i=DSreadVL(ds);
    if(id) *id=i;
    size_t size=DSreadVL(ds);
    if (count) *count=size+1;
    // Actual data to be read by the user.
}

void ghf_write_len(stream_t ds,uint32_t id,off_t len){
    DSwriteU8(ds,GHF_LEN);
    DSwriteVL(ds,id);
    DSwriteVL(ds,len);
}

void ghf_read_len(stream_t ds,uint32_t *id,off_t *len){
    uint32_t i=DSreadVL(ds);
    if(id) *id=i;
    off_t size=DSreadVL(ds);
    if(len) *len=size;
}

void ghf_write_orig(stream_t ds,uint32_t id,off_t len){
    DSwriteU8(ds,GHF_ORIG);
    DSwriteVL(ds,id);
    DSwriteVL(ds,len);
}

void ghf_read_orig(stream_t ds,uint32_t *id,off_t *len){
    uint32_t i=DSreadVL(ds);
    if(id) *id=i;
    off_t size=DSreadVL(ds);
    if(len) *len=size;
}

void ghf_skip(stream_t ds,uint8_t tag){
    switch(tag){
    case GHF_NEW:
        ghf_read_new(ds,NULL,NULL);
        return;
    case GHF_END:
        ghf_read_end(ds,NULL);
        return;
    case GHF_EOF:
        return;
    case GHF_DAT:{
        size_t size;
        ghf_read_data(ds,NULL,&size);
        char buf[size];
        DSread(ds,buf,size);
        return;
    }
    case GHF_LEN:
        ghf_read_len(ds,NULL,NULL);
        return;
    default:
        Abort("unknown tag %d",(int)tag);
    }
}
