// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
@file raf.h
@brief Abstraction layer for random access files.

The lowest level of IO for single file archives is the random acces file.

 */
#ifndef RAF_H
#define RAF_H

#include <unistd.h>
#include <hre-io/user.h>

/** @brief Read from a random acces file. */
extern void raf_read(raf_t raf,void*buf,size_t len,off_t offset);

/** @brief Write to a random access file. */
extern void raf_write(raf_t raf,void*buf,size_t len,off_t offset);

/** @brief Get the current size of a file. */
extern off_t raf_size(raf_t raf);

/** @brief Set the size of a file. */
extern void raf_resize(raf_t raf, off_t size);

/** @brief Close the random acces file and destroy the handle. */
/** In a distributed setting this is a collective operation. */
extern void raf_close(raf_t *raf);

/** @brief Open a random acces file using UNIX calls. */
extern raf_t raf_unistd(char *name);

#endif

