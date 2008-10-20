#ifndef AIO_KERNEL_H
#define AIO_KERNEL_H

#include "config.h"
#include <pthread.h>
#include <aio.h>

typedef struct aiocb *aiocb_t;
typedef struct aio_kern_s *aio_kernel_t;

/** Submit wait request to kernel */
extern void aio_wait(aio_kernel_t k,aiocb_t rq,void(*resume)(aio_kernel_t k,void*arg),void*arg);

/** create a new thread and run an io_kernel in it */
extern aio_kernel_t aio_kernel();

#endif

