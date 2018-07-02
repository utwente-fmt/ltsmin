// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <hre-io/arch_object.h>
#include <hre-io/gcf_common.h>
#include <hre-io/user.h>
#include <hre-io/stream_object.h>

struct archive_s {
    struct archive_obj procs;
    raf_t raf;
    size_t block_size;
    size_t cluster_size;
    size_t align;
    int top_free;
    int worker;
    int worker_count;
    char *buffer;
    int next_cluster;
    int next_id;
    size_t alloc_used;
    stream_t alloc_stream;
    stream_t meta_stream;
};


struct stream_s {
    struct stream_obj procs;
    off_t len;
    uint32_t blocks;
    archive_t arch;
    int id;
    uint32_t next_block;
};

static void gcf_stream_write(stream_t s,void* buf,size_t count){
    if (count>s->arch->block_size) Abort("attempt to write more than block size");
    if (count==s->arch->block_size && s->arch->align) {
        // align a full block.
        s->arch->top_free-=s->arch->top_free%s->arch->align;
    }
    // available is free space minus end-of-alloc (1) and worst case alloc message(15);
    int avail=(s->arch->top_free) - (s->arch->alloc_used) - 16;
    if (avail < (int)count){
        // not enough free space: ship out current cluster.
        DSwriteVL(s->arch->alloc_stream,0);
        off_t ofs=(s->arch->cluster_size)*(s->arch->next_cluster);
        raf_write(s->arch->raf,s->arch->buffer,s->arch->cluster_size,ofs);
        s->arch->next_cluster+=s->arch->worker_count;
        s->arch->alloc_used=0;
        s->arch->top_free=s->arch->cluster_size;
        memset(s->arch->buffer, 0, s->arch->cluster_size);
    }
    s->arch->top_free-=count;
    memcpy((s->arch->buffer)+s->arch->top_free,buf,count);
    DSwriteVL(s->arch->alloc_stream,s->id);
    DSwriteVL(s->arch->alloc_stream,s->arch->top_free);
    DSwriteVL(s->arch->alloc_stream,count);
    s->len+=count;
}

static void gcf_stream_close(stream_t *s){
    Debug("gcf_stream_close %u %jd",(*s)->id,(intmax_t)(*s)->len);
    if ((*s)->id > (*s)->arch->worker_count) {
        ghf_write_len((*s)->arch->meta_stream,(*s)->id,(*s)->len);
    }
    RTfree(*s);
    *s=NULL;
}

static void gcf_stream_close_z(stream_t *s,uint64_t orig_size){
    Debug("stream %u: compressed %jd, original %"PRIu64,(*s)->id,(intmax_t)(*s)->len,orig_size);
    if ((*s)->id > (*s)->arch->worker_count) {
        ghf_write_orig((*s)->arch->meta_stream,(*s)->id,orig_size);
    }
    gcf_stream_close(s);
}

static stream_t gcf_create_stream(archive_t arch,uint32_t id){
    stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
    stream_init(s);
    s->procs.close=gcf_stream_close;
    s->procs.close_z=gcf_stream_close_z;
    s->procs.write=gcf_stream_write;
    s->len=0;
    s->arch=arch;
    s->id=id;
    return stream_buffer(s,arch->block_size);
}

static stream_t gcf_write_raw(archive_t archive,char *name,char *code){
    stream_t s=gcf_create_stream(archive,archive->next_id);
    ghf_write_new(archive->meta_stream,archive->next_id,name);
    if (code) {
        ghf_write_code(archive->meta_stream,archive->next_id,code);
    }
    archive->next_id+=archive->worker_count;
    return s;
}

static stream_t gcf_write(archive_t archive,char *name,char *code){
    stream_t s=gcf_write_raw(archive,name,code);
    if (code) {
        s=stream_add_code(s,code);
    }
    return s;
}

static void gcf_close_write(archive_t *archive){
    #define arch(field) ((*archive)->field)
    ghf_write_eof(arch(meta_stream));
    DSclose(&arch(meta_stream));
    if (arch(alloc_used)){
        off_t ofs=arch(cluster_size)*arch(next_cluster);
        raf_write(arch(raf),arch(buffer),arch(cluster_size),ofs);
    }
    raf_close(&arch(raf));
    #undef arch
    RTfree(*archive);
    *archive=NULL;
}

archive_t arch_gcf_create(raf_t raf,size_t block_size,size_t cluster_size,int worker,int worker_count){
    archive_t arch=(archive_t)HREmallocZero(NULL,sizeof(struct archive_s));
    arch_init(arch);
    arch->raf=raf;
    arch->cluster_size=cluster_size;
    arch->block_size=block_size;
    if (block_size>=4096) {
        arch->align=4096;
    } else {
        arch->align=0;
    }
    arch->worker=worker;
    arch->worker_count=worker_count;
    arch->next_cluster=worker;
    arch->buffer=HREmallocZero(NULL,cluster_size);
    arch->next_id=worker+1+worker_count; // skip 0: free and one meta stream per worker.
    arch->alloc_stream=stream_write_mem(arch->buffer,cluster_size,&arch->alloc_used);
    arch->top_free=cluster_size;
    if(worker==0) {
        raf_resize(raf,0);
        DSwriteS(arch->alloc_stream,"GCF 0.3");
        DSwriteU32(arch->alloc_stream,cluster_size);
        DSwriteU32(arch->alloc_stream,block_size);
        DSwriteU32(arch->alloc_stream,worker_count);
    }
    arch->meta_stream=gcf_create_stream(arch,worker+1);
    arch->procs.write=gcf_write;
    arch->procs.write_raw=gcf_write_raw;
    arch->procs.close=gcf_close_write;
    return arch;
}
