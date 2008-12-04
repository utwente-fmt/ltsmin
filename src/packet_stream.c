
#include "packet_stream.h"
#include "stream_object.h"
#include "runtime.h"

struct stream_s {
	struct stream_obj procs;
	packet_cb cb;
	void*context;
	uint32_t state;
	uint16_t len;
	char buffer[65536];
};

static void pkt_write(stream_t stream,void*buf,size_t count){
	while(count){
		switch(stream->state){
		case 0:
			stream->len=0;
			stream->len=((uint8_t*)buf)[0];
			stream->state++;
			if (count==1) return;
			count--;
			buf++;
		case 1:
			stream->len=stream->len<<8;
			stream->len+=((uint8_t*)buf)[0];
			stream->state++;
			count--;
			buf++;
		default:
			for(;;){
				if(stream->state==((uint32_t)stream->len)+2){
					stream->cb(stream->context,stream->len,stream->buffer);
					stream->state=0;
					break;
				}
				if(count==0) return;
				stream->buffer[stream->state-2]=((uint8_t*)buf)[0];
				stream->state++;
				count--;
				buf++;		
			}
		}
	}
}

static void pkt_close(stream_t *stream){
	free(*stream);
	*stream=NULL;
}

static void pkt_flush(stream_t stream){
	(void)stream;
	Warning(info,"Flushing a packet stream is not necessary");
}

stream_t packet_stream(packet_cb cb,void*context){
	stream_t s=(stream_t)RTmalloc(sizeof(struct stream_s));
	stream_init(s);
	s->state=0;
	s->cb=cb;
	s->context=context;
	s->procs.write=pkt_write;
	s->procs.flush=pkt_flush;
	s->procs.close=pkt_close;
	return s;
}

