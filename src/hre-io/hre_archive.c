// -*- tab-width:4 ; indent-tabs-mode:nil -*-
#include <hre/config.h>

#include <hre-io/arch_object.h>
#include <hre-io/user.h>

struct archive_s {
    struct archive_obj procs;
};

const char* arch_name(archive_t archive){
    return archive->procs.name;
}

string_map_t arch_get_write_policy(archive_t archive){
    return archive->procs.write_policy;
}

void arch_set_write_policy(archive_t archive,string_map_t policy){
    archive->procs.write_policy=policy;
}

int arch_readable(archive_t archive){
    return (archive->procs.read!=arch_illegal_read);
}

int arch_contains(archive_t archive,char *name){
    return archive->procs.contains(archive,name);
}

stream_t arch_read_raw(archive_t archive,char *name,char**code){
    if (archive->procs.flags&ARCH_TRANSPARENT_COMPRESSION){
        stream_t s=archive->procs.read_raw(archive,name,NULL);
        char*hdr=DSreadSA(s);
        if (code) *code=hdr; else RTfree(hdr);
        return s;
    } else {
        return archive->procs.read_raw(archive,name,code);
    }
}

stream_t arch_read(archive_t archive,char *name){
    if (archive->procs.flags&ARCH_TRANSPARENT_COMPRESSION){
        stream_t s=archive->procs.read_raw(archive,name,NULL);
        char code[1024];
        DSreadS(s,code,1024);
        return stream_add_code(s,code);
    } else {
        return archive->procs.read(archive,name);
    }
}

int arch_writable(archive_t archive){
    return (archive->procs.write!=arch_illegal_write);
}

stream_t arch_write_apply(archive_t archive,char *name,char*code){
    if (archive->procs.flags&ARCH_TRANSPARENT_COMPRESSION){
        stream_t s=archive->procs.write(archive,name,NULL);
        if (code==NULL) code="";
        DSwriteS(s,code);
        s=stream_add_code(s,code);
        return s;
    } else {
        return archive->procs.write(archive,name,code);
    }
}

stream_t arch_write_raw(archive_t archive,char *name,char*code){
    if (archive->procs.flags&ARCH_TRANSPARENT_COMPRESSION){
        stream_t s=archive->procs.write(archive,name,NULL);
        if (code==NULL) code="";
        DSwriteS(s,code);
        return s;
    } else {
        return archive->procs.write_raw(archive,name,code);
    }
}

stream_t arch_write(archive_t archive,char *name){
    char*policy=NULL;
    if (archive->procs.write_policy) {
        policy=SSMcall(archive->procs.write_policy,name);
    }
    return arch_write_apply(archive,name,policy);
}

arch_enum_t arch_enum(archive_t archive,char *regex){
    return archive->procs.enumerator(archive,regex);
}

void arch_close(archive_t *archive){
    (*archive)->procs.close(archive);
}

struct arch_enum {
    struct archive_enum_obj procs;
};

int arch_enumerate(arch_enum_t enumerator,struct arch_enum_callbacks *cb,void*arg){
    return enumerator->procs.enumerate(enumerator,cb,arg);
}
void arch_enum_free(arch_enum_t* enumerator){
    (*enumerator)->procs.free(enumerator);
}

int arch_illegal_contains(archive_t arch,char*name){
    (void)arch;(void)name;
    Abort("illegal contains test on archive");
}

stream_t arch_illegal_read(archive_t arch,char*name){
    (void)arch;(void)name;
    Abort("illegal read on archive");
    return NULL;
}
stream_t arch_illegal_read_raw(archive_t arch,char*name,char**code){
    (void)arch;(void)name;(void)code;
    Abort("illegal read on archive");
    return NULL;
}
stream_t arch_illegal_write(archive_t arch,char*name,char*code){
    (void)arch;(void)name;(void)code;
    Abort("illegal write on archive");
    return NULL;
}
arch_enum_t arch_illegal_enum(archive_t arch,char*regex){
    (void)arch;(void)regex;
    Abort("This archive cannot create an(other) enumerator.");
    return NULL;
}
void arch_illegal_close(archive_t *arch){
    (void)arch;
    Abort("illegal close on archive");
}
void arch_init(archive_t arch){
    arch->procs.contains=arch_illegal_contains;
    arch->procs.read=arch_illegal_read;
    arch->procs.read_raw=arch_illegal_read_raw;
    arch->procs.write=arch_illegal_write;
    arch->procs.write_raw=arch_illegal_write;
    arch->procs.enumerator=arch_illegal_enum;
    arch->procs.close=arch_illegal_close;
    arch->procs.write_policy=NULL;
    arch->procs.name="<archive>";
    arch->procs.flags=0;
}

void arch_set_transparent_compression(archive_t arch){
    arch->procs.flags|=ARCH_TRANSPARENT_COMPRESSION;
}
