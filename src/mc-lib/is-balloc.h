#ifndef IS_BALLOC_H_
#define IS_BALLOC_H_

#include <unistd.h>

/**
 \file fi-buffer.h

 Provides a block allocator with stack regime for fixed-size int arrays.
 */

/**
 \brief Opaque type isb_allocator.
 */
typedef struct isb_allocator *isb_allocator_t;

/**
 \brief Create a block allocator
 */
extern isb_allocator_t isba_create(int element_size);

extern size_t isba_elt_size (const isb_allocator_t buf);

extern void isba_destroy(isb_allocator_t isb_alloc);

extern char *isba_to_string(isb_allocator_t isb_alloc);

extern int *isba_push_int(isb_allocator_t isb_alloc, const int *element);

extern int *isba_pop_int(isb_allocator_t isb_alloc);

extern void isba_discard_int(isb_allocator_t isb_alloc, size_t amount);

extern int *isba_top_int(isb_allocator_t isb_alloc);

extern int *isba_peek_int(isb_allocator_t isb_alloc, size_t offset_top);

extern int *isba_index(isb_allocator_t isb_alloc, size_t offset_top);

extern size_t isba_size_int(isb_allocator_t isb_alloc);

#endif

