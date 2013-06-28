// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef LTS_USER_H
#define LTS_USER_H

#include <hre-io/user.h>
#include <ltsmin-lib/lts-type.h>
#include <util-lib/tables.h>
#include <util-lib/string-map.h>

/**
\file lts-io/user.h
\brief User interface to the LTS-IO library.

This library provides streaming read and write functionality.
The native format of this library is the vector IO format.
Limited support for Aldebaran format (aut) and BCG format
is also available.

*/

/**
\brief Pre option parsing setup.
*/
extern void lts_lib_setup();

/**
\brief Opaque type lts file.
*/
typedef struct lts_file_s *lts_file_t;

/**
\brief Enumerated type for edge ownership.
*/
typedef enum {SourceOwned,DestOwned} edge_owner_t;

/**
\brief Enumerated type for state representation.
*/
typedef enum {Index,Vector,SegVector} state_format_t;

/**
\brief Get the state representation for initial states.
*/
state_format_t lts_file_init_mode(lts_file_t);

/**
\brief Set the state representation for initial states.
*/
void lts_file_set_init_mode(lts_file_t,state_format_t mode);

/**
\brief Get the source state representation.
*/
state_format_t lts_file_source_mode(lts_file_t);

/**
\brief Set the source state representation.
*/
void lts_file_set_source_mode(lts_file_t,state_format_t mode);

/**
\brief Get the destination state representation.
*/
state_format_t lts_file_dest_mode(lts_file_t);

/**
\brief Set the destination state representation.
*/
void lts_file_set_dest_mode(lts_file_t,state_format_t mode);

/**
\brief Create a new LTS file. (collective)

This function writes the LTS in the detected format.
*/
lts_file_t lts_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);

/**
\brief Create a new LTS file that hides state vectors on-the-fly. (collective)

This function adds a filter that hides the state vector.
*/
lts_file_t lts_file_create_nostate(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);

/**
\brief Create a new LTS file that writes only the filtered state and edge labels. (collective)

*/
lts_file_t lts_file_create_filter(const char* name,lts_type_t ltstype,string_set_t filter,int segments,lts_file_t settings);

/**
\brief Open an existing LTS file. (collective)
*/
lts_file_t lts_file_open(const char* name);

/**
\brief Open a stream that is attached to the LTS file.
*/
stream_t lts_file_attach(lts_file_t lts,char *name);

/**
\brief Get the name of the file.
*/
const char* lts_file_get_name(lts_file_t lts);

/**
\brief Get the current edge ownership of the file.
*/
edge_owner_t lts_file_get_edge_owner(lts_file_t file);

/**
\brief Set a new edge ownership.
*/
void lts_file_set_edge_owner(lts_file_t file,edge_owner_t owner);

/**
\brief Get the type/signature of the LTS. (asynchronous)
 */
lts_type_t lts_file_get_type(lts_file_t lts);

/**
\brief Get the number of segments.
*/
int lts_file_get_segments(lts_file_t lts);

/**
\brief Get the number of owned segments.
*/
int lts_file_owned_count(lts_file_t lts);

/**
\brief Get the n-th owned segment
*/
int lts_file_owned(lts_file_t lts,int nth);

/**
\brief Set the HRE context of the file.
*/
void lts_file_set_context(lts_file_t lts, hre_context_t ctx);

/**
\brief Get the HRE context of the file.
*/
hre_context_t lts_file_context(lts_file_t lts);

/**
\brief Close the  LTS file. (collective)
*/
void lts_file_close(lts_file_t lts);

/**
\brief Set the value table for the given type.
*/
void lts_file_set_table(lts_file_t lts,int type_no,value_table_t table);

/**
\brief Get the value table for the given type.
*/
value_table_t lts_file_get_table(lts_file_t lts,int type_no);

/**
\brief Check if random interleaved writing is supported.
*/
int lts_write_supported(lts_file_t lts);

/**
\brief Check if an LTS supports reading by pushing.
*/
int lts_push_supported(lts_file_t lts);

/**
\brief Check if random interleaved reading is supported.
*/
int lts_read_supported(lts_file_t lts);

/**
\brief Check if an LTS supports writing by pulling.
*/
int lts_pull_supported(lts_file_t lts);

/**
\brief Copy one LTS file to another.

One of the file must support random interleaving.
*/
void lts_file_copy(lts_file_t src,lts_file_t dst);

/**
\brief Let the source push the date to the destination.

The destination must support random interleaving.*/
void lts_file_push(lts_file_t src,lts_file_t dst);

/**
\brief Let the destination pull the date from the source.

The source must support random interleaving.
*/
void lts_file_pull(lts_file_t dst,lts_file_t src);

/**
\brief Write an initial state.
*/
void lts_write_init(lts_file_t lts,int seg,void* state);

/**
\brief Write a state to the file.
*/
void lts_write_state(lts_file_t lts,int seg,void* state,void* label);

/**
\brief Write an edge to the file.
*/
void lts_write_edge(lts_file_t lts,int src_seg,void* src_state,int dst_seg,void*dst_state,void* label);

/**
\brief Read the next initial state.
*/
int lts_read_init(lts_file_t lts,int*seg,void* state);

/**
\brief Read a state from any segment.

\return 1 on succes and 0 on end of file.
*/
int lts_read_state(lts_file_t lts,int *seg,void* state,void* label);

/**
\brief Read an edge between arbitrary segments.

\return 1 on succes and 0 on end of file.
*/
int lts_read_edge(lts_file_t lts,int *src_seg,void* src_state,int *dst_seg,void*dst_state,void* label);

/**
\brief Get a default indexed template.

This writes destination owned indexed LTSs.
*/
lts_file_t lts_index_template();

/**
\brief Get a default template for vector writing.

The sources are written indexed.
Initial states and destinations are written in segvector mode.
*/
lts_file_t lts_vset_template();


/**
\brief Get a template from an existing file.
*/
lts_file_t lts_get_template(lts_file_t lts);


/**
\brief Get the number of initial states
*/
uint32_t lts_get_init_count(lts_file_t lts);

/**
\brief Get the number of states in a segment.
*/
uint32_t lts_get_state_count(lts_file_t lts,int segment);

/**
\brief Get the number of transitions in a segment.
*/
uint64_t lts_get_edge_count(lts_file_t lts,int segment);

/**
\brief Synchornize the counts across workers.
*/
void lts_file_sync(lts_file_t lts);

/**
\brief Get the expected number of values in a type.
*/
uint32_t lts_get_get_expected_value_count(lts_file_t lts,int type_no);

/**
\brief Write the serialization of an lts type to a stream.
*/
extern void lts_type_serialize(lts_type_t t,stream_t s);

/**
\brief Create an lts type by reading it's serialization from a stream.
*/  
extern lts_type_t lts_type_deserialize(stream_t s);

/// Print the lts type to the log stream;
extern void lts_type_print(log_t log, lts_type_t t);

#endif

