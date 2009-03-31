#ifndef STRUCTURED_IO
#define STRUCTURED_IO

/**
\file struct_io.h

Support for writing/reading structures to (sets of) files.
*/

#include "archive.h"

/// Opaque type struct stream.
typedef struct struct_stream_s *struct_stream_t;

/// Write one structure.
extern void DSwriteStruct(struct_stream_t stream,void *data);

/// Read one structure.
extern void DSreadStruct(struct_stream_t stream,void *data);

/// Close all streams.
extern void DSstructClose(struct_stream_t *stream);

/// Create a stream that can read vectors of U32's.
extern struct_stream_t arch_read_vec_U32(archive_t archive,char*fmt,int len,char*code);

/// Create a stream that can write vectors of U32's.
extern struct_stream_t arch_write_vec_U32(archive_t archive,char*fmt,int len,char*code,int hdr);

/// Create a stream that can read vectors of U32's.
extern struct_stream_t arch_read_vec_U32_named(archive_t archive,char*fmt,int len,char **name,char*code);

/// Create a stream that can write vectors of U32's.
extern struct_stream_t arch_write_vec_U32_named(archive_t archive,char*fmt,int len,char **name,char*code,int hdr);

#endif


