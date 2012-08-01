/**
\brief Concurrent Chunktables

calling threads shpould take care of the construction of cache and table_map
Usually, the cct_cont_t belongs in thread local storage and the map is global.
This avoids internal calls to pthread_getspecific and makes the 
cct interface more explicit.
*/

#ifndef CCT_H
#define CCT_H

#include <pthread.h>

#include <spec-greybox.h>
#include <tables.h>

#if SPEC_MT_SAFE == 1
#define MC_ALLOC_GLOBAL
#define MC_ALLOC_LOCAL
#define MC_MUTEX_SHARED_ATTR PTHREAD_PROCESS_PRIVATE
#else
#define MC_MUTEX_SHARED_ATTR PTHREAD_PROCESS_SHARED
#define MC_ALLOC_GLOBAL RT_ALLOC_GLOBAL
#define MC_ALLOC_LOCAL RT_ALLOC_LOCAL
#endif

/**
\typedef a thread-local container for the chunk table map
it maintains the current table index and a local cache

This struct is HRE-aware and allocates on the global HRE heap
*/
typedef struct cct_cont_s cct_cont_t;

/**
\typedef A map of concurrent chunk tables
*/
typedef struct cct_map_s cct_map_t;

extern cct_map_t       *cct_create_map();

extern cct_cont_t      *cct_create_cont(cct_map_t *tables);

extern value_table_t    cct_create_vt(cct_cont_t *map);

extern void            *cct_new_map(void* context);

extern void            *cct_map_get(void*ctx,int idx,int*len);

extern int              cct_map_put(void*ctx,void *chunk,int len);

extern int              cct_map_count(void* ctx);

#endif

