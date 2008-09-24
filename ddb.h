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

typedef enum {DDB_U32, DDB_IDX, DDB_OFS} DDB_TYPE;
/**< The possible data types of arrays

*/

extern ddb_t DDBcreate(MPI_Comm comm);

extern void DDBload(ddb_t *ddb,archive_t arch);

extern void DDBclose(ddb_t * ddb);

extern void DDBtableCreate(ddb_t ddb, char*name);

extern void DDBcolCreate(ddb_t ddb,char* table,char*name,DDB_TYPE t,char*arg);
/**< @brief Create an array.
@param table The name of the table to which the column should be added.
*/

typedef struct chunk_index * chunk_idx_t;

extern void DDBchunkCreate(ddb_t ddb,char *name);
extern chunk_idx_t DDBchunkIndex(ddt_t ddb,char *name);
extern uint32_t DDBchunkPut(chunk_idx_t idx,void *data,uint16_t len);
extern uint16_t DDBchunkLen(chunk_idx_t idx,uint32_t ofs);
extern void* DDBchunkData(chunk_idx_t idx,uint32_t ofs);



#endif

