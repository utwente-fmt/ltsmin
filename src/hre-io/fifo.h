// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef FIFO_H
#define FIFO_H

/**
\file fifo.h

Variable sized FIFO buffering for streams.
*/

#include <stdlib.h>

#include <hre-io/user.h>

/// Create FIFO buffer.
extern stream_t FIFOcreate(size_t blocksize);

/**
\brief Return the number of bytes in the buffer.

The result of calling this function on a stream, which is not a fifo
is undefined.
*/
extern size_t FIFOsize(stream_t fifo);

#endif

