// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <fnmatch.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <hre/unix.h>
#include <hre-io/arch_object.h>
#include <hre-io/gcf_common.h>
#include <hre-io/stream_object.h>
#include <hre-io/user.h>
#include <util-lib/balloc.h>
#include <util-lib/dynamic-array.h>
#include <hre/stringindex.h>

typedef struct part_list_s *part_list_t;
struct part_list_s {
    uint32_t cluster;
    uint32_t offset;
    uint32_t length;
    part_list_t next;
};

typedef struct {
    uint32_t id;
    char* code;
    uint64_t size;
    uint64_t compressed;
    part_list_t data_head;
    part_list_t data_tail;
} gcf_info_s;

struct archive_s {
    struct archive_obj procs;
    raf_t raf;
    size_t block_size;
    size_t cluster_size;
    array_manager_t idman;
    gcf_info_s *gcf_info;
    allocater_t list_alloc;
    string_index_t stream_index;
};


struct stream_s {
    struct stream_obj procs;
    archive_t arch;
    part_list_t data;
};

static void gcf_stream_read_close(stream_t *s){
    free(*s);
    *s=NULL;
}

static int gcf_stream_empty(stream_t stream){
    return (stream->data==NULL);
}

static size_t gcf_stream_read_max(stream_t stream,void*buf,size_t count){
    if (count!=stream->arch->block_size) Abort("incorrect buffering");
    if (stream->data==NULL) return 0;
    if (stream->data->length>count) Abort("part bigger than block");
    off_t offset=stream->arch->cluster_size*stream->data->cluster+stream->data->offset;
    size_t res=stream->data->length;
    raf_read(stream->arch->raf,buf,res,offset);
    stream->data=stream->data->next;
    return res;
}

static stream_t gcf_read_stream(archive_t arch,uint32_t id){
    stream_t s=(stream_t)HREmalloc(NULL,sizeof(struct stream_s));
    stream_init(s);
    s->procs.close=gcf_stream_read_close;
    s->procs.read_max=gcf_stream_read_max;
    s->procs.read=stream_default_read;
    s->procs.empty=gcf_stream_empty;
    s->arch=arch;
    s->data=arch->gcf_info[id].data_head;
    return stream_buffer(s,arch->block_size);
}

static void gcf_close_read(archive_t *archive){
	#define arch(field) ((*archive)->field)
    // lots of freeing TODO.
    raf_close(&arch(raf));
	#undef arch
    free(*archive);
    *archive=NULL;
}

struct arch_enum {
    struct archive_enum_obj procs;
    char* pattern;
    archive_t archive;
    uint32_t next_stream;
};

static int gcf_enumerate(arch_enum_t e,struct arch_enum_callbacks *cb,void*arg){
    if (cb->data!=NULL) Abort("cannot enumerate data");
    uint32_t N=SIgetRange(e->archive->stream_index);
    while(e->next_stream<N){
        char*name= SIget(e->archive->stream_index,e->next_stream);
        if(name!=NULL){
            if (e->pattern && fnmatch(e->pattern,name,0)) {
                e->next_stream++;
                continue;
            }
            if (cb->new_item) {
                int res=cb->new_item(arg,e->next_stream,name);
                if (res) {
                    e->next_stream++;
                    return res;
                }
            }
            if (cb->stat){
                struct archive_item_s item={
                    .name=name,
                    .code=e->archive->gcf_info[e->next_stream].code,
                    .compressed=e->archive->gcf_info[e->next_stream].compressed,
                    .length=e->archive->gcf_info[e->next_stream].size
                };
                int res=cb->stat(arg,e->next_stream,&item);
                if (res) {
                    e->next_stream++;
                    return res;
                }
            }
        }
        e->next_stream++;
    }
    return 0;
}

static void gcf_enum_free(arch_enum_t *e){
    free(*e);
    *e=NULL;
}

