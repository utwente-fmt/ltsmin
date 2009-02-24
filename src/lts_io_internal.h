#ifndef LTS_IO_INTERNAL_H
#define LTS_IO_INTERNAL_H

/**
\file lts_io_internal.h
\brief The internal structures of LTS I/O.
 */

#include <lts_io.h>
#include <lts_count.h>

struct lts_io_ops{
	void(*write_open)(lts_output_t output);
	lts_enum_cb_t(*write_begin)(lts_output_t output,int which_state,int which_src,int which_dst);
	void(*write_end)(lts_output_t output,lts_enum_cb_t writer);
	void(*write_close)(lts_output_t output);
	void(*read_open)(lts_input_t input);
	void(*read_part)(lts_input_t input,int which_state,int which_src,int which_dst,lts_enum_cb_t output);
	void(*read_close)(lts_input_t input);	
};

struct lts_output_struct{
	char *name;
	model_t model;
	int share;
	int share_count;
	int segment_count;
	struct lts_io_ops *ops;
	void* ops_context;
	lts_count_t count;
	uint32_t root_seg;
	uint64_t root_ofs;
};

struct lts_input_struct {
	char *name;
	model_t model;
	int share;
	int share_count;
	int segment_count;
	struct lts_io_ops *ops;
	void* ops_context;
	lts_count_t count;
	uint32_t root_seg;
	uint64_t root_ofs;
};

#endif

