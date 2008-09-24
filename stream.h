/** @file stream.h
 *  @brief abstraction for data streams
 */

#ifndef STREAM_H
#define STREAM_H

#include "config.h"
#include <unistd.h>
#include <stdio.h>


typedef struct stream_s *stream_t;

extern size_t stream_read_max(stream_t s,void* buf,size_t count);

extern void stream_read(stream_t s,void* buf,size_t count);

extern int stream_empty(stream_t s);

extern void stream_write(stream_t s,void* buf,size_t count);

extern void stream_flush(stream_t stream);

extern void stream_close(stream_t *stream);

extern int stream_readable(stream_t stream);

extern int stream_writable(stream_t stream);

/// Create input stream from a FILE.
extern stream_t stream_input(FILE*f);

extern stream_t fs_read(char *name);

/// Create output stream from a FILE.
extern stream_t stream_output(FILE*f);

extern stream_t fs_write(char *name);

extern stream_t stream_buffer(stream_t s,int size);

extern stream_t stream_write_mem(void*buf,size_t len,size_t *used);

extern stream_t stream_read_mem(void*buf,size_t len,size_t *used);

#endif

