/**
@file raf.h
@brief Abstraction layer for random access files.

The lowest level of IO for single file archives is the random acces file.

 */
#ifndef RAF_H
#define RAF_H

#include "config.h"
#include "runtime.h"
#include <unistd.h>

/** @brief Handle to a random acces file. */
typedef struct raf_struct_s *raf_t;

/** @brief Read from a random acces file. */
extern void raf_read(raf_t raf,void*buf,size_t len,off_t offset);

/** @brief Write to a random access file. */
extern void raf_write(raf_t raf,void*buf,size_t len,off_t offset);

/** @brief Asynchronous write to a raf. */
extern void raf_async_write(raf_t raf,void*buf,size_t len,off_t offset);

/** @brief Wait for completion of asynchronous write */
extern void raf_wait(raf_t raf);

/** @brief Get the current size of a file. */
extern off_t raf_size(raf_t raf);

/** @brief Set the size of a file. */
extern void raf_resize(raf_t raf, off_t size);

/** @brief Close the random acces file and destroy the handle. */
/** In a distributed setting this is a collective operation. */
extern void raf_close(raf_t *raf);

/** @brief Open a random acces file using UNIX calls. */
extern raf_t raf_unistd(char *name);

/** @brief Open a random acces file using UNIX/AIO calls. */
extern raf_t raf_aio(char *name);


#endif
