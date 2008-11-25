#include "config.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "arch_object.h"
#include "stream_object.h"
#include "stream.h"
#include "ghf.h"

struct archive_s {
	struct archive_obj procs;
	raf_t raf;
	size_t block_size;
	size_t cluster_size;
	int worker;
	int worker_count;
	char *buffer;
	int pending;
	char *other;
	int block_count;
	int next_cluster;
	int next_block;
	int meta_block;
	uint32_t meta_begin;
	uint32_t data_begin;
	uint32_t *meta;
	uint32_t next_id;
	stream_t  ds;
	uint32_t stream_count;
	uint32_t *blockmap;
	char **name;
	off_t *length;
	uint32_t meta_offset;
};


struct stream_s {
	struct stream_obj procs;
	off_t len;
	uint32_t blocks;
	archive_t arch;
	uint32_t id;
	uint32_t next_block;
};

static void gcf_stream_pre_close(stream_t s,void* buf,size_t count){
	(void)s;(void)buf;(void)count;
	Fatal(0,error,"expected stream to be closed");
}

static void gcf_stream_write(stream_t s,void* buf,size_t count){
	//Warning(info,"gcf_stream_write %u",s->id);
	if (count>s->arch->block_size) Fatal(0,error,"attempt to write more than block size");
	while (s->arch->meta[s->arch->next_block]!=0) s->arch->next_block++; //find empty block.
	int mem_ofs=(s->arch->next_block)*(s->arch->block_size);
	s->arch->meta[s->arch->next_block]=s->id;
	//Warning(info,"block %u.%u assigned to %u",s->arch->next_cluster,s->arch->next_block,s->id);
	s->arch->next_block++;
	memcpy((s->arch->buffer)+mem_ofs,buf,count);
	s->len+=count;
	if (count<s->arch->block_size) s->procs.write=gcf_stream_pre_close;

	// If the cluster if full then we write it to disk.
	while (s->arch->meta[s->arch->next_block]!=0) s->arch->next_block++;
	if (s->arch->next_block==s->arch->block_count){
		//Warning(info,"writing cluster");
		if (s->arch->pending){
			raf_wait(s->arch->raf);
		}
		off_t ofs=(s->arch->cluster_size)*(s->arch->next_cluster);
		raf_async_write(s->arch->raf,s->arch->buffer,s->arch->cluster_size,ofs);
		s->arch->pending=1;
		char*tmp=s->arch->buffer;
		s->arch->buffer=s->arch->other;
		s->arch->other=tmp;
		bzero(s->arch->buffer,s->arch->cluster_size);
		s->arch->next_cluster+=s->arch->worker_count;
		s->arch->meta_block=((s->arch->next_cluster))%(s->arch->block_count);
		s->arch->meta=(uint32_t*)((s->arch->buffer)+((s->arch->meta_offset)+(s->arch->meta_block)*(s->arch->block_size)));
		s->arch->next_block=0;
		s->arch->meta[s->arch->meta_block]=1;
	}
}

static void gcf_stream_close(stream_t *s){
	//Warning(info,"gcf_stream_close %u %llu",(*s)->id,(*s)->len);
	if ((*s)->id >= (*s)->arch->data_begin) {
		ghf_write_len((*s)->arch->ds,(*s)->id,(*s)->len);
	}
	free(*s);
	*s=NULL;
}

static stream_t gcf_create_stream(archive_t arch,uint32_t id){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->procs.close=gcf_stream_close;
	s->procs.write=gcf_stream_write;
	s->len=0;
	s->arch=arch;
	s->id=id;
	return stream_buffer(s,arch->block_size);
}

static void gcf_stream_read_close(stream_t *s){
	free(*s);
	*s=NULL;
}

int gcf_stream_empty(stream_t stream){
	return (stream->len==stream->arch->length[stream->id]);
}

static size_t gcf_stream_read_max(stream_t stream,void*buf,size_t count){
	if (count!=stream->arch->block_size) Fatal(0,error,"incorrect buffering");
	if (stream->id>=stream->arch->data_begin) {
		if(stream->len==stream->arch->length[stream->id]) {
			//Warning(info,"attempt to read from empty file %u",stream->id);
			return 0;
		}
	}
	uint32_t i=stream->next_block;
	while(stream->arch->blockmap[i]!=stream->id) i++;
	stream->next_block=i+1;
	stream->blocks++;
	//Warning(info,"reading block %u at %u %llu",stream->blocks,i,stream->len);
	raf_read(stream->arch->raf,buf,count,i*count);
	stream->len+=count;
	if (stream->id<stream->arch->data_begin) return count;
	if (stream->len<=stream->arch->length[stream->id]) return count;
	size_t res=count - (stream->len-stream->arch->length[stream->id]);
	stream->len=stream->arch->length[stream->id];
	//Warning(info,"file %u at %llu of %llu (res=%u)",stream->id,stream->len,stream->arch->length[stream->id],res);
	return res;
}

