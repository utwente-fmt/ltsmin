// -*- tab-width:4 ; indent-tabs-mode:nil -*-
/**
\file tables.h

\brief Various types of tables and operations on them.
*/

#ifndef LTSMIN_TABLES_H
#define LTSMIN_TABLES_H

#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>

#ifdef LTSMIN_CONFIG_INCLUDED
#include <util-lib/chunk_support.h>
#else
#include <ltsmin/chunk_support.h>
#endif


/**
\brief type of a value index
*/
typedef uint32_t value_index_t;

/**
\brief Abstract type for tables that map values to numbers and back.

A value table is a pointer to the user area of the object.
Note that this area is preceeded by a system area, so do not
use free and realloc directly!
*/
typedef struct value_table_s *value_table_t;

/**
\brief Type of a value table factory function.
*/
typedef value_table_t(*value_table_create_t)(void* context,char*type_name);

/**
\brief Create an abstract base for a value table.
*/
extern value_table_t VTcreateBase(char*type_name,size_t user_size);

/**
\brief Change the size of the user portion of the value table object.
*/
extern void VTrealloc(value_table_t vt,size_t user_size);

/**
\brief Destroy a value table.
*/
extern void VTdestroy(value_table_t vt);

/**
\brief Destroy a value table and zero the pointer.
*/
extern void VTdestroyZ(value_table_t *vt_ptr);

typedef void(*user_destroy_t)(value_table_t vt);

/**
\brief Set the method that will be used to destroy the user part of the object.
*/
extern void VTdestroySet(value_table_t vt,user_destroy_t method);

/**
\brief return the type of the elements in the value table
*/
extern char* VTgetType(value_table_t vt);

extern value_index_t VTputNative(value_table_t vt,...);
typedef value_index_t(*put_native_t)(value_table_t vt,va_list args);
extern void VTputNativeSet(value_table_t vt,put_native_t method);

extern void VTgetNative(value_table_t vt,value_index_t idx,...);
typedef void(*get_native_t)(value_table_t vt,value_index_t idx,va_list args);
extern void VTgetNativeSet(value_table_t vt,get_native_t method);

/**
\brief Insert or retrieve a chunk.
*/
extern value_index_t VTputChunk(value_table_t vt,chunk item);
typedef value_index_t(*put_chunk_t)(value_table_t vt,chunk item);
extern void VTputChunkSet(value_table_t vt,put_chunk_t method);

/**
\brief Insert or retrieve a chunk at a particular index.
*/
extern void VTputAtChunk(value_table_t vt,chunk item,value_index_t pos);
typedef void(*put_at_chunk_t)(value_table_t vt,chunk item,value_index_t pos);
extern void VTputAtChunkSet(value_table_t vt,put_at_chunk_t method);

/**
\brief Retrive the chunk associated with an index
*/
extern chunk VTgetChunk(value_table_t vt,value_index_t idx);
typedef chunk(*get_chunk_t)(value_table_t vt,value_index_t idx);
extern void VTgetChunkSet(value_table_t vt,get_chunk_t method);

/**
\brief Number of chunks.
*/
extern int VTgetCount(value_table_t vt);
typedef int(*vt_get_count_t)(value_table_t vt);
extern void VTgetCountSet(value_table_t vt,vt_get_count_t method);


typedef struct table_iterator_s *table_iterator_t;

/**
\brief Retrieve chunk table iterator.
Iterator implementations guarantee that that chunks are iterated in order
(according to indexing).
WARNING: this order might not be dense!
*/
extern table_iterator_t VTiterator(value_table_t vt);
typedef table_iterator_t(*vt_iterator_t)(value_table_t vt);
extern void VTiteratorSet(value_table_t vt,vt_iterator_t method);


table_iterator_t ITcreateBase (size_t user_size);

extern chunk ITnext (table_iterator_t vt);
typedef chunk (*it_next_t)(table_iterator_t vt);
extern void ITnextSet (table_iterator_t vt, it_next_t method);

extern int IThasNext (table_iterator_t vt);
typedef int (*it_has_next_t)(table_iterator_t vt);
extern void IThasNextSet (table_iterator_t vt, it_has_next_t method);


/**
\brief Abstract type for multi column tables.
*/
typedef struct matrix_table_struct *matrix_table_t;

/**
\brief Create a new matrix table.
*/
extern matrix_table_t  MTcreate(int width);

/**
\brief Destroy a matrix table.
*/
extern void MTdestroy(matrix_table_t mt);

/**
\brief Destroy a matrix table and zero the pointer.
*/
extern void MTdestroyZ(matrix_table_t* mt_ptr);

/**
\brief Get the width of a matrix table
*/
extern int MTgetWidth(matrix_table_t mt);

/**
\brief Get the number of rows of a matrix table
*/
extern int MTgetCount(matrix_table_t mt);

/**
\brief Add a row to the matrix.
*/
extern void MTaddRow(matrix_table_t mt,uint32_t *row);

/**
\brief Get a row from the matrix.
*/
extern void MTgetRow(matrix_table_t mt,int row_no,uint32_t *row);

/**
\brief Change one entry in a matrix.
*/
extern void MTupdate(matrix_table_t mt,int row,int col,uint32_t val);

/**
\brief Cluster the rows of a matrix according to one column.
*/
extern void MTclusterBuild(matrix_table_t mt,int col,uint32_t cluster_count);

/**
\brief Sort the rows of a clustered table on another column.
*/
extern void MTclusterSort(matrix_table_t mt,int col);

/**
\brief Get the number of entries in a cluster.
*/
extern int MTclusterSize(matrix_table_t mt,uint32_t cluster);

/**
\brief Map the begin array.
*/
extern uint32_t* MTclusterMapBegin(matrix_table_t mt);

/**
\brief Map one of the columns.
*/
extern uint32_t* MTclusterMapColumn(matrix_table_t mt,int col);

/**
\brief Get one of the rows in a cluster.
*/
extern void MTclusterGetRow(matrix_table_t mt,uint32_t cluster,int row_no,uint32_t *row);

/**
\brief Get one element of a row in a clustered matrix.
*/
extern uint32_t MTclusterGetElem(matrix_table_t mt,uint32_t cluster,int row_no,int col);

/**
\brief Get the number of clusters.
*/
extern uint32_t MTclusterCount(matrix_table_t mt);

/**
\brief Change one entry in a matrix.
*/
extern void MTclusterUpdate(matrix_table_t mt,uint32_t cluster,int row,int col,uint32_t val);

/**
\brief get the maximum value of a column
*/
extern uint32_t MTgetMax(matrix_table_t mt,int col);

/**
\brief Copy, while sorting and removing doubles.
*/
extern void MTsimplify(matrix_table_t dst, matrix_table_t src);


#endif