static arch_enum_t gcf_enum(archive_t archive,char *regex){
    arch_enum_t e=(arch_enum_t)HREmalloc(NULL,sizeof(struct arch_enum));
    e->procs.enumerate=gcf_enumerate;
    e->procs.free=gcf_enum_free;
    e->pattern=regex;
    e->archive=archive;
    e->next_stream=0;
    return e;
}

static int gcf_contains(archive_t archive,char *name){
    int id=SIlookup(archive->stream_index,name);
    return (id!=SI_INDEX_FAILED);
}

static stream_t gcf_read(archive_t archive,char *name){
    int id=SIlookup(archive->stream_index,name);
    if (id==SI_INDEX_FAILED) Abort("stream %s not found",name);
    stream_t s=gcf_read_stream(archive,id);
    if (archive->gcf_info[id].code){
        s=stream_add_code(s,archive->gcf_info[id].code);
    }
    return s;
}

static stream_t gcf_read_raw(archive_t archive,char *name,char**code){
    int id=SIlookup(archive->stream_index,name);
    if (id==SI_INDEX_FAILED) Abort("stream %s not found",name);
    stream_t s=gcf_read_stream(archive,id);
    if (code) {
        if (archive->gcf_info[id].code){
            *code=strdup(archive->gcf_info[id].code);
        } else {
            *code=NULL;
        }
    }
    return s;
}

