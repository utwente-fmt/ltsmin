/**
\brief Concurrent Chunktables

calling threads shpould take care of the construction of cache and table_map
Usually, the cct_cont_t belongs in thread local storage and the map is global.
This avoids internal calls to pthread_getspecific and makes the 
cct interface more explicit.
*/

#ifndef CCT_H
#define CCT_H

/**
\typedef a chunk table 
use malloc
*/
typedef struct table_s table_t;

/**
\typedef a map of concurrent chunctables
*/
typedef struct cct_map_s cct_map_t;

/**
\typedef a thread-local container for the chunktable map
it maintains the current table index and a local cache
*/
typedef struct cct_cont_s cct_cont_t;

extern cct_map_t *cct_create_map();

extern cct_cont_t *cct_create_cont(cct_map_t *tables, size_t start_index);

extern void *cct_new_map(void* context);

extern void *cct_map_get(void*ctx,int idx,int*len);

extern int cct_map_put(void*ctx,void *chunk,int len); 

extern int cct_map_count(void* ctx);
 
/**
 * End concurrent chunktables
 */

#endif

