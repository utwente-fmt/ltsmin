// -*- tab-width:4 ; indent-tabs-mode:nil -*-

/**
 * A chunk table factory and a simple implementation for sequential use
 */


#ifndef CHUNK_TABLE_FACTORY_H
#define CHUNK_TABLE_FACTORY_H


#ifdef LTSMIN_CONFIG_INCLUDED
#include <util-lib/tables.h>
#else
#include <ltsmin/tables.h>
#endif


typedef struct table_factory_s *table_factory_t;


/**
\brief New value table functionality
*/
extern value_table_t TFnewTable (table_factory_t tf);
typedef value_table_t (*tf_new_map_t) (table_factory_t tf);
extern void TFnewTableSet (table_factory_t tf, tf_new_map_t method);

/**
\brief Factory creation.
*/
extern table_factory_t TFcreateBase (size_t user_size);

/**
\brief Factory function that creates chunk based value tables.
*/
extern value_table_t simple_chunk_table_create (void* context,char*type_name);

/**
\brief Factory function that creates chunk based value tables.
*/
extern table_factory_t simple_table_factory_create ();

/**
 * Simple iterator using the get method
 */
extern table_iterator_t simple_iterator_create (value_table_t vt);

#endif // CHUNK_TABLE_FACTORY_H
