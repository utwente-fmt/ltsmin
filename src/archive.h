
#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "config.h"
#include "stream.h"
#include "raf.h"
typedef struct archive_s *archive_t;

extern int arch_readable(archive_t archive);
extern stream_t arch_read(archive_t archive,char *name,char*code);

extern int arch_writable(archive_t archive);
extern stream_t arch_write(archive_t archive,char *name,char*code,int hdr);

typedef struct arch_enum* arch_enum_t;
extern arch_enum_t arch_enum(archive_t archive,char *regex);

struct arch_enum_callbacks {
	int(*new_item)(void*arg,int no,char*name);
	int(*end_item)(void*arg,int no);
	int(*data)(void*arg,int no,void*data,size_t len);
};
/**
 * Returns 0 if everything is completed. If one of the callbacks returns non-zero this
   non-zero response is immediately returned.
 */
extern int arch_enumerate(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg);
extern void arch_enum_free(arch_enum_t* enumerator);

extern void arch_close(archive_t *archive);

typedef stream_t(*stream_create_t)(char*name);

extern archive_t arch_fmt(char*format,stream_create_t crd,stream_create_t cwr,int buf);

extern archive_t arch_dir_create(char*dirname,int buf,int del);

extern archive_t arch_dir_open(char*dirname,int buf);

extern archive_t arch_gcf_create(raf_t raf,size_t block_size,size_t cluster_size,int worker,int worker_count);

extern archive_t arch_gcf_read(raf_t raf);

extern archive_t arch_gsf_read(stream_t s);

extern archive_t arch_gsf_write(stream_t s);

extern archive_t arch_gsf(stream_t s);

#endif



