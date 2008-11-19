#ifndef PACKET_STREAM_H
#define PACKET_STREAM_H

#include <unistd.h>

#include "config.h"
#include "stream.h"

/**
@file msg_packet.h
@brief Decode a stream of packets while it is being written.

Decoding a formatted data stream while reading is relatively easy:
one can just read whatever is needed. When the stream is written
rather than read is is more difficult, because one need to decide
how long to buffer before processing. The simplest case is to use
a data format where every record starts with its length. This is
what we call a packet stream. 
*/ 

/// Packet callback.
typedef void(*packet_cb)(void* context,uint16_t len,void* data);

/// Create a sequence of callbacks by writing a stream.
extern stream_t packet_stream(packet_cb cb,void*context);

/// Macro that fetches a 32 bit integer from an offset within an array.
#define PKT_U32(array,ofs) ((((uint32_t)array[ofs])<<24)|\
			    (((uint32_t)array[ofs+1])<<16)|\
			    (((uint32_t)array[ofs+2])<<8)|\
			     ((uint32_t)array[ofs+3]))


#endif
