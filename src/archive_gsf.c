#include "config.h"
#include <stdlib.h>
#include <string.h>

#include "ghf.h"
#include "arch_object.h"
#include "stream_object.h"
#include "stream.h"
#include "runtime.h"
#include "stringindex.h"

#define MAX_READ 10

struct archive_s {
	struct archive_obj procs;
	stream_t ds;
	int count;
	string_index_t streams;
	// fixed arrays for buffers, at most one buffer per stream.
	int buf_next[MAX_READ];
	int buf_len[MAX_READ];
	int buf_eof[MAX_READ];
	void* buffer[MAX_READ];
};

struct stream_s {
	struct stream_obj procs;
	archive_t archive;
	uint32_t id;
};

static void wr_chan_write(stream_t stream,void*buf,size_t count){
	ghf_write_data(stream->archive->ds,stream->id,buf,count);
}

static void wr_chan_close(stream_t *stream){
	ghf_write_end((*stream)->archive->ds,(*stream)->id);
	free(*stream);
	*stream=NULL;
}

static void wr_chan_flush(stream_t stream){
	stream_flush(stream->archive->ds);
}


static stream_t gsf_write(archive_t archive,char *name){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->procs.write=wr_chan_write;
	s->procs.flush=wr_chan_flush;
	s->procs.close=wr_chan_close;
	s->id=archive->count;
	s->archive=archive;
	ghf_write_new(archive->ds,s->id,name);
	archive->count++;
	return stream_buffer(s,4096);
}

static void read_until(archive_t arch,char*name,uint32_t rqid){
	for(;;){
/*
		if (name) {
			Warning(info,"looking for file %s",name);
		} else {
			Warning(info,"looking for file no %d",rqid);
		}
*/
		if (name && SI_INDEX_FAILED!=SIlookup(arch->streams,name)) return;
		if (name==NULL && (arch->buffer[rqid]||arch->buf_eof[rqid])) return;
		uint8_t tag=DSreadU8(arch->ds);
		switch(tag){
			case GHF_NEW: {
				char *name;
				uint32_t id;
				ghf_read_new(arch->ds,&id,&name);
				//Warning(info,"new file %d %s",id,name);
				if (id>=MAX_READ) Fatal(1,error,"no more than %d streams supported",MAX_READ);
				SIputAt(arch->streams,name,id);
				arch->buffer[id]=NULL;
				arch->buf_eof[id]=0;
				free(name);
				continue;
			}
			case GHF_END: {
				uint32_t id;
				ghf_read_end(arch->ds,&id);
				//Warning(info,"file ends %d",id);
				arch->buf_eof[id]=1;
				continue;
			}
			case GHF_DAT: {
				uint32_t id;
				size_t len;
				ghf_read_data(arch->ds,&id,&len);
				//Warning(info,"file data %d: %d bytes",id,len);
				if (arch->buffer[id]) Fatal(1,error,"cannot have more than one buffer");
				arch->buffer[id]=RTmalloc(len);
				stream_read(arch->ds,arch->buffer[id],len);
				arch->buf_next[id]=0;
				arch->buf_len[id]=len;
				continue;
			}
			default:
				ghf_skip(arch->ds,tag);
				Fatal(1,error,"archive ended");
				return;
		}
	}
}
static void rd_chan_close(stream_t *stream){
	free(*stream);
	*stream=NULL;
}
static size_t rd_chan_read_max(stream_t stream,void*buf,size_t count){
	//Warning(info,"reading up to %d from chan %d",count,stream->id);
	size_t res=0;
	int *next=&stream->archive->buf_next[stream->id];
	int *len=&stream->archive->buf_len[stream->id];
	while(res<count){
		//Warning(info,"got %d of %d",res,count);
		if (stream->archive->buffer[stream->id]==NULL) {
			if (stream->archive->buf_eof[stream->id]) {
				//Warning(info,"returning %d",res);
				return res;
			}
			read_until(stream->archive,NULL,stream->id);
			if (stream->archive->buf_eof[stream->id]) {
				//Warning(info,"returning %d",res);
				return res;
			}
		}
		int wanted=count-res;
		int avail=*len-*next;
		if (wanted<avail){
			memcpy(buf+res,stream->archive->buffer[stream->id]+(*next),wanted);
			*next+=wanted;
			//Warning(info,"returning %d",res);
			return res;
		} else {
			memcpy(buf+res,stream->archive->buffer[stream->id]+(*next),avail);
			res+=avail;
			free(stream->archive->buffer[stream->id]);
			stream->archive->buffer[stream->id]=NULL;
		}
	}
	//Warning(info,"returning %d",res);
	return res;
}
static stream_t gsf_read(archive_t arch,char*name){
	read_until(arch,name,0);
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->procs.close=rd_chan_close;
	s->procs.read_max=rd_chan_read_max;
	s->id=SIlookup(arch->streams,name);
	s->archive=arch;
	return stream_buffer(s,4096);
}

