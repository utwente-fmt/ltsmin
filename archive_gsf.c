#include "arch_object.h"
#include "stream_object.h"
#include "data_io.h"
#include "runtime.h"
#include <malloc.h>

#define GSF_NEW 0x01
#define GSF_END 0x02
#define GSF_EOF 0x03
#define GSF_DAT 0x04

struct archive_s {
	struct archive_obj procs;
	data_stream_t ds;
	int count;
};

struct stream_s {
	struct stream_obj procs;
	archive_t archive;
	uint16_t id;
};

static void chan_write(stream_t stream,void*buf,size_t count){
	if (count==0) return;
	DSwriteU8(stream->archive->ds,GSF_DAT);
	DSwriteU16(stream->archive->ds,stream->id);
	DSwriteVL(stream->archive->ds,count-1);
	DSwrite(stream->archive->ds,buf,count);
}

static void chan_close(stream_t *stream){
	DSwriteU8((*stream)->archive->ds,GSF_END);
	DSwriteU16((*stream)->archive->ds,(*stream)->id);
	free(*stream);
	*stream=NULL;
}

static void chan_flush(stream_t stream){
	DSflush(stream->archive->ds);
}


static stream_t gsf_write(archive_t archive,char *name){
	stream_t s=(stream_t)malloc(sizeof(struct stream_s));
	if (s==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	s->procs.read_max=stream_illegal_io_try;
	s->procs.read=stream_illegal_io_op;
	s->procs.empty=stream_illegal_int;
	s->procs.write=chan_write;
	s->procs.flush=chan_flush;
	s->procs.close=chan_close;
	s->id=archive->count;
	s->archive=archive;
	DSwriteU8(archive->ds,GSF_NEW);
	DSwriteU16(archive->ds,s->id);
	DSwriteS(archive->ds,name);
	archive->count++;
	return stream_buffer(s,4096,4096);
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
			case GSF_NEW: {
				char name[NAME_MAX];
				uint16_t id=DSreadU16(arch->ds);
				DSreadS(arch->ds,name,NAME_MAX);
				cb->new_item(arg,id,name);
				continue;
			}
			case GSF_END: {
				uint16_t id=DSreadU16(arch->ds);
				cb->end_item(arg,id);
				continue;
			}
			case GSF_DAT: {
				uint16_t id=DSreadU16(arch->ds);
				size_t len=DSreadVL(arch->ds)+1;
				char buf[len];
				DSread(arch->ds,buf,len);
				cb->data(arg,id,buf,len);
				continue;
			}
			case GSF_EOF: {
				return;
			}
			default:
				Fatal(0,error,"bad tag %d",(int)tag);
		}
	}
}

static void gsf_close(archive_t *arch){
	if ((*arch)->procs.write!=NULL) {
		DSwriteU8((*arch)->ds,GSF_EOF);
	}
	DSclose(&((*arch)->ds));
	free(*arch);
	*arch=NULL;
}

archive_t arch_gsf_read(stream_t s){
	archive_t arch=(archive_t)malloc(sizeof(struct archive_s));
	if (arch==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	arch->ds=DScreate(s,SWAP_NETWORK);
	arch->count=-1;
	arch->procs.read=NULL;
	arch->procs.write=NULL;
	arch->procs.list=NULL;
	arch->procs.play=gsf_play;
	arch->procs.close=gsf_close;
	return arch;
}

archive_t arch_gsf_write(stream_t s){
	archive_t arch=(archive_t)malloc(sizeof(struct archive_s));
	if (arch==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	arch->ds=DScreate(s,SWAP_NETWORK);
	arch->count=0;
	arch->procs.read=NULL;
	arch->procs.write=gsf_write;
	arch->procs.list=NULL;
	arch->procs.play=NULL;
	arch->procs.close=gsf_close;
	DSwriteS(arch->ds,"GSF 0.1");
	return arch;
}

archive_t arch_gsf(stream_t s){
	archive_t arch=(archive_t)malloc(sizeof(struct archive_s));
	if (arch==NULL) {
		Fatal(0,error,"out of memory");
		return NULL;
	}
	arch->ds=DScreate(s,SWAP_NETWORK);
	if (DSreadable(arch->ds)) {
		arch->procs.play=gsf_play;
	} else {
		arch->procs.play=NULL;
	}
	if (DSwritable(arch->ds)){
		arch->procs.write=gsf_write;
		DSwriteS(arch->ds,"GSF 0.1");
	} else {
		arch->procs.write=NULL;
	}
	arch->count=0;
	arch->procs.read=NULL;
	arch->procs.list=NULL;
	arch->procs.close=gsf_close;
	return arch;
}



