
#ifndef ARCHIVE_OBJECT_H
#define ARCHIVE_OBJECT_H


#include "archive.h"

struct archive_obj {
	stream_t(*read)(archive_t arch,char*name);
	stream_t(*write)(archive_t arch,char*name);
	arch_enum_t(*enumerator)(archive_t arch,char*regex);
	void(*close)(archive_t *arch);
};

extern stream_t arch_illegal_read(archive_t arch,char*name);
extern stream_t arch_illegal_write(archive_t arch,char*name);
extern arch_enum_t arch_illegal_enum(archive_t arch,char*regex);
extern void arch_illegal_close(archive_t *arch);

extern void arch_init(archive_t arch);

struct archive_enum_obj {
	int(*enumerate)(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg);
	void(*free)(arch_enum_t* enumerator);
};

#endif

