#ifndef LTS_PROVIDER_H
#define LTS_PROVIDER_H

#include <unistd.h>

#include <lts-io/user.h>

/**
\brief Create a bare lts file object.

The name, mode and index_length are kept in the system part.
The size of the user part of the object is passed in user_size.
The pointer returned is a pointer to the user part.
*/
extern lts_file_t lts_file_bare(const char* name,lts_type_t ltstype,int segments,lts_file_t settings,size_t user_size);

/**
\brief copy the settings from an lts file or template.
*/
extern void lts_copy_settings(lts_file_t lts, lts_file_t settings);


/**
\brief Automatically fill in methods that are needed, but not present yet.
*/
extern void lts_file_complete(lts_file_t lts);


/**
\brief The type of an lts with the label action on the edges.

This is the native type of AUT, BCG and DIR files.
*/
lts_type_t single_action_type();

typedef void(*lts_push_m)(lts_file_t me,lts_file_t dest);
void lts_file_set_push(lts_file_t file,lts_push_m method);

typedef void(*lts_pull_m)(lts_file_t me,lts_file_t src);
void lts_file_set_pull(lts_file_t file,lts_pull_m method);

typedef int(*lts_read_init_m)(lts_file_t file,int*seg,void *state);
void lts_file_set_read_init(lts_file_t file,lts_read_init_m method);

typedef void(*lts_write_init_m)(lts_file_t file,int seg,void *state);
void lts_file_set_write_init(lts_file_t file,lts_write_init_m method);

typedef int(*lts_read_state_m)(lts_file_t file,int*seg,void *state,void*labels);
void lts_file_set_read_state(lts_file_t file,lts_read_state_m method);

typedef void(*lts_write_state_m)(lts_file_t file,int seg,void *state,void*labels);
void lts_file_set_write_state(lts_file_t file,lts_write_state_m method);

typedef int(*lts_read_edge_m)(lts_file_t file,int*src_seg,void *src_state,
                              int *dst_seg,void*dst_state,void*labels);
void lts_file_set_read_edge(lts_file_t file,lts_read_edge_m method);

typedef void(*lts_write_edge_m)(lts_file_t file,int src_seg,void *src_state,
                                int dst_seg,void*dst_state,void*labels);
void lts_file_set_write_edge(lts_file_t file,lts_write_edge_m method);

typedef void(*lts_close_m)(lts_file_t file);
void lts_file_set_close(lts_file_t file,lts_close_m method);

typedef value_table_t(*lts_set_table_m)(lts_file_t lts,int type_no,value_table_t table);
void lts_file_set_table_callback(lts_file_t file,lts_set_table_m method);

typedef stream_t(*lts_attach_m)(lts_file_t lts,char *name);
void lts_file_set_attach(lts_file_t file,lts_attach_m method);

/**
\brief Get the maximum source plus 1 for a segment.
*/
uint32_t lts_get_max_src_p1(lts_file_t lts,int segment);

/**
\brief Get the maximum dest plus 1 for a segment.
*/
uint32_t lts_get_max_dst_p1(lts_file_t lts,int segment);

/**
\brief adjust initial state count.
*/
void lts_set_init_count(lts_file_t lts,uint32_t count);

/**
\brief adjust state count.
*/
void lts_set_state_count(lts_file_t lts,int segment,uint32_t count);

/**
\brief adjust state count.
*/
void lts_set_edge_count(lts_file_t lts,int segment,uint64_t count);

/**
\brief Set the expected number of values in a type.
*/
void lts_set_expected_value_count(lts_file_t lts,int type_no,uint32_t count);

#endif
