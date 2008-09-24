
#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "config.h"
#include "stream.h"
#include "raf.h"
typedef struct archive_s *archive_t;

extern int arch_readable(archive_t archive);
extern stream_t arch_read(archive_t archive,char *name);

extern int arch_writable(archive_t archive);
extern stream_t arch_write(archive_t archive,char *name);

extern int arch_searchable(archive_t archive);
typedef void(*string_enum_t)(void*arg,char*name);
extern void arch_search(archive_t archive,char *regex,string_enum_t cb,void*arg);


extern int arch_enumerable(archive_t archive);
struct archive_enum {
	void(*new_item)(void*arg,int no,char*name);
	void(*end_item)(void*arg,int no);
	void(*data)(void*arg,int no,void*data,size_t len);
};
extern void arch_enum(archive_t archive,char *regex,struct archive_enum *cb,void*arg);

extern void arch_close(archive_t *archive);

typedef stream_t(*stream_create_t)(char*name);

extern archive_t arch_dir(char*dirname,int buf);

extern archive_t arch_fmt(char*format,stream_create_t crd,stream_create_t cwr,int buf);

extern archive_t arch_gsf_read(stream_t s);

extern archive_t arch_gsf_write(stream_t s);

extern archive_t arch_gsf(stream_t s);

extern archive_t arch_gcf_create(raf_t raf,size_t block_size,size_t cluster_size,int worker,int worker_count);

extern archive_t arch_gcf_read(raf_t raf);

#endif



