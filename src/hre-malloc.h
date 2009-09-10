#ifndef HRE_MALLOC_H
#define HRE_MALLOC_H

#include <unistd.h>

/**
\file hre-malloc.h

Process architecture aware memory allocation.
*/


/**
Opaque type memory region.
*/
typedef struct hre_region_s *hre_region_t;

/**
Allocater for the process heap.
*/
extern hre_region_t hre_heap;

/**
Allocate memory in a region.
*/
extern void* HREmalloc(hre_region_t region,size_t size);

/**
Allocate and fill with zeros.
*/
extern void* HREmallocZero(hre_region_t region,size_t size);

/**
Free memory of known region and size.

Knowing the region and size helps, because it simplifies the administration
that the allocater has to maintain. Thus, it is easier to develop a new
allocater and it can speed up memory management when there any many
short-lived small objects.
*/
extern void HREfree(hre_region_t region,void* mem,size_t size);

/**
Free memory of unknown origins.

If the region is not known pass NULL as the region.
*/
extern void HREfreeGuess(hre_region_t region,void* mem);

/**
Macro that allocates room for a new object given the type.
*/
#define HRE_NEW(region,sort) ((sort*)HREmallocZero(region,sizeof(sort)))

/**
Macro that can free the memory pointer to by a variable.
*/
#define HRE_FREE(region,var) HREfree(region,var,sizeof(*var))

#endif
