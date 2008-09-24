#ifndef GHF_H
#define GHF_H

#include "config.h"
#include "data_io.h"
#include <stdint.h>

/**
@file ghf.h
@brief Defines the function for writing and reading the Generic Header Format.
*/

#define GHF_NEW 0x01
#define GHF_END 0x02
#define GHF_EOF 0x03
#define GHF_DAT 0x04
#define GHF_LEN 0x05

extern void ghf_skip(data_stream_t ds,uint8_t tag);

extern void ghf_write_new(data_stream_t ds,uint32_t id,char*name);
extern void ghf_read_new(data_stream_t ds,uint32_t *id,char**name);

extern void ghf_write_end(data_stream_t ds,uint32_t id);
extern void ghf_read_end(data_stream_t ds,uint32_t *id);

extern void ghf_write_eof(data_stream_t ds);

extern void ghf_write_data(data_stream_t ds,uint32_t id,void*buf,size_t count);
extern void ghf_read_data(data_stream_t ds,uint32_t *id,size_t*count);

extern void ghf_write_len(data_stream_t ds,uint32_t id,off_t len);
extern void ghf_read_len(data_stream_t ds,uint32_t *id,off_t *len);


#endif
