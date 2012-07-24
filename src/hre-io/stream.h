// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/** @file stream.h
 *  @brief abstraction for data streams
 */

#ifndef STREAM_H
#define STREAM_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <hre-io/user.h>

extern size_t stream_read_max(stream_t s,void* buf,size_t count);

extern void stream_read(stream_t s,void* buf,size_t count);
#define DSread stream_read

extern char* DSreadLN(stream_t ds);

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

/// Create output stream from a FILE.
extern stream_t stream_output(FILE*f);

/// Create a stream that reads from the named file.
extern stream_t file_input(char *name);

/// Create a stream that writes to the named file.
extern stream_t file_output(char*name);

extern stream_t stream_buffer(stream_t s,int size);

extern stream_t stream_write_mem(void*buf,size_t len,size_t *used);

extern stream_t stream_read_mem(void*buf,size_t len,size_t *used);

extern stream_t stream_gzip(stream_t compressed,int level,int bufsize);

extern stream_t stream_gunzip(stream_t expanded,int level,int bufsize);

extern stream_t stream_diff32(stream_t s);

extern stream_t stream_undiff32(stream_t s);

extern stream_t stream_rle32(stream_t s);

extern stream_t stream_unrle32(stream_t s);

/**
\brief Encode the stream with the given code.

That is, writing the stream involves encoding and reading involves decoding.
 */
extern stream_t stream_add_code(stream_t s,const char* code);


/**
\brief Decode the stream with the given code.

That is, writing the stream involves deconding and reading involves encoding.
 */
extern stream_t stream_add_decode(stream_t s,const char* code);

/**
\brief Create a stream that reads from the given file descriptor.
*/
extern stream_t fd_input(int fd);

/**
\brief Create a stream that write to the given file descriptor.
*/
extern stream_t fd_output(int fd);

/**
\brief Create a bidirectional stream around one file descriptor.
*/
extern stream_t fd_stream(int fd);

/**
\brief Create a bidirectional stream around two file descriptors.
*/
extern stream_t fd_stream_pair(int fd_in,int fd_out);

/** \defgroup data_io Data Input/Output */
/*@{*/

/// Write a signed 8 bit integer in network byte order.
extern void DSwriteS8(stream_t ds,int8_t i);
/// Write an unsigned 8 bit integer in network byte order.
extern void DSwriteU8(stream_t ds,uint8_t i);
/// Write a signed 16 bit integer in network byte order.
extern void DSwriteS16(stream_t ds,int16_t i);
/// Write an unsigned 16 bit integer in network byte order.
extern void DSwriteU16(stream_t ds,uint16_t i);
/// Write a signed 32 bit integer in network byte order.
extern void DSwriteS32(stream_t ds,int32_t i);
/// Write an unsigned 32 bit integer in network byte order.
extern void DSwriteU32(stream_t ds,uint32_t i);
/// Write a signed 64 bit integer in network byte order.
extern void DSwriteS64(stream_t ds,int64_t i);
/// Write an unsigned 64 bit integer in network byte order.
extern void DSwriteU64(stream_t ds,uint64_t i);
/// Write a 0-terminated string.
extern void DSwriteS(stream_t ds,char *s);
/// Write a 64 bit integer using variable length encoding.
extern void DSwriteVL(stream_t ds,uint64_t i);

/// Read a signed 8 bit integer in network byte order.
extern int8_t DSreadS8(stream_t ds);
/// Read an unsigned 8 bit integer in network byte order.
extern uint8_t DSreadU8(stream_t ds);
/// Read a signed 16 bit integer in network byte order.
extern int16_t DSreadS16(stream_t ds);
/// Read an unsigned 16 bit integer in network byte order.
extern uint16_t DSreadU16(stream_t ds);
/// Read a signed 32 bit integer in network byte order.
extern int32_t DSreadS32(stream_t ds);
/// Read an unsigned 32 bit integer in network byte order.
extern uint32_t DSreadU32(stream_t ds);
/// Read a signed 64 bit integer in network byte order.
extern int64_t DSreadS64(stream_t ds);
/// Read an unsigned 64 bit integer in network byte order.
extern uint64_t DSreadU64(stream_t ds);

/// Read a 0-terminated string into user provided memory.
extern void DSreadS(stream_t ds,char *s,int maxlen);
/// Read a 0-terminated string into allocated memory.
extern char* DSreadSA(stream_t ds);
/// Read a 64 bit integer in variable length encoding.
extern uint64_t DSreadVL(stream_t ds);

/*}@*/

#endif

