/** @file stream.h
 *  @brief abstraction for data streams
 */

#ifndef STREAM_H
#define STREAM_H

#include "config.h"
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>

typedef struct stream_s *stream_t;

extern size_t stream_read_max(stream_t s,void* buf,size_t count);

extern void stream_read(stream_t s,void* buf,size_t count);
#define DSread stream_read

extern int stream_empty(stream_t s);
#define DSempty stream_empty

extern void stream_write(stream_t s,void* buf,size_t count);
#define DSwrite stream_write

extern void stream_flush(stream_t stream);
#define DSflush stream_flush

extern void stream_close(stream_t *stream);
#define DSclose stream_close

extern int stream_readable(stream_t stream);

extern int stream_writable(stream_t stream);

/// Create input stream from a FILE.
extern stream_t stream_input(FILE*f);

extern stream_t fs_read(char *name);

/// Create output stream from a FILE.
extern stream_t stream_output(FILE*f);

extern stream_t file_input(char *name);

extern stream_t file_output(char*name);

extern stream_t fs_write(char *name);

extern stream_t stream_buffer(stream_t s,int size);

extern stream_t stream_write_mem(void*buf,size_t len,size_t *used);

extern stream_t stream_read_mem(void*buf,size_t len,size_t *used);

extern stream_t stream_gzip(stream_t compressed,int level,int bufsize);

extern stream_t stream_gunzip(stream_t expanded,int level,int bufsize);

/* future work:
extern stream_t stream_gunzip(stream_t expanded,int level,int bufsize);
*/
extern stream_t stream_diff32(stream_t s);

extern stream_t stream_add_code(stream_t s,char* code);

extern stream_t stream_setup(stream_t s,char* code);

/** \defgroup data_io Data Input/Output */
/*@{*/

#define SWAP_READ 0x01
#define SWAP_WRITE 0x02

#define SWAP_NONE 0x00

#if defined(__linux__)
#include <endian.h>
#elif defined(__FreeBSD__) || defined(__APPLE__)
#include <machine/endian.h>
#endif

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SWAP_NETWORK 0x03
#else
#define SWAP_NETWORK 0x00
#endif

extern void DSsetSwap(stream_t ds,int swap);
extern int  DSgetSwap(stream_t ds);
extern void DSautoSwap(stream_t ds);

extern void DSwriteS8(stream_t ds,int8_t i);
extern void DSwriteU8(stream_t ds,uint8_t i);

extern void DSwriteS16(stream_t ds,int16_t i);
extern void DSwriteU16(stream_t ds,uint16_t i);

extern void DSwriteS32(stream_t ds,int32_t i);
extern void DSwriteU32(stream_t ds,uint32_t i);

extern void DSwriteS64(stream_t ds,int64_t i);
extern void DSwriteU64(stream_t ds,uint64_t i);

extern void DSwriteF(stream_t ds,float f);
extern void DSwriteD(stream_t ds,double d);

extern void DSwriteS(stream_t ds,char *s);
extern void DSwriteC(stream_t ds,uint16_t len,char *c);
extern void DSwriteVL(stream_t ds,uint64_t i);


extern int8_t DSreadS8(stream_t ds);
extern uint8_t DSreadU8(stream_t ds);

extern int16_t DSreadS16(stream_t ds);
extern uint16_t DSreadU16(stream_t ds);

extern int32_t DSreadS32(stream_t ds);
extern uint32_t DSreadU32(stream_t ds);

extern int64_t DSreadS64(stream_t ds);
extern uint64_t DSreadU64(stream_t ds);

extern float DSreadF(stream_t ds);
extern double DSreadD(stream_t ds);

extern void DSreadS(stream_t ds,char *s,int maxlen);
extern char* DSreadSA(stream_t ds);
extern char* DSreadLN(stream_t ds);

extern uint64_t DSreadVL(stream_t ds);

/*}@*/

#endif