static stream_t gcf_read_stream(archive_t arch,uint32_t id){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->procs.close=gcf_stream_read_close;
	s->procs.read_max=gcf_stream_read_max;
	s->procs.empty=gcf_stream_empty;
	s->len=0;
	s->arch=arch;
	s->id=id;
	s->next_block=0;
	s->blocks=0;
	return stream_buffer(s,arch->block_size);
}

static stream_t gcf_write(archive_t archive,char *name){
	stream_t s=gcf_create_stream(archive,archive->next_id);
	ghf_write_new(archive->ds,archive->next_id,name);
	archive->next_id+=archive->worker_count;
	return s;
}

static void gcf_close_write(archive_t *archive){
	#define arch(field) ((*archive)->field)
	ghf_write_eof(arch(ds));
	//Warning(info,"close");
	DSclose(&arch(ds));
	if (arch(pending)){
		raf_wait(arch(raf));
	}
	if (arch(next_block)){
		//Warning(info,"writing last block");
		off_t ofs=arch(cluster_size)*arch(next_cluster);
		raf_write(arch(raf),arch(buffer),arch(cluster_size),ofs);
	}
	raf_close(&arch(raf));
	#undef arch
	free(*archive);
	*archive=NULL;
}

archive_t arch_gcf_create(raf_t raf,size_t block_size,size_t cluster_size,int worker,int worker_count){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	arch_init(arch);
	arch->raf=raf;
	arch->block_size=block_size;
	arch->cluster_size=cluster_size;
	arch->worker=worker;
	arch->worker_count=worker_count;
	arch->block_count=cluster_size/block_size;
	arch->meta_begin=2;
	arch->data_begin=(arch->meta_begin)+worker_count;
	arch->next_cluster=worker;
	arch->buffer=RTmalloc(cluster_size);
	arch->pending=0;
	arch->other=RTmalloc(cluster_size);
	bzero(arch->buffer,cluster_size);
	arch->meta_block=worker%(arch->block_count);
	arch->meta_offset=1024;
	arch->meta=(uint32_t*)((arch->buffer)+((arch->meta_offset)+(arch->meta_block)*(arch->block_size)));
	arch->meta[arch->meta_block]=1;
	arch->next_id=worker+(arch->data_begin);
	arch->next_block=0;
	if(worker==0) {
		raf_resize(raf,0);
		size_t used;
		stream_t  ds=stream_write_mem(arch->buffer,block_size,&used);
		DSwriteS(ds,"GCF 0.1");
		DSwriteU32(ds,arch->block_size);
		DSwriteU32(ds,arch->cluster_size);
		DSwriteU32(ds,arch->meta_begin);
		DSwriteU32(ds,arch->data_begin);
		DSwriteU32(ds,arch->meta_offset);
		DSclose(&ds);
	}
	arch->ds=gcf_create_stream(arch,worker+(arch->meta_begin));
	arch->procs.write=gcf_write;
	arch->procs.close=gcf_close_write;
	return arch;
}

static void gcf_close_read(archive_t *archive){
	#define arch(field) ((*archive)->field)
	free(arch(blockmap));
	free(arch(name));//memory leak: should free names as well.
	free(arch(length));
	raf_close(&arch(raf));
	#undef arch
	free(*archive);
	*archive=NULL;
}

struct arch_enum {
	struct archive_enum_obj procs;
	archive_t archive;
	uint32_t next_stream;
};

static int gcf_enumerate(arch_enum_t e,struct arch_enum_callbacks *cb,void*arg){
	if (cb->data!=NULL) Fatal(1,error,"cannot enumerate data");
	while(e->next_stream<e->archive->stream_count){
		if(e->archive->name[e->next_stream]!=NULL && cb->new_item) {
			int res=cb->new_item(arg,e->next_stream,e->archive->name[e->next_stream]);
			e->next_stream++;
			if (res) return res;
		} else {
			e->next_stream++;
		}
	}
	return 0;
}

