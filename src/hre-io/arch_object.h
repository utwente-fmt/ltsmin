// -*- tab-width:4 ; indent-tabs-mode:nil -*-

#ifndef ARCHIVE_OBJECT_H
#define ARCHIVE_OBJECT_H

#include <hre-io/user.h>

struct archive_obj {
    int(*contains)(archive_t arch,char*name);
    stream_t(*read)(archive_t arch,char*name);
    stream_t(*read_raw)(archive_t arch,char*name,char**code);
    stream_t(*write)(archive_t arch,char*name,char *code);
    stream_t(*write_raw)(archive_t arch,char*name,char *code);
    arch_enum_t(*enumerator)(archive_t arch,char*regex);
    void(*close)(archive_t *arch);
    string_map_t write_policy;
    const char* name;
    int flags;
};

#define ARCH_TRANSPARENT_COMPRESSION 1

extern int arch_illegal_contains(archive_t arch,char*name);
extern stream_t arch_illegal_read(archive_t arch,char*name);
extern stream_t arch_illegal_read_raw(archive_t arch,char*name,char**code);
extern stream_t arch_illegal_write(archive_t arch,char*name,char*code);
extern arch_enum_t arch_illegal_enum(archive_t arch,char*regex);
extern void arch_illegal_close(archive_t *arch);

extern void arch_init(archive_t arch);

struct archive_enum_obj {
    int(*enumerate)(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg);
    void(*free)(arch_enum_t* enumerator);
};

#endif

