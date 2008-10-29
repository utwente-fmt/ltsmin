#ifndef CHUNK_TABLE_H
#define CHUNK_TABLE_H

#include "config.h"
#include <stdint.h>

/**
 @file chunk-table.h
 @brief Chunk management interface between language module and state space explorers.

Language modules often have an internal data representation. This internal representation
cannot be used for either writing to files or for communicating with other workers in a
distributed setting. The simplest solution is to communicate with serialized forms. However,
this is inefficient both in space and time so we use table compression which represent
every serialized form, or chunk, as a single integer. This interface is the API
representation of a protocol for building compression tables.

A language module that uses compression tables (e.g. one for actions labels and
one for data elements) can create a table by providing the names (e.g. "action" and "data").
When the language module needs to resolve a newly discovered chunk or a newly encountered chunk number,
it will call CTsubmitChunk or CTupdateTable. The numbering of chunks in a table is determined by the order
in which they are returned to the language module by calling the provided callback.
When there are multiple instances of a language module it is therefore the responsibility of the system
to give these to each instance in the same order.
*/

typedef void* chunk_table_t;
/**< @brief Abstract type chunk table.
 */

extern chunk_table_t CTcreate(char *name);
/**< @brief Create a table.
 */

typedef void(*chunk_add_t)(void *context,size_t len,void* chunk);
/**< @brief Type of the callback function used to acknowledge new chunks. */

extern void CTsubmitChunk(chunk_table_t table,size_t len,void* chunk,chunk_add_t cb,void* context);
/**< @brief Insert a chunk into a table.

The purpose of this call is to find the number of the given chunk. To do this
this function will call the callback function cb until the requested chunk has been returned.
If the requested chunk has already been returned then there may be 0 call backs.
Moreover if cb is called with the requested chunk then there may be more calls to cb
before thsi functions returns.
 */

extern void CTupdateTable(chunk_table_t table,uint32_t wanted,chunk_add_t cb,void* context);
/**< @brief Ask for an update to the table.

The purpose of this call is to find chunk correspinding to wanted.
Thus this function will call the callback until the wanted chunk has been passed.
It is not an error is the requested chunk has already been passed. Nor is it an error
if the callback continue after providing the requested chunk.
 */

#endif