static stream_t gsf_read_first(archive_t arch,char*name){
	char ident[LTSMIN_PATHNAME_MAX];
	DSreadS(arch->ds,ident,LTSMIN_PATHNAME_MAX);
	if(strncmp(ident,"GSF",3)){
		Fatal(0,error,"I do not recognize %s as a GSF file.",ident);
	}
	Warning(info,"reading a %s file",ident);
	arch->procs.read=gsf_read;
	arch->procs.enumerator=arch_illegal_enum;
	arch->streams=SIcreate();
	return gsf_read(arch,name);
}

static void gsf_close(archive_t *arch){
	if (stream_writable((*arch)->ds)) {
		ghf_write_eof((*arch)->ds);
	}
	stream_close(&((*arch)->ds));
	free(*arch);
	*arch=NULL;
}

struct arch_enum {
	struct archive_enum_obj procs;
	archive_t archive;
};

static int gsf_enumerate(arch_enum_t e,struct arch_enum_callbacks *cb,void*arg){
	archive_t arch=e->archive;
	for(;;){
		uint8_t tag=DSreadU8(arch->ds);
		switch(tag){
			case GHF_NEW: {
				char *name;
				uint32_t id;
				ghf_read_new(arch->ds,&id,&name);
				if(cb->new_item){
					int res=cb->new_item(arg,id,name);
					if (res) return res;
				}
				free(name);
				continue;
			}
			case GHF_END: {
				uint32_t id;
				ghf_read_end(arch->ds,&id);
				if (cb->end_item) {
					int res=cb->end_item(arg,id);
					if (res) return res;
				}
				continue;
			}
			case GHF_DAT: {
				uint32_t id;
				size_t len;
				ghf_read_data(arch->ds,&id,&len);
				char buf[len];
				stream_read(arch->ds,buf,len);
				if (cb->data) {
					int res=cb->data(arg,id,buf,len);
					if (res) return res;
				}
				continue;
			}
			case GHF_EOF: {
				return 0;
			}
			default: {
				ghf_skip(arch->ds,tag);
				continue;
			}
		}
	}
}

static void gsf_enum_free(arch_enum_t *e){
	free(*e);
	*e=NULL;
}

static arch_enum_t gsf_enum(archive_t archive,char *regex){
	if (regex!=NULL) Warning(info,"regex not supported");
	arch_enum_t e=(arch_enum_t)RTmalloc(sizeof(struct arch_enum));
	e->procs.enumerate=gsf_enumerate;
	e->procs.free=gsf_enum_free;
	e->archive=archive;
	char ident[LTSMIN_PATHNAME_MAX];
	DSreadS(archive->ds,ident,LTSMIN_PATHNAME_MAX);
	if(strncmp(ident,"GSF",3)){
		Fatal(0,error,"I do not recognize %s as a GSF file.",ident);
	}
	Warning(info,"reading a %s file",ident);
	archive->procs.read=arch_illegal_read;
	archive->procs.enumerator=arch_illegal_enum;
	return e;
}

static archive_t gsf_create(stream_t s,int rd,int wr){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	arch_init(arch);
	arch->ds=s;
	if (rd) {
		arch->procs.enumerator=gsf_enum;
		arch->procs.read=gsf_read_first;
	}
	if (wr){
		arch->procs.write=gsf_write;
		DSwriteS(arch->ds,"GSF 0.1");
	}
	arch->count=0;
	arch->procs.close=gsf_close;
	return arch;
}

archive_t arch_gsf_read(stream_t s){
	return gsf_create(s,1,0);
}

archive_t arch_gsf_write(stream_t s){
	return gsf_create(s,0,1);
}

archive_t arch_gsf(stream_t s){
	return gsf_create(s,stream_readable(s),stream_writable(s));
}



