#ifndef FIFO_H
#define FIFO_H

#include <stdlib.h>
#include <stream.h>

typedef struct fifo_s *fifo_t;

extern fifo_t FIFOcreate(size_t blocksize);

extern void FIFOdestroy(fifo_t *fifo_p);

extern stream_t FIFOstream(fifo_t fifo);

extern size_t FIFOsize(fifo_t fifo);

#endif

