#ifndef BLOCK_ALLOC_H
#define BLOCK_ALLOC_H

#include <stdlib.h>

/**
\file balloc.h

\brief Memory allocater for large numbers of objects of the same size.

malloc/free needs to keep track of what is allocated. Thus allocating and freeing
small pieces of memory often using malloc/free is not a good idea. This library uses
malloc/free to get blocks of memory and will hand out fixed size pieces
when asked.
*/


/**
\brief Opaque type for an allocater.
*/
typedef struct block_allocator *allocater_t;

/**
\brief Create a block based allocater.

Create an allocator for elements of size element_size>=size_of(void*) that
   allocates blocks of size block_size >> element_size.
 */
extern allocater_t BAcreate(size_t element_size,size_t block_size);

/**
\brief Add another reference to an allocater.
*/
extern void BAaddref(allocater_t a);

/**
\brief Dereference an allocater.
 */
extern void BAderef(allocater_t a);

/**
\brief Allocate an element.

The element will be taken from the free list if possible and from
a newly allocated block if necessary.
 */
extern void* BAget(allocater_t a);

/**
\brief Free an element. */
extern void BAfree(allocater_t a,void* e);

#endif
