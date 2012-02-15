// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#ifndef STRUCTURED_IO
#define STRUCTURED_IO

/**
\file struct_io.h

\brief Support for writing/reading structures to (sets of) files.
*/

#include <hre-io/user.h>

/// Write one structure.
extern void DSwriteStruct(struct_stream_t stream,void *data);

/// Read one structure.
extern int DSreadStruct(struct_stream_t stream,void *data);

/// Close all streams.
extern void DSstructClose(struct_stream_t *stream);

/// Create a stream that can read vectors of U32's to numbered streams.
extern struct_stream_t arch_read_vec_U32(archive_t archive,char*fmt,int len);

/// Create a stream that can write vectors of U32's to numbered streams.
extern struct_stream_t arch_write_vec_U32(archive_t archive,char*fmt,int len);

/// Create a stream that can read vectors of U32's to named streams.
extern struct_stream_t arch_read_vec_U32_named(archive_t archive,char*fmt,int len,char **name);

/// Create a stream that can write vectors of U32's to named streams.
extern struct_stream_t arch_write_vec_U32_named(archive_t archive,char*fmt,int len,char **name);

/// Test if the stream is empty.
extern int DSstructEmpty(struct_stream_t stream);

#endif


