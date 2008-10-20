
#ifndef ARCHIVE_OBJECT_H
#define ARCHIVE_OBJECT_H


#include "archive.h"

struct archive_obj {
	stream_t(*read)(archive_t arch,char*name);
	stream_t(*write)(archive_t arch,char*name);
	void(*list)(archive_t arch,char*regex,string_enum_t cb,void*arg);
	void(*play)(archive_t arch,char*regex,struct archive_enum *cb,void*arg);
	void(*close)(archive_t *arch);
};

extern void arch_play(archive_t arch,char*regex,struct archive_enum *cb,void*arg);

extern stream_t arch_illegal_read(archive_t arch,char*name);
extern stream_t arch_illegal_write(archive_t arch,char*name);
extern void arch_illegal_list(archive_t arch,char*regex,string_enum_t cb,void*arg);
extern void arch_illegal_play(archive_t arch,char*regex,struct archive_enum *cb,void*arg);
extern void arch_illegal_close(archive_t *arch);
extern void arch_init(archive_t arch);

#endif

