// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef GCF_COMMON_H
#define GCF_COMMON_H

#include <hre-io/user.h>

/**
@file gcf_common.h
@brief Defines the function for writing and reading the Generic Header Format.
*/

#define GHF_NEW 0x01
#define GHF_END 0x02
#define GHF_EOF 0x03
#define GHF_DAT 0x04
#define GHF_LEN 0x05
#define GHF_CODE 0x06
#define GHF_ORIG 0x07

extern void ghf_skip(stream_t ds,uint8_t tag);

extern void ghf_write_new(stream_t ds,uint32_t id,char*name);
extern void ghf_read_new(stream_t ds,uint32_t *id,char**name);

extern void ghf_write_end(stream_t ds,uint32_t id);
extern void ghf_read_end(stream_t ds,uint32_t *id);

extern void ghf_write_eof(stream_t ds);

extern void ghf_write_data(stream_t ds,uint32_t id,void*buf,size_t count);
extern void ghf_read_data(stream_t ds,uint32_t *id,size_t*count);

extern void ghf_write_len(stream_t ds,uint32_t id,off_t len);
extern void ghf_read_len(stream_t ds,uint32_t *id,off_t *len);

extern void ghf_write_code(stream_t ds,uint32_t id,char*code);
extern void ghf_read_code(stream_t ds,uint32_t *id,char**code);

extern void ghf_write_orig(stream_t ds,uint32_t id,off_t len);
extern void ghf_read_orig(stream_t ds,uint32_t *id,off_t *len);

#endif

