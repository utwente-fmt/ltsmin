/**
\brief Concurrent Chunktables

calling threads should take care of the construction of cache and table_map
Usually, the cct_cont_t belongs in thread local storage and the map is global.
This avoids internal calls to pthread_getspecific and makes the 
cct interface more explicit.

This associated chunk table factory is HRE-aware and allocates on the
global HRE heap (w shared = true)
*/

#ifndef CCT_H
#define CCT_H

#include <stdbool.h>

#include <hre/feedback.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/chunk_table_factory.h>

/**
\typedef a thread-local container for the chunk table map
it maintains the current table index and a local cache
*/

/**
\typedef A map of concurrent chunk tables
*/
typedef struct cct_map_s cct_map_t;

extern cct_map_t       *cct_create_map (bool shared);

extern size_t           cct_print_stats (log_t log, log_t details,
                                         lts_type_t type, cct_map_t *);

extern table_factory_t  cct_create_table_factory (cct_map_t *tables);

#endif

