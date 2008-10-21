#include "config.h"
#include <stdlib.h>
#include <string.h>

#include "unix.h"
#include "stream_object.h"
#include "runtime.h"

typedef int32_t diff_t ;

struct stream_s {
	struct stream_obj procs;
	stream_t s;
	diff_t prev_rd;
	diff_t prev_wr;
};


static size_t diff_read_max(stream_t stream,void*buf,size_t count){
	size_t res=stream_read_max(stream->s,buf,count);
	if (res%sizeof(diff_t)) {
		Fatal(1,error,"misaligned read");
	}
	int len=res/sizeof(diff_t);
	int swap_code=stream->s->procs.swap & SWAP_READ;
	int swap_clear=stream->procs.swap & SWAP_READ;
	diff_t prev=stream->prev_rd;
	for(int i=0;i<len;i++){
		if (swap_code) ((diff_t*)buf)[i]=bswap_32(((diff_t*)buf)[i]);
		((diff_t*)buf)[i]+=prev;
		prev=((diff_t*)buf)[i];
		if (swap_clear) ((diff_t*)buf)[i]=bswap_32(((diff_t*)buf)[i]);
	}
	stream->prev_rd=prev;
	return res;
}

static void diff_read(stream_t stream,void*buf,size_t count){
	size_t res=diff_read_max(stream,buf,count);
	if (res<count) Fatal(0,error,"short read");
}

static int diff_empty(stream_t stream){
	return stream_empty(stream->s);
}

static void diff_write(stream_t stream,void*buf,size_t count){
	if (count%sizeof(diff_t)) {
		Fatal(1,error,"misaligned write");
	}
	int len=count/sizeof(diff_t);
	int swap_code=stream->s->procs.swap & SWAP_WRITE;
	int swap_clear=stream->procs.swap & SWAP_WRITE;
	diff_t dbuf[len];
	diff_t prev=stream->prev_wr;
	for(int i=0;i<len;i++){
		diff_t current=((diff_t*)buf)[i];
		if(swap_clear) current=bswap_32(current);
		//Warning(info,"current is %d",current);
		dbuf[i]=current-prev;
		if(swap_code) dbuf[i]=bswap_32(dbuf[i]);
		prev=current;
	}
	stream->prev_wr=prev;
	stream_write(stream->s,dbuf,count);
}

static void diff_close(stream_t *stream){
	//Warning(info,"closing stream, clear swap %d code swap %d",DSgetSwap(*stream),DSgetSwap((*stream)->s));
	stream_close(&((*stream)->s));
	free(*stream);
	*stream=NULL;
}

static void diff_flush(stream_t stream){
	stream_flush(stream->s);
}

stream_t stream_diff32(stream_t s){
	stream_t ds=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(ds);
	ds->s=s;
	ds->prev_rd=0;
	ds->prev_wr=0;
	if (stream_readable(s)) {
		ds->procs.read_max=diff_read_max;
		ds->procs.read=diff_read;
		ds->procs.empty=diff_empty;
	}
	if (stream_writable(s)){
		ds->procs.write=diff_write;
		ds->procs.flush=diff_flush;
	}
	ds->procs.close=diff_close;
	ds->procs.swap=s->procs.swap;
	return ds;
}