archive_t arch_gcf_read(raf_t raf){
    archive_t arch=(archive_t)HREmalloc(NULL,sizeof(struct archive_s));
    arch_init(arch);
    arch->raf=raf;

    char buf[4096];
    size_t used;
    char ident[LTSMIN_PATHNAME_MAX];
    raf_read(raf,buf,4096,0);
    stream_t  ds=stream_read_mem(buf,4096,&used);
    DSreadS(ds,ident,LTSMIN_PATHNAME_MAX);
    if(strncmp(ident,"GCF",3)){
        Abort("I do not recognize %s as a GCF file.",ident);
    }
    if(strcmp(ident,"GCF 0.1")==0){
        // version 0.1 uses transparent compression.
        arch_set_transparent_compression(arch);
    } else if (strcmp(ident,"GCF 0.2")==0) {
        // version 0.2 puts compression in the meta stream.
    } else if (strcmp(ident,"GCF 0.3")==0) {
        // version 0.3 uses a different allocation format.
    } else {
        Abort("This implementation does not support %s.",ident);
    }
    uint32_t meta_begin;
    uint32_t data_begin;
    arch->idman=create_manager(64);
    arch->gcf_info=NULL;
    ADD_ARRAY(arch->idman,arch->gcf_info,gcf_info_s);
    arch->list_alloc=BAcreate(sizeof(gcf_info_s),65536);
    arch->stream_index=SIcreate();
    if (strcmp(ident,"GCF 0.1")==0 || strcmp(ident,"GCF 0.2")==0) {
        //Debug("reading a %s file",ident);
        arch->block_size=DSreadU32(ds);
        arch->cluster_size=DSreadU32(ds);
        meta_begin=DSreadU32(ds);
        data_begin=DSreadU32(ds);
        uint32_t meta_offset=DSreadU32(ds);
        DSclose(&ds);

        uint32_t block_count=arch->cluster_size/arch->block_size;
        uint32_t cluster_count=(uint32_t)(raf_size(raf)/((off_t)arch->cluster_size));

        Debug("archive contains %d cluster(s)",cluster_count);
        int convert=0;
        for(uint32_t i=0;i<cluster_count;i++){
            off_t ofs=(i%block_count)*arch->block_size + i*arch->cluster_size + meta_offset;
            //Debug("reading meta from offset %x",ofs);
            uint32_t blockmap[block_count];
            raf_read(raf,blockmap,4*block_count,ofs);
            if (i==0 && blockmap[0]==0x01000000) {
                Debug("Converting the index from network to host byte order.");
                convert=1;
            }
            for(uint32_t j=0;j<block_count;j++){
                if (convert) blockmap[j]=bswap_32(blockmap[j]);
                ensure_access(arch->idman,blockmap[j]);
                if (blockmap[j]<meta_begin) continue;
                part_list_t part=BAget(arch->list_alloc);
                part->cluster=i;
                part->offset=j*arch->block_size;
                part->length=arch->block_size;
                part->next=NULL;
                if (arch->gcf_info[blockmap[j]].data_head==NULL){
                    arch->gcf_info[blockmap[j]].data_head=part;
                } else {
                    arch->gcf_info[blockmap[j]].data_tail->next=part;
                }
                arch->gcf_info[blockmap[j]].data_tail=part;
            }
        }
    } else if (strcmp(ident,"GCF 0.3")==0) {
        arch->cluster_size=DSreadU32(ds);
        arch->block_size=DSreadU32(ds);
        meta_begin=1;
        data_begin=1+DSreadU32(ds);
        ensure_access(arch->idman,data_begin);
        uint32_t cluster_count=(uint32_t)(raf_size(raf)/((off_t)arch->cluster_size));
        if ((uint32_t)(raf_size(raf)%((off_t)arch->cluster_size))) cluster_count++;
        for(uint32_t i=0;i<cluster_count;i++){
            if (i) {
                used=0;
                Debug("reading alloc from offset %x",i*arch->cluster_size);
                raf_read(raf,buf,4096,i*arch->cluster_size);
            }
            uint32_t id,offset,length;
            for(;(id=DSreadVL(ds));){
                ensure_access(arch->idman,id);
                offset=DSreadVL(ds);
                length=DSreadVL(ds);
                part_list_t part=BAget(arch->list_alloc);
                part->cluster=i;
                part->offset=offset;
                part->length=length;
                part->next=NULL;
                if (arch->gcf_info[id].data_head==NULL){
                    arch->gcf_info[id].data_head=part;
                } else {
                    arch->gcf_info[id].data_tail->next=part;
                }
                arch->gcf_info[id].data_tail=part;
            }
        }
        DSclose(&ds);
    } else {
        Abort("unknown GCF version: %s",ident);
    }
    for(uint32_t i=meta_begin;i<data_begin;i++){
        Debug("scanning meta stream %u",i);
        stream_t  ds=gcf_read_stream(arch,i);
        for(;;){
            uint8_t tag=DSreadU8(ds);
            if (tag==GHF_EOF) break;
            switch(tag){
            case GHF_NEW: {
                uint32_t id;
                char*name;
                ghf_read_new(ds,&id,&name);
                SIputAt(arch->stream_index,name,id);
                Debug("file %u is %s",id,name);
                continue;
            }
            case GHF_CODE: {
                uint32_t id;
                char*code;
                ghf_read_code(ds,&id,&code);
                arch->gcf_info[id].code=code;
                Debug("file %u compression is %s",id,code);
                continue;
            }
            case GHF_LEN: {
                uint32_t id;
                off_t len;
                ghf_read_orig(ds,&id,&len);
                arch->gcf_info[id].compressed=len;
                Debug("file %u compressed to %lld bytes",id,len);
                uint32_t tmp=len%arch->block_size;
                if (tmp) arch->gcf_info[id].data_tail->length=tmp;
                if (arch->gcf_info[id].code==NULL || strlen(arch->gcf_info[id].code)==0){
                    Debug("assuming file %u contains %lld bytes",id,len);
                    arch->gcf_info[id].size=len;
                }
                continue;
            }
            case GHF_ORIG: {
                uint32_t id;
                off_t len;
                ghf_read_len(ds,&id,&len);
                arch->gcf_info[id].size=len;
                Debug("file %u contains %lld bytes",id,len);
                continue;
            }
            default:
                ghf_skip(ds,tag);
            }
        }
        //Debug("finished meta stream %u",i);
        DSclose(&ds);
        //Debug("closed meta stream %u",i);
    }
    Debug("meta data loaded");
    arch->procs.contains=gcf_contains;
    arch->procs.close=gcf_close_read;
    arch->procs.read=gcf_read;
    arch->procs.read_raw=gcf_read_raw;
    arch->procs.enumerator=gcf_enum;
    return arch;
}
