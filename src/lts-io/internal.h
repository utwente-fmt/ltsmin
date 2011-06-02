// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef LTS_INTERNAL_H
#define LTS_INTERNAL_H

#include <lts-io/provider.h>
#include <hre-io/user.h>

lts_file_t aut_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);
lts_file_t aut_file_open(const char* name);

lts_file_t bcg_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);
lts_file_t bcg_file_open(const char* name);

lts_file_t dir_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);
lts_file_t dir_file_open(const char* name);

lts_file_t gcd_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);
lts_file_t gcd_file_open(const char* name);

lts_file_t gcf_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);
lts_file_t gcf_file_open(const char* name);

lts_file_t fsm_file_create(const char* name,lts_type_t ltstype,int segments,lts_file_t settings);

lts_file_t vector_open(archive_t arch);

extern int lts_io_blocksize;
extern int lts_io_blockcount;

#endif

