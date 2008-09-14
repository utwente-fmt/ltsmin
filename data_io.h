

/**
@file data_io.h
@brief Data input and output on top of streams.
*/

#ifndef DATA_IO_H
#define DATA_IO_H

#include "stream.h"
#include <unistd.h>
#include <stdint.h>
#include <string.h>

/// Handle to DataStream.
typedef struct DataStream *data_stream_t;

#define SWAP_READ 0x01
#define SWAP_WRITE 0x02

#define SWAP_NONE 0x00

#include <endian.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SWAP_NETWORK 0x03
#else
#define SWAP_NETWORK 0x00
#endif

/** @brief Create a data stream on top of a stream.
 */
extern data_stream_t DScreate(stream_t stream,int swap);
extern void DSsetSwap(data_stream_t ds,int swap);
extern int  DSgetSwap(data_stream_t ds);

/** @brief Get the underlying stream of a data stream.
 */
extern stream_t DSgetStream(data_stream_t ds);

/** @brief Destroy data stream.
 */
extern void DSdestroy(data_stream_t *ds);

/** @brief Close underlying stream and destroy data stream.
 */
extern void DSclose(data_stream_t *ds);


extern int DSwritable(data_stream_t ds);
extern void DSwrite(data_stream_t ds,void*buf,size_t count);
extern void DSflush(data_stream_t ds);


extern void DSwriteS8(data_stream_t ds,int8_t i);
extern void DSwriteU8(data_stream_t ds,uint8_t i);

extern void DSwriteS16(data_stream_t ds,int16_t i);
extern void DSwriteU16(data_stream_t ds,uint16_t i);

extern void DSwriteS32(data_stream_t ds,int32_t i);
extern void DSwriteU32(data_stream_t ds,uint32_t i);

extern void DSwriteS64(data_stream_t ds,int64_t i);
extern void DSwriteU64(data_stream_t ds,uint64_t i);

extern void DSwriteF(data_stream_t ds,float f);
extern void DSwriteD(data_stream_t ds,double d);

extern void DSwriteS(data_stream_t ds,char *s);
extern void DSwriteVL(data_stream_t ds,uint64_t i);


extern int DSreadable(data_stream_t ds);
extern void DSread(data_stream_t ds,void*buf,size_t count);
extern int DSempty(data_stream_t ds);


extern int8_t DSreadS8(data_stream_t ds);
extern uint8_t DSreadU8(data_stream_t ds);

extern int16_t DSreadS16(data_stream_t ds);
extern uint16_t DSreadU16(data_stream_t ds);

extern int32_t DSreadS32(data_stream_t ds);
extern uint32_t DSreadU32(data_stream_t ds);

extern int64_t DSreadS64(data_stream_t ds);
extern uint64_t DSreadU64(data_stream_t ds);

extern float DSreadF(data_stream_t ds);
extern double DSreadD(data_stream_t ds);

extern void DSreadS(data_stream_t ds,char *s,int maxlen);
extern char* DSreadSA(data_stream_t ds);
extern char* DSreadLN(data_stream_t ds);

extern uint64_t DSreadVL(data_stream_t ds);

#endif
