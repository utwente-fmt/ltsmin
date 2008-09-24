#include "arch_object.h"
#include "stream_object.h"
#include "data_io.h"
#include "runtime.h"
#include <malloc.h>
#include "ghf.h"

struct archive_s {
	struct archive_obj procs;
	data_stream_t ds;
	int count;
};

struct stream_s {
	struct stream_obj procs;
	archive_t archive;
	uint32_t id;
};

static void chan_write(stream_t stream,void*buf,size_t count){
	ghf_write_data(stream->archive->ds,stream->id,buf,count);
}

static void chan_close(stream_t *stream){
	ghf_write_end((*stream)->archive->ds,(*stream)->id);
	free(*stream);
	*stream=NULL;
}

static void chan_flush(stream_t stream){
	DSflush(stream->archive->ds);
}


static stream_t gsf_write(archive_t archive,char *name){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->procs.write=chan_write;
	s->procs.flush=chan_flush;
	s->procs.close=chan_close;
	s->id=archive->count;
	s->archive=archive;
	ghf_write_new(archive->ds,s->id,name);
	archive->count++;
	return stream_buffer(s,4096);
}

static void gsf_play(archive_t arch,char *regex,struct archive_enum *cb,void*arg){
	{
		char ident[NAME_MAX];
		DSreadS(arch->ds,ident,NAME_MAX);
		if(strncmp(ident,"GSF",3)){
			Fatal(0,error,"I do not recognize %s as a GSF file.",ident);
		}
		Warning(info,"reading a %s file",ident);
	}
	for(;;){
		uint8_t tag=DSreadU8(arch->ds);
		switch(tag){
			case GHF_NEW: {
				char *name;
				uint32_t id;
				ghf_read_new(arch->ds,&id,&name);
				cb->new_item(arg,id,name);
				free(name);
				continue;
			}
			case GHF_END: {
				uint32_t id;
				ghf_read_end(arch->ds,&id);
				cb->end_item(arg,id);
				continue;
			}
			case GHF_DAT: {
				uint32_t id;
				size_t len;
				ghf_read_data(arch->ds,&id,&len);
				char buf[len];
				DSread(arch->ds,buf,len);
				cb->data(arg,id,buf,len);
				continue;
			}
			default:
				ghf_skip(arch->ds,tag);
				Warning(info,"archive ended");
				return;
		}
	}
}

static void gsf_close(archive_t *arch){
	if (DSwritable((*arch)->ds)) {
		ghf_write_eof((*arch)->ds);
	}
	DSclose(&((*arch)->ds));
	free(*arch);
	*arch=NULL;
}


static archive_t gsf_create(stream_t s,int rd,int wr){
	archive_t arch=(archive_t)RTmalloc(sizeof(struct archive_s));
	arch_init(arch);
	arch->ds=DScreate(s,SWAP_NETWORK);
	if (rd) {
		arch->procs.play=gsf_play;
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