static void gcf_enum_free(arch_enum_t *e){
	free(*e);
	*e=NULL;
}

static arch_enum_t gcf_enum(archive_t archive,char *regex){
	if (regex!=NULL) Warning(info,"regex not supported");
	arch_enum_t e=(arch_enum_t)RTmalloc(sizeof(struct arch_enum));
	e->procs.enumerate=gcf_enumerate;
	e->procs.free=gcf_enum_free;
	e->archive=archive;
	e->next_stream=archive->data_begin;
	return e;
}

static stream_t gcf_read(archive_t archive,char *name){
	uint32_t i=0;
	for(i=0;i<archive->stream_count;i++){
		if(archive->name[i]!=NULL && !strcmp(name,archive->name[i])){
			return gcf_read_stream(archive,i);
		}
	}
	Fatal(0,error,"stream %s not found",name);
	return NULL;
}


archive_t arch_gcf_read(raf_t raf){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	arch_init(arch);
	arch->raf=raf;
	{
		char buf[4096];
		size_t used;
		char ident[LTSMIN_PATHNAME_MAX];
		raf_read(raf,buf,4096,0);
		stream_t  ds=stream_read_mem(buf,4096,&used);
		DSreadS(ds,ident,LTSMIN_PATHNAME_MAX);
		if(strncmp(ident,"GCF",3)){
			Fatal(0,error,"I do not recognize %s as a GCF file.",ident);
		}
		//Warning(info,"reading a %s file",ident);
		arch->block_size=DSreadU32(ds);
		arch->cluster_size=DSreadU32(ds);
		arch->meta_begin=DSreadU32(ds);
		arch->data_begin=DSreadU32(ds);
		arch->meta_offset=DSreadU32(ds);
		DSclose(&ds);
	}
	arch->block_count=arch->cluster_size/arch->block_size;
	uint32_t cluster_count=(uint32_t)(raf_size(raf)/((off_t)arch->cluster_size));
	//Warning(info,"there are %d clusters",cluster_count);
	arch->blockmap=(uint32_t*)RTmalloc(4*cluster_count*(arch->block_count));
	uint32_t i;
	for(i=0;i<cluster_count;i++){
		off_t ofs=(i%arch->block_count)*arch->block_size + i*arch->cluster_size + arch->meta_offset;
		//Warning(info,"reading meta from offset %x",ofs);
		raf_read(raf,arch->blockmap+(i*arch->block_count),4*arch->block_count,ofs);
	}
	uint32_t stream_count=0;
	for(i=0;i<cluster_count;i++){
		for(int j=0;j<arch->block_count;j++){
			if (arch->blockmap[i*arch->block_count+j]>=stream_count) {
				stream_count=arch->blockmap[i*arch->block_count+j]+1;
			}
			//printf("block %u (%u.%d) belongs to %u\n",i*arch->block_count+j,i,j,arch->blockmap[i*arch->block_count+j]);
		}
	}
	arch->stream_count=stream_count;
	arch->name=(char**)RTmalloc(stream_count*sizeof(char*));
	arch->length=(off_t*)RTmalloc(stream_count*sizeof(off_t));
	for(i=0;i<stream_count;i++){
		arch->name[i]=NULL;
	}
	for(i=arch->meta_begin;i<arch->data_begin;i++){
		//Warning(info,"scanning meta stream %u",i);
		stream_t  ds=gcf_read_stream(arch,i);
		for(;;){
			uint8_t tag=DSreadU8(ds);
			if (tag==GHF_EOF) break;
			switch(tag){
			case GHF_NEW: {
				uint32_t id;
				char*name;
				ghf_read_new(ds,&id,&name);
				arch->name[id]=name;
				//Warning(info,"file %u is %s",id,name);
				continue;
			}
			case GHF_LEN: {
				uint32_t id;
				off_t len;
				ghf_read_len(ds,&id,&len);
				arch->length[id]=len;
				//Warning(info,"file %u contains %lld bytes",id,len);
				continue;
			}
			default:
				ghf_skip(ds,tag);
			}
		}
		//Warning(info,"finished meta stream %u",i);
		DSclose(&ds);
		//Warning(info,"closed meta stream %u",i);
	}
	arch->procs.close=gcf_close_read;
	arch->procs.read=gcf_read;
	arch->procs.enumerator=gcf_enum;
	return arch;
}






