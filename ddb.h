/**
@file ddb.h
@brief A Distributed DataBase library.

*/

#ifndef DDB_H
#define DDB_H

#include "config.h"
#include "archive.h"
#include <mpi.h>

typedef struct ddb_s *ddb_t;
/**< Handle to a distributed database.
*/


extern ddb_t DDBcreate(MPI_Comm comm);

extern void DDBdestroy(ddb_t *ddb);


extern int DDBattach(ddb_t *ddb,archive_t arch);
/**< @brief Attach an archive as a pseudo worker to allow transfering data.
 */
extern archive_t DDBdetach(ddb_t ddb,int worker);
/**< @brief Detach a pseudo worker
 @returns The detached archive, which can then be closed or reused.
 */


extern void DDBxferSetup(ddb_t ddb);
/**< @brief Start setting up an exchange of data. */
extern void DDBmoveIn(ddb_t,char*table,char*col,int worker);
extern void DDBmoveOut(ddb_t,char*table,char*col,int worker);
extern void DDBcopyIn(ddb_t,char*table,char*col,int worker);
extern void DDBcopyOut(ddb_t,char*table,char*col,int worker);
extern void DDBerase(ddb_t,char*table,char*col);
extern void DDBxferWait(ddb_t ddb);
/**< @brief Wait for completion of the requested transfers. */


extern stream_t DDBreadCol(ddb_t ddb,char* table,char*name,int worker);
/**< @brief read the column data at worker as a stream */
extern stream_t DDBappendCol(ddb_t ddb,char* table,char*name,int worker);
/**< @brief extend column at worker by writing to a stream. */
extern void DDBmapCol(ddb_t ddb,char* table,char*name,void**addr);
/**< @brief let the library maintain a point to a columns data in addr. */
extern void DDBunmapCol(ddb_t ddb,char* table,char*name,void**addr);
/**< @brief stop maintaining the location data. */


extern void DDBtableCreate(ddb_t ddb, char*name);

typedef enum {DDB_FIXED, DDB_IDX, DDB_CHUNK} DDB_TYPE;
/**< @brief The possible data types of arrays.

Some of these types need arguments.
The type DDB_FIXED needs a single integer argument which is the size of a single item.
The type DDB_CHUNK also need an integer argument: the width of an item in the sequence of items.
(Typically this width would be 1.)
The item width of the type DDB_IDX is fixed at 4. The integer argument denotes the number
of different columns refered to. If this number is 1 then two string arguments are needed:
that table and column to which the index refers. If the argument is N > 1 then
we need N+2 arguments: the N different tables, the column name, the column of the discriminator.
If the column of the discriminator is NULL then we use (I * N + T) to encode item I in table T.

*/
extern void DDBcolCreate(ddb_t ddb,char* table,char*name,DDB_TYPE t,int arg1,void*arg2);
/**< @brief Create an array.
@param table The name of the table to which the column should be added.
*/

/*
define IDX_LOCAL 0
define IDX_REMOTE(worker) (((worker)*2)+1)
define IDX_REPLICATED(worker) (((worker)*2)+2)
IDX_ASYNC
*/

typedef struct chunk_index *chunk_idx_t;
extern chunk_idx_t DDBchunkIndex(ddt_t ddb,char*table,char *name/*,int idx_type*/);
extern int DDBchunkPut(chunk_idx_t idx,void *data,uint16_t len,uint32_t *pos);
extern int DDBchunkFind(chunk_idx_t idx,void *data,uint16_t len,uint32_t *pos);
extern uint16_t DDBchunkLen(chunk_idx_t idx,uint32_t pos);
extern void* DDBchunkData(chunk_idx_t idx,uint32_t pos);

typedef struct ddb_index ddb_idx_t;
extern ddb_idx_t DDBindexCreate(ddt_t ddb,char*table,char *name/*,int idx_type*/);
extern int DDBindexPut(ddb_idx_t idx,void *item,uint32_t *pos);
extern int DDBindexFind(ddb_idx_t idx,void *item,uint32_t *pos);
extern void* DDBindexGet(ddb_idx_t idx,uint32_t pos);

#endif

